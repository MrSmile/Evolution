// graph.cpp : world rendering
//

#include "graph.h"
#include "video.h"
#include "stream.h"
#include <algorithm>
#include <cassert>



void print_checksum(const World &world, const OutStream &stream)
{
    const uint32_t *checksum = static_cast<const uint32_t *>(stream.checksum());
    std::printf("Time: %llu, Checksum:", (unsigned long long)world.current_time);
    for(unsigned i = 0; i < Hash::result_size / 4; i++)
        std::printf(" %08lX", (unsigned long)bswap32(checksum[i]));
    std::printf("\n");
}



// Camera struct

void Camera::update_scale()
{
    scale = std::exp2(tile_order - scale_step * log_scale);
}

Camera::Camera(SDL_Window *window) : log_scale(8 / scale_step), x(0), y(0)
{
    SDL_GetWindowSize(window, &width, &height);  update_scale();
}

void Camera::resize(int w, int h)
{
    width = w;  height = h;
}

void Camera::move(int dx, int dy)
{
    x -= std::lround(dx * scale);
    y -= std::lround(dy * scale);
}

void Camera::rescale(int delta)
{
    log_scale += delta;
    //if(log_scale < min_scale)log_scale = min_scale;
    //if(log_scale > max_scale)log_scale = max_scale;
    double old = scale;  update_scale();

    int x0, y0;
    SDL_GetMouseState(&x0, &y0);
    x -= std::lround(x0 * (scale - old));
    y -= std::lround(y0 * (scale - old));
}



// Representation class

enum AttributeFlags
{
    f_integer   = 1 << 0,
    f_normalize = 1 << 1,
    f_instance  = 1 << 2
};

struct VertexAttribute
{
    GLint size;
    GLenum type;
    uint32_t stride, offset;
    int flags;

    VertexAttribute(uint32_t stride, uint32_t offset, GLint size, GLenum type, int flags) :
        size(size), type(type), stride(stride), offset(offset), flags(flags)
    {
    }
};

void register_attributes(const VertexAttribute *attr, int attr_count, GLuint buf_base, GLuint buf_inst)
{
    for(int i = 0; i < attr_count; i++)
    {
        glEnableVertexAttribArray(i);
        glBindBuffer(GL_ARRAY_BUFFER, attr[i].flags & f_instance ? buf_inst : buf_base);
        if(attr[i].flags & f_integer)glVertexAttribIPointer(i, attr[i].size, attr[i].type,
            attr[i].stride, reinterpret_cast<void *>(attr[i].offset));
        else glVertexAttribPointer(i, attr[i].size, attr[i].type,
            attr[i].flags & f_normalize ? GL_TRUE : GL_FALSE,
            attr[i].stride, reinterpret_cast<void *>(attr[i].offset));
        if(attr[i].flags & f_instance)glVertexAttribDivisor(i, 1);
    }
}

#define ATTR(data_type, member, size, type, flags) \
    VertexAttribute(sizeof(data_type), offsetof(data_type, member), size, type, flags)

const VertexAttribute layout_food[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(FoodData,     x,       4,       GL_FLOAT,          f_instance),
};
const VertexAttribute layout_creature[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(CreatureData, x,       2,       GL_FLOAT,          f_instance),
    ATTR(CreatureData, rad,     3,       GL_FLOAT,          f_instance),
    ATTR(CreatureData, angle,   4,       GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_sector[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(SectorData,   x,       3,       GL_FLOAT,          f_instance),
    ATTR(SectorData,   angle,   2,       GL_UNSIGNED_BYTE,  f_instance),
    ATTR(SectorData,   color1,  GL_BGRA, GL_UNSIGNED_BYTE,  f_instance | f_normalize),
    ATTR(SectorData,   color2,  GL_BGRA, GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_leg[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(LegData,      x,       3,       GL_FLOAT,          f_instance),
    ATTR(LegData,      angle,   1,       GL_UNSIGNED_BYTE,  f_instance),
    ATTR(LegData,      color,   GL_BGRA, GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_sel[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
};
const VertexAttribute layout_back[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(GuiBack,      pos,     2,       GL_SHORT,          f_instance),
    ATTR(GuiBack,      color,   GL_BGRA, GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_gui[] =
{
    ATTR(Vertex,       x,       2,       GL_FLOAT,          0),
    ATTR(GuiQuad,      x,       2,       GL_SHORT,          f_instance),
    ATTR(GuiQuad,      tx,      4,       GL_UNSIGNED_BYTE,  f_instance),
};
const VertexAttribute layout_panel[] =
{
    ATTR(PanelVertex,  x,       2,       GL_SHORT,          0),
    ATTR(PanelVertex,  stretch, 1,       GL_UNSIGNED_SHORT, f_normalize),
    ATTR(PanelVertex,  tx,      2,       GL_UNSIGNED_BYTE,  0),
};

#undef ATTR

#define INFO(name, base, inst, index, alpha_blending) \
    {Representation::prog_##name, sizeof(layout_##name) / sizeof(VertexAttribute), \
        layout_##name, Representation::base, Representation::inst, Representation::index, alpha_blending}

const Representation::PassInfo Representation::pass_info[] =
{
    INFO(sector,   vtx_sector,   inst_sector,   idx_sector,   true),
    INFO(food,     vtx_food,     inst_food,     idx_food,     false),
    INFO(creature, vtx_creature, inst_creature, idx_creature, false),
    INFO(leg,      vtx_leg,      inst_leg,      idx_leg,      true),
    INFO(sel,      vtx_sel,      buf_count,     idx_sel,      false),
    INFO(back,     vtx_quad,     inst_slot_bg,  buf_count,    true),
    INFO(back,     vtx_quad,     inst_gene_bg,  buf_count,    true),
    INFO(gui,      vtx_quad,     inst_slot,     buf_count,    true),
    INFO(gui,      vtx_quad,     inst_level,    buf_count,    true),
    INFO(gui,      vtx_quad,     inst_link,     buf_count,    true),
    INFO(gui,      vtx_quad,     inst_gene,     buf_count,    true),
    INFO(gui,      vtx_quad,     inst_header,   buf_count,    true),
    INFO(panel,    vtx_panel,    buf_count,     idx_panel,    true),
};

#undef INFO


GLuint load_shader(GLint type, const char *name, int id)
{
    GLuint shader = glCreateShader(type);  GLint size = shaders[id].length;
    glShaderSource(shader, 1, &shaders[id].source, &size);

    char msg[65536];  GLsizei len;
    glCompileShader(shader);  glGetShaderInfoLog(shader, sizeof(msg), &len, msg);
    if(len)std::printf("%s shader \"%s\" log:\n%s\n", name, shaders[id].name, msg);
    return shader;
}

GLuint create_program(const char *name, VertShader::Index vert_id, FragShader::Index frag_id)
{
    GLuint prog = glCreateProgram();
    GLuint vert = load_shader(GL_VERTEX_SHADER, "Vertex", vert_id);
    GLuint frag = load_shader(GL_FRAGMENT_SHADER, "Fragment", frag_id);
    glAttachShader(prog, vert);  glAttachShader(prog, frag);

    char msg[65536];  GLsizei len;
    glLinkProgram(prog);  glGetProgramInfoLog(prog, sizeof(msg), &len, msg);
    if(len)std::printf("Shader program \"%s\" log:\n%s\n", name, msg);

    glDetachShader(prog, vert);  glDetachShader(prog, frag);
    glDeleteShader(vert);  glDeleteShader(frag);  return prog;
}


void Representation::fill_sel_bufs()
{
    sel.slot = -1;  sel.scroll[l_slot] = sel.scroll[l_gene] = 0;

    sel.fill_sel_genes(world.config,
        buf[inst_gene_bg], buf[inst_gene], count[inst_gene_bg], count[inst_gene]);
    sel.fill_sel_header(world.config, buf[inst_header], count[inst_header]);
    sel.fill_sel_slots(buf[inst_slot_bg], buf[inst_slot], count[inst_slot_bg], count[inst_slot]);
    sel.fill_sel_levels(buf[inst_level], count[inst_level]);
    sel.fill_sel_links(buf[inst_link], count[inst_link]);
    sel.fill_sel_limbs(buf[inst_sector], buf[inst_leg], count[inst_sector], count[inst_leg]);
}

void Representation::make_food_shape()
{
    constexpr int n = 3, m = 2 * n - 2;
    count[idx_food] = 3 * m + 3;
    count[inst_food] = 0;

    Vertex vertex[2 * n];
    for(int i = 0; i < 2 * n; i++)
    {
        double r = i & 1 ? 1.0 : 0.25;
        vertex[i].x = r * std::sin(i * (pi / n));
        vertex[i].y = r * std::cos(i * (pi / n));
    }

    Triangle triangle[m + 1];
    for(int i = 0; i < m; i += 2)
    {
        triangle[i + 0] = Triangle(i, i + 1, i + 2);
        triangle[i + 1] = Triangle(m, i,     i + 2);
    }
    triangle[m] = Triangle(m, m + 1, 0);

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_food]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_food]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
}

void Representation::make_creature_shape()
{
    constexpr int n = 8;
    count[idx_creature] = 3 * n;
    count[inst_creature] = 0;

    Vertex vertex[n + 1];
    double r = 1 / std::cos(pi / n);
    for(int i = 0; i < n; i++)
    {
        vertex[i].x = r * std::sin(i * (2 * pi / n));
        vertex[i].y = r * std::cos(i * (2 * pi / n));
    }
    vertex[n].x = vertex[n].y = 0;

    Triangle triangle[n];  triangle[0] = Triangle(n, n - 1, 0);
    for(int i = 1; i < n; i++)triangle[i] = Triangle(n, i - 1, i);

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_creature]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_creature]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
}

void Representation::make_sector_shape()
{
    constexpr int n = 4;
    count[idx_sector] = 3 * n;
    count[inst_sector] = 0;

    Vertex vertex[n + 2];
    for(int i = 0; i <= n; i++)vertex[i] = {1, GLfloat(i / double(n))};
    vertex[n + 1] = {0, 0.5};

    Triangle triangle[n];
    for(int i = 0; i < n; i++)triangle[i] = {i, n + 1, i + 1};

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_sector]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_sector]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
}

void Representation::make_leg_shape()
{
    count[idx_leg] = 6;
    count[inst_leg] = 0;

    Vertex vertex[4] =
    {
        {0, 0}, {0.5, 0.5}, {0.5, -0.5}, {1, 0}
    };

    Triangle triangle[2] =
    {
        {0, 1, 2}, {3, 2, 1}
    };

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_leg]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_leg]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
}

void Representation::make_sel_shape()
{
    constexpr int n = 24;
    constexpr GLfloat inner = 1.3, outer = 1.5, hole = 0.8;
    count[idx_sel] = n + 3;

    Vertex vertex[n], cur = {1, 0};
    for(int i = 0, k = 0; i < 4; i++)
    {
        vertex[k++] = {inner * cur.x + hole  * cur.y, inner * cur.y - hole  * cur.x};
        vertex[k++] = {outer * cur.x + hole  * cur.y, outer * cur.y - hole  * cur.x};
        vertex[k++] = {inner * cur.x + inner * cur.y, inner * cur.y - inner * cur.x};
        vertex[k++] = {outer * cur.x + outer * cur.y, outer * cur.y - outer * cur.x};
        vertex[k++] = {hole  * cur.x + inner * cur.y, hole  * cur.y - inner * cur.x};
        vertex[k++] = {hole  * cur.x + outer * cur.y, hole  * cur.y - outer * cur.x};
        cur = {cur.y, -cur.x};
    }

    GLubyte index[n + 3];  index[0] = 0;
    for(int i = 1, k = 1; i < n; i++)
    {
        if(!(i % 6))index[k++] = GLubyte(-1);  index[k++] = i;
    }

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_sel]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_sel]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index), index, GL_STATIC_DRAW);
}

void Representation::make_quad_shape()
{
    count[vtx_quad] = 4;

    Vertex vertex[4] =
    {
        {0, 0}, {0, 1}, {1, 0}, {1, 1}
    };

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_quad]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
    fill_sel_bufs();
}


void put_coords(GLshort *pos, GLubyte *tex, int &index, int width, bool stretch = false)
{
    pos[index] = width < 0 ? width : pos[index - 1] + width;
    tex[index] = tex[index - 1] + (stretch ? Gui::panel_stretch : width);
    index++;
}

void put_quad(PanelVertex *vertex, int x, int y, int tx, int ty, int width, int height)
{
    *vertex++ = PanelVertex(x, y, tx, ty);
    *vertex++ = PanelVertex(x, y + height, tx, ty + height);

    x += width;  tx += width;
    *vertex++ = PanelVertex(x, y, tx, ty);
    *vertex++ = PanelVertex(x, y + height, tx, ty + height);
}

void put_strip(GLubyte *buf, int &index, GLubyte base, GLubyte n, int flags)
{
    for(GLubyte i = 0; i < n; i++)
    {
        buf[index++] = base + i;  buf[index++] = base + n + i;
        if(flags & (1 << i))buf[index++] = GLubyte(-1);
    }
}

void put_strip(GLubyte *buf, int &index, GLubyte base, GLubyte n)
{
    put_strip(buf, index, base, n, 1 << (n - 1));
}

void Representation::make_panel()
{
    constexpr int nx = 6, ny = 6, nb = 4, ns = 4;
    constexpr int m = (2 * nx + 1) * (ny - 1) + 4 * nb + 13;
    count[vtx_panel] = m;  count[idx_panel] = 2 * ns;

    int index;
    GLshort  x[nx],  y[ny],  b[nb],  s[ns];
    GLubyte tx[nx], ty[ny], tb[nb], ts[ns];

    x[0] = -Gui::panel_border;  tx[0] = 0;  index = 1;
    put_coords(x, tx, index, 2 * Gui::panel_border);
    put_coords(x, tx, index, Gui::slot_width - 2 * Gui::panel_border, true);
    put_coords(x, tx, index, Gui::scroll_width + 2 * Gui::panel_border);
    put_coords(x, tx, index, Gui::gene_width - 2 * Gui::panel_border, true);
    put_coords(x, tx, index, Gui::scroll_width + Gui::panel_border);
    assert(index == nx);

    y[0] = 0;  ty[0] = Gui::panel_border;  index = 1;
    put_coords(y, ty, index, Gui::panel_border);
    put_coords(y, ty, index, Gui::header_height - 2 * Gui::panel_border, true);
    put_coords(y, ty, index, 2 * Gui::panel_border);
    put_coords(y, ty, index, -Gui::panel_border, true);
    put_coords(y, ty, index, Gui::panel_border);
    assert(index == ny);

    b[0] = Gui::sel_icon_size + Gui::panel_border;
    tb[0] = Gui::sel_bar1_pos_x;  index = 1;
    put_coords(b, tb, index, 2 * Gui::panel_border);
    put_coords(b, tb, index, Gui::bar_width - 2 * Gui::panel_border, true);
    put_coords(b, tb, index, 2 * Gui::panel_border);
    assert(index == nb);

    s[0] = 0;  ts[0] = Gui::scroll_pos_y;  index = 1;
    put_coords(s, ts, index, Gui::panel_border);
    put_coords(s, ts, index, 0, true);
    put_coords(s, ts, index, Gui::panel_border);
    assert(index == ns);

    PanelVertex vertex[nx * ny + 4 * nb + 2 * ns + 8];
    for(int i = 0, k = 0; i < ny; i++)for(int j = 0; j < nx; j++, k++)
    {
        vertex[k].x = x[j];  vertex[k].tx = tx[j];
        vertex[k].y = y[i];  vertex[k].ty = ty[i];
        vertex[k].stretch = i > 3 ? GLushort(-1) : 0;
    }

    index = nx * ny;
    for(int k = 0; k < 2; k++)for(int i = 0; i < nb; i++, index += 2)
    {
        vertex[index].x  = vertex[index + 1].x  = b[i];
        vertex[index].tx = vertex[index + 1].tx = tb[i];

        vertex[index].y = Gui::margin + k * Gui::line_spacing;
        vertex[index].ty = Gui::sel_bar1_pos_y;

        vertex[index + 1].y  = vertex[index].y  + Gui::control_height;
        vertex[index + 1].ty = vertex[index].ty + Gui::control_height;

        vertex[index].stretch = vertex[index + 1].stretch = 0;
    }

    put_quad(vertex + index, Gui::panel_border, Gui::panel_border,
        Gui::sel_icon_pos_x, Gui::sel_icon_pos_y, Gui::sel_icon_size, Gui::sel_icon_size);
    index += 4;

    put_quad(vertex + index, Gui::sel_all_offs_x, Gui::sel_all_offs_y,
        Gui::sel_all_pos_x, Gui::sel_all_pos_y, Gui::sel_all_width, Gui::control_height);
    index += 4;

    for(int i = 0; i < ns; i++, index += 2)
    {
        vertex[index + 1].x = 0;  vertex[index + 1].tx = Gui::scroll_pos_x;
        vertex[index].x  = vertex[index + 1].x  + Gui::scroll_width;
        vertex[index].tx = vertex[index + 1].tx + Gui::scroll_width;

        vertex[index].y  = vertex[index + 1].y  = s[i];
        vertex[index].ty = vertex[index + 1].ty = ts[i];
        vertex[index].stretch = vertex[index + 1].stretch = i > 1 ? GLushort(-1) : 0;
    }

    GLubyte strip[m + 2 * ns];  index = 0;
    put_strip(strip, index, 0 * nx, nx);
    put_strip(strip, index, 1 * nx, nx);
    put_strip(strip, index, 2 * nx, nx);
    put_strip(strip, index, 3 * nx, nx, 0x2A);
    put_strip(strip, index, 4 * nx, nx);

    int offs = nx * ny;
    for(int i = 0; i < 2 * nb; i++)strip[index++] = offs + i;
    strip[index++] = GLubyte(-1);  offs += 2 * nb;

    for(int i = 0; i < 2 * nb; i++)strip[index++] = offs + i;
    strip[index++] = GLubyte(-1);  offs += 2 * nb;

    for(int i = 0; i < 4; i++)strip[index++] = offs + i;
    strip[index++] = GLubyte(-1);  offs += 4;
    for(int i = 0; i < 4; i++)strip[index++] = offs + i;
    assert(index == m);  offs += 4;

    for(int i = 0; i < 2 * ns; i++)strip[index++] = offs + i;

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_panel]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_panel]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(strip), strip, GL_STATIC_DRAW);
}


GLuint load_texture(Image::Index id)
{
    GLuint tex;  glGenTextures(1, &tex);  glBindTexture(GL_TEXTURE_2D, tex);  const ImageDesc &image = images[id];
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);
    return tex;
}

Representation::Representation(World &world, SDL_Window *window) : world(world), cam(window), move(t_none)
{
    prog[prog_food] = create_program("food", VertShader::food, FragShader::color);
    i_transform[prog_food] = glGetUniformLocation(prog[prog_food], "transform");

    prog[prog_creature] = create_program("creature", VertShader::creature, FragShader::creature);
    i_transform[prog_creature] = glGetUniformLocation(prog[prog_creature], "transform");

    prog[prog_sector] = create_program("sector", VertShader::sector, FragShader::sector);
    i_transform[prog_sector] = glGetUniformLocation(prog[prog_sector], "transform");

    prog[prog_leg] = create_program("leg", VertShader::leg, FragShader::color);
    i_transform[prog_leg] = glGetUniformLocation(prog[prog_leg], "transform");

    prog[prog_sel] = create_program("back", VertShader::sel, FragShader::color);
    i_transform[prog_sel] = glGetUniformLocation(prog[prog_sel], "transform");
    i_sel = glGetUniformLocation(prog[prog_sel], "sel");

    prog[prog_back] = create_program("back", VertShader::back, FragShader::color);
    i_transform[prog_back] = glGetUniformLocation(prog[prog_back], "transform");
    i_size = glGetUniformLocation(prog[prog_back], "size");

    prog[prog_gui] = create_program("gui", VertShader::gui, FragShader::texture);
    i_transform[prog_gui] = glGetUniformLocation(prog[prog_gui], "transform");
    i_gui = glGetUniformLocation(prog[prog_gui], "tex");

    prog[prog_panel] = create_program("panel", VertShader::panel, FragShader::texture);
    i_transform[prog_panel] = glGetUniformLocation(prog[prog_panel], "transform");
    i_size = glGetUniformLocation(prog[prog_panel], "height");
    i_panel = glGetUniformLocation(prog[prog_panel], "tex");


    glGenVertexArrays(pass_count, arr);
    glGenBuffers(buf_count, buf);

    for(int pass = 0; pass < pass_count; pass++)
    {
        glBindVertexArray(arr[pass]);  auto &info = pass_info[pass];
        register_attributes(info.attr, info.attr_count, buf[info.base], buf[info.inst]);
        if(info.index < buf_count)glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[info.index]);
    }
    glBindVertexArray(0);

    tex_gui = load_texture(Image::gui);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    tex_panel = load_texture(Image::panel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    make_food_shape();
    make_creature_shape();
    make_sector_shape();
    make_leg_shape();
    make_sel_shape();
    make_quad_shape();
    make_panel();
}

Representation::~Representation()
{
    for(int i = 0; i < prog_count; i++)glDeleteProgram(prog[i]);
    glDeleteVertexArrays(pass_count, arr);  glDeleteBuffers(buf_count, buf);
    glDeleteTextures(1, &tex_gui);  glDeleteTextures(1, &tex_panel);
}


void Representation::resize(int w, int h)
{
    cam.resize(w, h);
}

Representation::HitTest Representation::hit_test(int &x, int &y)
{
    if(x < cam.width - Gui::panel_width)return t_field;
    x -= cam.width - Gui::panel_width;

    if(y < Gui::header_height)
    {
        if(x < Gui::sel_all_offs_x || y < Gui::sel_all_offs_y)return t_none;
        x -= Gui::sel_all_offs_x;  y -= Gui::sel_all_offs_y;

        if(x < Gui::control_height && y < Gui::sel_all_width)return t_show_all;
        return t_none;
    }
    y -= Gui::header_height;

    if(x < Gui::slot_width + Gui::scroll_width)
    {
        if(x >= Gui::slot_width)return t_slot_scroll;
        y += sel.scroll[l_slot];  return t_slots;
    }
    else
    {
        x -= Gui::slot_width + Gui::scroll_width;
        if(x >= Gui::gene_width)return t_gene_scroll;
        y += sel.scroll[l_gene];  return t_genes;
    }
}

bool Representation::select_slot(List list, int y)
{
    unsigned pos = y / Gui::line_spacing;
    int slot = pos < sel.mapping[list].size() ? sel.mapping[list][pos] : -1;
    if(sel.slot == slot)return false;

    sel.slot = slot;
    sel.fill_sel_links(buf[inst_link], count[inst_link]);
    sel.fill_sel_limbs(buf[inst_sector], buf[inst_leg], count[inst_sector], count[inst_leg]);
    return true;
}

bool Representation::mouse_wheel(const SDL_MouseWheelEvent &evt)
{
    int x, y;
    SDL_GetMouseState(&x, &y);
    switch(hit_test(x, y))
    {
    case t_field:
        cam.rescale(evt.y);  return true;

    case t_slots:  case t_slot_scroll:
        sel.set_scroll(cam, l_slot, sel.scroll[l_slot] - 4 * Gui::line_spacing * evt.y);  return true;

    case t_genes:  case t_gene_scroll:
        sel.set_scroll(cam, l_gene, sel.scroll[l_gene] - 4 * Gui::line_spacing * evt.y);  return true;

    default:
        return false;
    }
}

bool Representation::mouse_down(const SDL_MouseButtonEvent &evt)
{
    int x = evt.x, y = evt.y;
    HitTest test = hit_test(x, y);
    if(evt.button == SDL_BUTTON_LEFT)switch(test)
    {
    case t_field:
        return select(x, y);

    case t_show_all:
        sel.skip_unused = !sel.skip_unused;
        sel.fill_sel_header(world.config, buf[inst_header], count[inst_header]);
        sel.fill_sel_slots(buf[inst_slot_bg], buf[inst_slot], count[inst_slot_bg], count[inst_slot]);
        sel.fill_sel_levels(buf[inst_level], count[inst_level]);
        sel.fill_sel_links(buf[inst_link], count[inst_link]);
        sel.fill_sel_limbs(buf[inst_sector], buf[inst_leg], count[inst_sector], count[inst_leg]);
        sel.set_scroll(cam, l_slot, sel.scroll[l_slot]);
        return true;

    case t_slots:
        return select_slot(l_slot, y);

    case t_slot_scroll:
        scroll_base = sel.scroll[l_slot];
        mouse_start = evt.y;  move = t_slot_scroll;  break;

    case t_genes:
        return select_slot(l_gene, y);

    case t_gene_scroll:
        scroll_base = sel.scroll[l_gene];
        mouse_start = evt.y;  move = t_gene_scroll;  break;

    default:
        return false;
    }
    else if(evt.button == SDL_BUTTON_RIGHT)switch(test)
    {
    case t_field:
        move = t_field;  break;

    case t_slots:
        scroll_base = sel.scroll[l_slot];
        mouse_start = evt.y;  move = t_slots;  break;

    case t_genes:
        scroll_base = sel.scroll[l_gene];
        mouse_start = evt.y;  move = t_genes;  break;

    default:
        return false;
    }
    else return false;

    SDL_CaptureMouse(SDL_TRUE);  return false;
}

bool Representation::mouse_move(const SDL_MouseMotionEvent &evt)
{
    switch(move)
    {
    case t_field:  cam.move(evt.xrel, evt.yrel);  return true;
    case t_slots:  sel.set_scroll(cam, l_slot, scroll_base + mouse_start - evt.y);  return true;
    case t_slot_scroll:  sel.drag_scroll(cam, l_slot, scroll_base, evt.y - mouse_start);  return true;
    case t_genes:  sel.set_scroll(cam, l_gene, scroll_base + mouse_start - evt.y);  return true;
    case t_gene_scroll:  sel.drag_scroll(cam, l_gene, scroll_base, evt.y - mouse_start);  return true;
    default:  return false;
    }
}

bool Representation::mouse_up(const SDL_MouseButtonEvent &evt)
{
    if(!move)return false;
    uint8_t test = (move >= t_scroll ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT);
    if(evt.button != test)return false;

    SDL_CaptureMouse(SDL_FALSE);  move = t_none;  return false;
}


bool Representation::select(int32_t x, int32_t y)
{
    constexpr int click_zone = 8;

    uint64_t x0 = cam.x + std::lround(x * cam.scale);
    uint64_t y0 = cam.y + std::lround(y * cam.scale);
    uint32_t rad = std::min<long>(tile_size, world.config.base_radius + std::lround(click_zone * cam.scale));
    sel.cr = world.hit_test({x0, y0}, rad, sel.id);
    if(!sel.cr)
    {
        if(sel.id == uint64_t(-1))return false;
        sel.id = uint64_t(-1);  fill_sel_bufs();  return true;
    }
    if(sel.cr->id == sel.id)return false;

    sel.id = sel.cr->id;  sel.pos = sel.cr->pos;
    sel.proc.process(world.config, sel.cr->genome);
    Creature::calc_mapping(sel.proc, sel.input_mapping);
    fill_sel_bufs();  return true;
}

void Representation::update(SDL_Window *window, bool checksum, bool draw)
{
    world.count_objects();

    char title[256];
    snprintf(title, sizeof(title),
        "Evolution - Time: %llu, Food: %lu, Creature: %lu", (unsigned long long)world.current_time,
        (unsigned long)world.food_total(), (unsigned long)world.creature_total());
    SDL_SetWindowTitle(window, title);

    if(checksum || !(world.current_time % 1000))
    {
        OutStream stream;  stream.initialize();
        stream << world;  stream.finalize();
        print_checksum(world, stream);
    }

    if(!draw)return;

    count[inst_food] = world.food_total();
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);  FoodData *food_buf = nullptr;
    glBufferData(GL_ARRAY_BUFFER, count[inst_food] * sizeof(FoodData), nullptr, GL_STREAM_DRAW);
    if(count[inst_food])food_buf = static_cast<FoodData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    count[inst_creature] = world.creature_total();
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);  CreatureData *creature_buf = nullptr;
    glBufferData(GL_ARRAY_BUFFER, count[inst_creature] * sizeof(CreatureData), nullptr, GL_STREAM_DRAW);
    if(count[inst_creature])creature_buf = static_cast<CreatureData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    sel.cr = world.update(food_buf, creature_buf, sel.id);
    sel.fill_sel_header(world.config, buf[inst_header], count[inst_header]);
    sel.fill_sel_levels(buf[inst_level], count[inst_level]);
    sel.fill_sel_limbs(buf[inst_sector], buf[inst_leg], count[inst_sector], count[inst_leg]);

    if(sel.cr)
    {
        cam.x += sel.cr->pos.x - sel.pos.x;
        cam.y += sel.cr->pos.y - sel.pos.y;
        sel.pos = sel.cr->pos;
    }

    if(food_buf)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    if(creature_buf)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
}

void Representation::draw()
{
    glViewport(0, 0, cam.width, cam.height);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_PRIMITIVE_RESTART);  glPrimitiveRestartIndex(255);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    double mul_x = 2 / (cam.width  * cam.scale);
    double mul_y = 2 / (cam.height * cam.scale);
    double scale_x = tile_size * mul_x, dx = (world.config.mask_x + 1) * scale_x;
    double scale_y = tile_size * mul_y, dy = (world.config.mask_y + 1) * scale_y;
    double x_end = 1 + scale_x, x_beg = x_end + ((cam.x - tile_size) & world.config.full_mask_x) * mul_x;
    double y_end = 1 + scale_y, y_beg = y_end + ((cam.y - tile_size) & world.config.full_mask_y) * mul_y;

    Program cur = prog_count;
    for(int pass = 0; pass < pass_sel; pass++)
    {
        const PassInfo &info = pass_info[pass];
        if(cur != info.prog)glUseProgram(prog[cur = info.prog]);
        if(info.alpha_blending)glEnable(GL_BLEND);
        else glDisable(GL_BLEND);

        glBindVertexArray(arr[pass]);
        for(double y = -y_beg; y < y_end; y += dy)for(double x = -x_beg; x < x_end; x += dx)
        {
            glUniform4f(i_transform[cur], x, -y, scale_x, -scale_y);
            glDrawElementsInstanced(GL_TRIANGLES, count[info.index], GL_UNSIGNED_BYTE, nullptr, count[info.inst]);
        }
    }

    if(sel.cr)
    {
        glDisable(GL_BLEND);
        double radius = world.config.base_radius + 4 * cam.scale;
        glUseProgram(prog[prog_sel]);  glBindVertexArray(arr[pass_sel]);
        glUniform3f(i_sel, sel.pos.x * draw_scale, sel.pos.y * draw_scale, radius * draw_scale);
        for(double y = -y_beg; y < y_end; y += dy)for(double x = -x_beg; x < x_end; x += dx)
        {
            glUniform4f(i_transform[prog_sel], x, -y, scale_x, -scale_y);
            glDrawElements(GL_TRIANGLE_STRIP, count[idx_sel], GL_UNSIGNED_BYTE, nullptr);
        }
    }


    int list_height = cam.height - Gui::header_height;
    int scroll_gap = list_height - 2 * Gui::panel_border;
    if(scroll_gap < 0)return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mul_x = 2.0 / cam.width;  mul_y = 2.0 / cam.height;

    double x_slot = 1 - Gui::panel_width * mul_x;
    double x_gene = 1 - (Gui::gene_width + Gui::scroll_width) * mul_x;
    double y_slot = 1 - (Gui::header_height - sel.scroll[l_slot]) * mul_y;
    double y_gene = 1 - (Gui::header_height - sel.scroll[l_gene]) * mul_y;

    glEnable(GL_SCISSOR_TEST);
    glScissor(cam.width - Gui::panel_width, 0, Gui::panel_width, list_height);

    glUseProgram(prog[prog_back]);  glBindVertexArray(arr[pass_slot_bg]);
    glUniform4f(i_transform[prog_back], x_slot, y_slot, mul_x, -mul_y);
    glUniform3f(i_size, Gui::slot_width, Gui::line_spacing, sel.slot);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_slot_bg]);

    glBindVertexArray(arr[pass_gene_bg]);
    glUniform4f(i_transform[prog_back], x_gene, y_gene, mul_x, -mul_y);
    glUniform3f(i_size, Gui::gene_width, Gui::line_spacing, sel.slot);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_gene_bg]);

    glUseProgram(prog[prog_gui]);  glUniform1i(i_gui, 0);
    glActiveTexture(GL_TEXTURE0);  glBindTexture(GL_TEXTURE_2D, tex_gui);

    glBindVertexArray(arr[pass_slot]);
    glUniform4f(i_transform[prog_gui], x_slot, y_slot, mul_x, -mul_y);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_slot]);

    glBindVertexArray(arr[pass_level]);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_level]);

    glBindVertexArray(arr[pass_link]);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_link]);

    glBindVertexArray(arr[pass_gene]);
    glUniform4f(i_transform[prog_gui], x_gene, y_gene, mul_x, -mul_y);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_gene]);

    glDisable(GL_SCISSOR_TEST);

    glUseProgram(prog[prog_panel]);  glUniform1i(i_panel, 0);
    glActiveTexture(GL_TEXTURE0);  glBindTexture(GL_TEXTURE_2D, tex_panel);

    glBindVertexArray(arr[pass_panel]);
    glUniform4f(i_transform[prog_panel], x_slot, 1, mul_x, -mul_y);  glUniform1f(i_size, cam.height);
    glDrawElements(GL_TRIANGLE_STRIP, count[vtx_panel], GL_UNSIGNED_BYTE, nullptr);
    void *scroll_offs = reinterpret_cast<void *>(count[vtx_panel]);

    int scroll_pos[] = {Gui::panel_width - Gui::slot_width, Gui::scroll_width};
    for(int i = 0; i < list_count; i++)
    {
        double scroll = scroll_gap;
        scroll /= std::max(list_height, int(sel.mapping[i].size() + 1) * Gui::line_spacing);

        double x = 1 - scroll_pos[i] * mul_x;
        double y = 1 - (Gui::header_height + sel.scroll[i] * scroll) * mul_y;
        glUniform4f(i_transform[prog_panel], x, y, mul_x, -mul_y);  glUniform1f(i_size, list_height * scroll);
        glDrawElements(GL_TRIANGLE_STRIP, count[idx_panel], GL_UNSIGNED_BYTE, scroll_offs);
    }

    glUseProgram(prog[prog_gui]);  glUniform1i(i_gui, 0);
    glActiveTexture(GL_TEXTURE0);  glBindTexture(GL_TEXTURE_2D, tex_gui);

    glBindVertexArray(arr[pass_header]);
    glUniform4f(i_transform[prog_gui], x_slot, 1, mul_x, -mul_y);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, count[vtx_quad], count[inst_header]);
}
