// respack.cpp : resource packer
//

#include <pnglite.h>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <map>



enum class Type
{
    invalid, image, shader_vert, shader_frag
};

struct Image
{
    size_t width, height;
};

struct Shader
{
    size_t vert_len, frag_len;

    Shader() : vert_len(0), frag_len(0)
    {
    }
};

inline bool check_postfix(const char *str, size_t &len, const char *postfix, size_t n)
{
    if(len <= n || std::memcmp(str + len - n, postfix, n))return false;
    len -= n;  return true;
}

Type classify(const char *name, size_t &len)
{
    Type type;
    if(check_postfix(name, len, ".png", 4))type = Type::image;
    else if(check_postfix(name, len, ".vert", 5))type = Type::shader_vert;
    else if(check_postfix(name, len, ".frag", 5))type = Type::shader_frag;
    else return Type::invalid;

    for(size_t i = 0; i < len; i++)
        if((name[i] < 'a' || name[i] > 'z') && name[i] != '_')return Type::invalid;
    return type;
}


class File
{
    FILE *stream;

    File() = delete;
    File(const File &file) = delete;
    File &operator = (const File &file) = delete;

public:
    File(const char *path, const char *mode) : stream(std::fopen(path, mode))
    {
    }

    operator FILE * ()
    {
        return stream;
    }

    ~File()
    {
        if(stream)std::fclose(stream);
    }
};

size_t cannot_open(const char *path)
{
    std::printf("Cannot open file \"%s\"!\n", path);  return 0;
}

size_t read_error()
{
    std::printf("Read error!\n");  return 0;
}

size_t write_error()
{
    std::printf("Write error!\n");  return 0;
}

size_t load_shader(const char *type, const char *name, size_t len, FILE *output)
{
    std::string path = "shaders/" + std::string(name);

    File input(path.c_str(), "rb");
    if(!input)return cannot_open(path.c_str());
    if(std::setvbuf(input, nullptr, _IONBF, 0))
    {
        std::printf("I/O error!\n");  return 0;
    }
    if(std::feof(input))
    {
        std::printf("Empty file \"%s\"!\n", path.c_str());  return 0;
    }

    if(std::fprintf(output, "\nstatic const char *shader_%.*s_%s =\n    \"", int(len), name, type) < 0)
        return write_error();

    size_t size = 0;
    for(unsigned char buf[65536];;)
    {
        size_t n = std::fread(buf, 1, sizeof(buf), input);
        if(std::ferror(input) || !n)return read_error();
        size += n;

        if(std::feof(input))n--;
        for(size_t i = 0; i < n; i++)
            if(std::fprintf(output, (i + 1) & 31 ? "\\x%02X" : "\\x%02X\"\n    \"", unsigned(buf[i])) < 0)
                return write_error();

        if(n == sizeof(buf))continue;
        if(std::fprintf(output, "\\x%02X\";\n", unsigned(buf[n])) < 0)return write_error();
        break;
    }
    return size;
}


bool load_image(const char *name, size_t len, FILE *output, Image &desc)
{
    std::string path = "images/" + std::string(name);

    png_t png;
    int err = png_open_file_read(&png, path.c_str());
    if(err)
    {
        std::printf("Cannot open PNG file \"%s\": %s\n", path.c_str(), png_error_string(err));
        png_close_file(&png);  return false;
    }
    if(png.depth != 8 || png.color_type != PNG_TRUECOLOR_ALPHA)
    {
        std::printf("Wrong format of PNG file \"%s\", should be 8-bit RGBA\n", path.c_str());
        png_close_file(&png);  return false;
    }
    desc.width = png.width;  desc.height = png.height;
    std::vector<unsigned char> buf(size_t(4) * png.width * png.height);
    err = png_get_data(&png, buf.data());  png_close_file(&png);
    if(err)
    {
        std::printf("Cannot read PNG file \"%s\": %s\n", path.c_str(), png_error_string(err));
        return false;
    }

    if(std::fprintf(output, "\nstatic const char *image_%.*s =\n    \"", int(len), name) < 0)
        return write_error();

    size_t n = buf.size() - 1;
    for(size_t i = 0; i < n; i++)
        if(std::fprintf(output, (i + 1) & 31 ? "\\x%02X" : "\\x%02X\"\n    \"", unsigned(buf[i])) < 0)
            return write_error();

    if(std::fprintf(output, "\\x%02X\";\n", unsigned(buf[n])) < 0)return write_error();
    return true;
}


bool process_files(const char *result, const char *include, size_t prefix, char **args, int n,
    std::map<std::string, Image> &images, std::map<std::string, Shader> &shaders)
{
    File output(result, "wb");  if(!output)return cannot_open(result);
    if(std::fprintf(output, "// %s : resource data\n//\n\n#include \"%s\"\n\n",
        result + prefix, include + prefix) < 0)return write_error();

    for(int i = 0; i < n; i++)
    {
        size_t len = std::strlen(args[i]);
        switch(classify(args[i], len))
        {
        case Type::image:
            if(load_image(args[i], len, output, images[std::string(args[i], len)]))break;
            else return false;

        case Type::shader_vert:
            {
                size_t size = load_shader("vert", args[i], len, output);
                if(size)shaders[std::string(args[i], len)].vert_len = size;
                else return false;  break;
            }
        case Type::shader_frag:
            {
                size_t size = load_shader("frag", args[i], len, output);
                if(size)shaders[std::string(args[i], len)].frag_len = size;
                else return false;  break;
            }
        default:
            std::printf("Invalid resource name \"%s\"!\n", args[i]);  return false;
        }
    }

    if(std::fprintf(output, "\nconst ImageDesc images[] =\n{") < 0)return write_error();
    for(const auto &image : images)
    {
        if(std::fprintf(output, "\n    {\"%s\", image_%s, %uu, %uu},", image.first.c_str(), image.first.c_str(),
            unsigned(image.second.width), unsigned(image.second.height)) < 0)return write_error();
    }
    if(std::fprintf(output, "\n};\n") < 0)return write_error();

    if(std::fprintf(output, "\nconst ShaderDesc shaders[] =\n{") < 0)return write_error();
    for(const auto &shader : shaders)
    {
        if(std::fprintf(output, "\n    {\"%s\", ", shader.first.c_str()) < 0)return write_error();
        if(shader.second.vert_len)
        {
            if(std::fprintf(output, "shader_%s_vert, ", shader.first.c_str()) < 0)return write_error();
        }
        else
        {
            if(std::fprintf(output, "nullptr, ") < 0)return write_error();
        }
        if(shader.second.frag_len)
        {
            if(std::fprintf(output, "shader_%s_frag, ", shader.first.c_str()) < 0)return write_error();
        }
        else
        {
            if(std::fprintf(output, "nullptr, ") < 0)return write_error();
        }
        if(std::fprintf(output, "%uu, %uu},",
            unsigned(shader.second.vert_len),
            unsigned(shader.second.frag_len)) < 0)return write_error();
    }
    if(std::fprintf(output, "\n};\n") < 0)return write_error();
    return true;
}

const char *header_tail = R"(
struct ImageDesc
{
    const char *name;
    const char *pixels;
    unsigned width, height;
};

struct ShaderDesc
{
    const char *name;
    const char *vert_src, *frag_src;
    unsigned vert_len, frag_len;
};

extern const ImageDesc images[];
extern const ShaderDesc shaders[];
)";

bool write_desc(const char *result, size_t prefix,
    const std::map<std::string, Image> &images, const std::map<std::string, Shader> &shaders)
{
    File output(result, "wb");  if(!output)return cannot_open(result);
    if(std::fprintf(output, "// %s : resource description\n//\n\n", result + prefix) < 0)
        return write_error();

    if(std::fprintf(output, "\nnamespace Image\n{\n    enum Index\n    {") < 0)return write_error();
    for(const auto &image : images)
        if(std::fprintf(output, "\n        %s,", image.first.c_str()) < 0)return write_error();
    if(std::fprintf(output, "\n    };\n}\n") < 0)return write_error();

    if(std::fprintf(output, "\nnamespace Shader\n{\n    enum Index\n    {") < 0)return write_error();
    for(const auto &shader : shaders)
        if(std::fprintf(output, "\n        %s,", shader.first.c_str()) < 0)return write_error();
    if(std::fprintf(output, "\n    };\n}\n") < 0)return write_error();

    return std::fprintf(output, "\n%s", header_tail) >= 0;
}

int main(int n, char **args)
{
    const char *result_h   = "build/resource.h";
    const char *result_cpp = "build/resource.cpp";
    const size_t prefix = 6;

    png_init(0, 0);
    std::map<std::string, Image> images;
    std::map<std::string, Shader> shaders;
    if(!process_files(result_cpp, result_h, prefix, args + 1, n - 1, images, shaders))return -1;
    return write_desc(result_h, prefix, images, shaders) ? 0 : -1;
}
