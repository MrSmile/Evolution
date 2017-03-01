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

struct Triangle
{
    GLubyte p0, p1, p2;

    Triangle()
    {
    }

    Triangle(int p0, int p1, int p2) : p0(p0), p1(p1), p2(p2)
    {
    }
};

struct FoodData
{
    GLfloat x, y, rad, type;

    void set(const Config &config, const Food &food)
    {
        x = food.pos.x * draw_scale;
        y = food.pos.y * draw_scale;
        rad = config.base_radius * draw_scale;
        type = food.type - Food::Grass;
    }
};

struct CreatureData
{
    GLfloat x, y, rad[3];
    GLubyte angle, signal, energy, life;

    void set(const Config &config, const Creature &cr)
    {
        GLfloat energy_mul = config.base_r2 * draw_scale * draw_scale / (1 << 16);
        GLfloat life_mul = energy_mul * config.hide_mul / config.life_mul;

        x = cr.pos.x * draw_scale;
        y = cr.pos.y * draw_scale;

        GLfloat sqr = cr.passive_energy * energy_mul - cr.max_life * life_mul;
        rad[0] = sqrt(sqr);  sqr += cr.max_energy * energy_mul;
        rad[1] = sqrt(sqr);  sqr += cr.max_life * life_mul;
        rad[2] = sqrt(sqr);

        angle = cr.angle;  signal = cr.flags;
        energy = std::lround(255.0 * cr.energy / cr.max_energy);
        life = std::lround(255.0 * cr.total_life / cr.max_life);
    }
};


GLuint load_shader(GLint type, const char *name, const unsigned char *data, GLint size)
{
    GLuint shader = glCreateShader(type);  char msg[65536];  GLsizei len;
    glShaderSource(shader, 1, reinterpret_cast<const GLchar **>(&data), &size);
    glCompileShader(shader);  glGetShaderInfoLog(shader, sizeof(msg), &len, msg);
    if(len)std::printf("%s shader log:\n%s\n", name, msg);  return shader;
}

void Representation::create_program(Pass pass, const char *name,
    const unsigned char *vert_src, unsigned vert_len, const unsigned char *frag_src, unsigned frag_len)
{
    prog[pass] = glCreateProgram();
    GLuint vert = load_shader(GL_VERTEX_SHADER, "Vertex", vert_src, vert_len);
    GLuint frag = load_shader(GL_FRAGMENT_SHADER, "Fragment", frag_src, frag_len);
    glAttachShader(prog[pass], vert);  glAttachShader(prog[pass], frag);

    char msg[65536];  GLsizei len;
    glLinkProgram(prog[pass]);  glGetProgramInfoLog(prog[pass], sizeof(msg), &len, msg);
    if(len)std::printf("Shader program \"%s\" log:\n%s\n", name, msg);

    glDetachShader(prog[pass], vert);  glDetachShader(prog[pass], frag);
    glDeleteShader(vert);  glDeleteShader(frag);

    i_transform[pass] = glGetUniformLocation(prog[pass], "transform");
}

#define SHADER(src) #src, shaders_##src##_vert, shaders_##src##_vert_len, shaders_##src##_frag, shaders_##src##_frag_len


void Representation::make_food_shape()
{
    constexpr int n = 3, m = 2 * n - 2;
    elem_count[pass_food] = 3 * m + 3;
    obj_count[pass_food] = 0;

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
    elem_count[pass_creature] = 3 * n;
    obj_count[pass_creature] = 0;

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


void register_attribute(GLuint index, int vec_size, GLenum type, GLboolean norm, size_t stride, size_t offs)
{
    glEnableVertexAttribArray(index);  if(index)glVertexAttribDivisor(index, 1);
    glVertexAttribPointer(index, vec_size, type, norm, stride, reinterpret_cast<void *>(offs));
}

Representation::Representation()
{
    create_program(pass_food, SHADER(food));
    create_program(pass_creature, SHADER(creature));

    glGenVertexArrays(pass_count, arr);
    glGenBuffers(buf_count, buf);

    glBindVertexArray(arr[pass_food]);
    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_food]);
    register_attribute(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);
    register_attribute(1, 4, GL_FLOAT, GL_FALSE, sizeof(FoodData), 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_food]);

    glBindVertexArray(arr[pass_creature]);
    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_creature]);
    register_attribute(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);
    register_attribute(1, 2, GL_FLOAT, GL_FALSE, sizeof(CreatureData), 0);
    register_attribute(2, 3, GL_FLOAT, GL_FALSE, sizeof(CreatureData), 2 * sizeof(GLfloat));
    register_attribute(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CreatureData), 5 * sizeof(GLfloat));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[idx_creature]);

    glBindVertexArray(0);

    make_food_shape();
    make_creature_shape();
}

Representation::~Representation()
{
    for(int pass = 0; pass < pass_count; pass++)glDeleteProgram(prog[pass]);
    glDeleteVertexArrays(pass_count, arr);  glDeleteBuffers(buf_count, buf);
}


void Representation::update(const World &world)
{
    obj_count[pass_food] = world.total_food_count;
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);  FoodData *food_ptr = nullptr;
    glBufferData(GL_ARRAY_BUFFER, obj_count[pass_food] * sizeof(FoodData), nullptr, GL_STREAM_DRAW);
    if(obj_count[pass_food])food_ptr = static_cast<FoodData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    obj_count[pass_creature] = world.total_creature_count;
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);  CreatureData *creature_ptr = nullptr;
    glBufferData(GL_ARRAY_BUFFER, obj_count[pass_creature] * sizeof(CreatureData), nullptr, GL_STREAM_DRAW);
    if(obj_count[pass_creature])creature_ptr = static_cast<CreatureData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

#ifndef NDEBUG
    FoodData *food_end = food_ptr + obj_count[pass_food];
    CreatureData *creature_end = creature_ptr + obj_count[pass_creature];
#endif
    for(const auto &tile : world.tiles)
    {
        for(const auto &food : tile.foods)if(food.type > Food::Sprout)
            (food_ptr++)->set(world.config, food);

        for(const Creature *cr = tile.first; cr; cr = cr->next)
            (creature_ptr++)->set(world.config, *cr);
    }
    assert(food_ptr == food_end);
    assert(creature_ptr == creature_end);

    if(food_ptr)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    if(creature_ptr)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
}

void Representation::draw(const World &world, const Camera &cam)
{
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

    for(int pass = 0; pass < pass_count; pass++)
    {
        glBindVertexArray(arr[pass]);  glUseProgram(prog[pass]);
        for(int i = 0; i <= ny; i++)for(int j = 0; j <= nx; j++)
        {
            glUniform4f(i_transform[pass], j * dx - x0, y0 - i * dy, mul_x * tile_size, -mul_y * tile_size);
            glDrawElementsInstanced(GL_TRIANGLES, elem_count[pass], GL_UNSIGNED_BYTE, nullptr, obj_count[pass]);
        }
    }
}
