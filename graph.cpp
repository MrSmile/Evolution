// graph.cpp : world rendering
//

#include "graph.h"
#include "shaders.h"
#include <algorithm>
#include <cassert>
#include <cmath>



// Camera struct

void Camera::update_scale()
{
    scale = std::exp2(tile_order - scale_step * log_scale);
}

Camera::Camera(SDL_Window *window) : x(0), y(0), log_scale(8 / scale_step)
{
    SDL_GetWindowSize(window, &width, &height);  update_scale();
}

void Camera::apply()
{
    glViewport(0, 0, width, height);
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

struct Vertex
{
    GLfloat x, y;
};

struct FoodData
{
    GLfloat x, y, rad, type;

    FoodData(const Config &config, const Food &food) :
        x(food.pos.x * draw_scale), y(food.pos.y * draw_scale),
        rad(config.base_radius * draw_scale), type(food.type - Food::Grass)
    {
    }
};

GLuint load_shader(GLint type, const char *name, const unsigned char *data, GLint size)
{
    GLuint shader = glCreateShader(type);  char msg[65536];  GLsizei len;
    glShaderSource(shader, 1, reinterpret_cast<const GLchar **>(&data), &size);
    glCompileShader(shader);  glGetShaderInfoLog(shader, sizeof(msg), &len, msg);
    if(len)std::printf("%s shader log:\n%s\n", name, msg);  return shader;
}

GLuint create_program()
{
    GLuint prog = glCreateProgram();
    GLuint vert = load_shader(GL_VERTEX_SHADER, "Vertex", shaders_food_vert, shaders_food_vert_len);
    GLuint frag = load_shader(GL_FRAGMENT_SHADER, "Fragment", shaders_food_frag, shaders_food_frag_len);
    glAttachShader(prog, vert);  glAttachShader(prog, frag);

    char msg[65536];  GLsizei len;
    glLinkProgram(prog);  glGetProgramInfoLog(prog, sizeof(msg), &len, msg);
    if(len)std::printf("Shader program log:\n%s\n", msg);

    glDetachShader(prog, vert);  glDetachShader(prog, frag);
    glDeleteShader(vert);  glDeleteShader(frag);  return prog;
}

Representation::Representation()
{
    glGenVertexArrays(1, &vao);  glGenBuffers(3, vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[2]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);  glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FoodData), nullptr);
    glVertexAttribDivisor(1, 1);  glBindVertexArray(0);

    prog = create_program();
    i_transform = glGetUniformLocation(prog, "transform");


    constexpr int n = 3, m = 6 * n - 3;
    count[0] = m;  count[1] = 0;

    Vertex data[2 * n];
    for(int i = 0; i < 2 * n; i++)
    {
        double r = i & 1 ? 1.0 : 0.25;
        data[i].x = r * std::sin(i * (pi / n));
        data[i].y = r * std::cos(i * (pi / n));
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);

    GLshort index[m], *ptr = index, end = 2 * n - 2;
    for(int i = 0; i < end; i += 2)
    {
        *ptr++ = i;    *ptr++ = i + 1;  *ptr++ = i + 2;
        *ptr++ = end;  *ptr++ = i;      *ptr++ = i + 2;
    }
    *ptr++ = end;  *ptr++ = end + 1;  *ptr++ = 0;
    assert(ptr == index + m);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index), index, GL_STATIC_DRAW);
}

Representation::~Representation()
{
    glDeleteProgram(prog);  glDeleteVertexArrays(1, &vao);  glDeleteBuffers(3, vbo);
}

void Representation::update(const World &world)
{
    std::vector<FoodData> buf;
    buf.reserve(count[1] = world.total_food_count);
    for(const auto &tile : world.tiles)for(const auto &food : tile.foods)
        if(food.type > Food::Sprout)buf.emplace_back(world.config, food);
    assert(buf.size() == count[1]);

    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, count[1] * sizeof(FoodData), buf.data(), GL_STREAM_DRAW);
}

void Representation::draw(const World &world, const Camera &cam)
{
    glBindVertexArray(vao);  glUseProgram(prog);

    double mul_x = 2 / (cam.width  * cam.scale);
    double mul_y = 2 / (cam.height * cam.scale);
    uint64_t x = cam.x & world.config.full_mask_x;
    uint64_t y = cam.y & world.config.full_mask_y;
    int order_x = world.config.order_x + tile_order;
    int order_y = world.config.order_y + tile_order;
    int nx = (x + cam.width  * cam.scale) * std::exp2(-order_x);
    int ny = (y + cam.height * cam.scale) * std::exp2(-order_y);
    double dx = mul_x * std::exp2(order_x), x0 = x * mul_x + 1;
    double dy = mul_y * std::exp2(order_y), y0 = y * mul_y + 1;
    for(int i = 0; i <= ny; i++)for(int j = 0; j <= nx; j++)
    {
        glUniform4f(i_transform, j * dx - x0, y0 - i * dy, mul_x * tile_size, -mul_y * tile_size);
        glDrawElementsInstanced(GL_TRIANGLES, count[0], GL_UNSIGNED_SHORT, nullptr, count[1]);
    }
}
