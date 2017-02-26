// graph.h : world rendering
//

#pragma once

#include "world.h"
#include <epoxy/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>



struct Camera
{
    static constexpr double scale_step = 1.0 / 4;
    static constexpr int min_scale = -32 * 4;
    static constexpr int max_scale = +32 * 4;


    int width, height;
    uint64_t x, y;
    int log_scale;
    double scale;


    void update_scale();

    Camera(SDL_Window *window);
    void apply();

    void resize(int w, int h);
    void move(int dx, int dy);
    void rescale(int delta);
};


constexpr double draw_scale = 1.0 / tile_size;

class Representation
{
    enum Pass
    {
        pass_food, pass_creature, pass_count
    };

    enum BufferType
    {
        vtx_food, inst_food, idx_food,
        vtx_creature, inst_creature, idx_creature,
        buf_count
    };


    GLuint prog[pass_count];
    GLint i_transform[pass_count];
    GLuint arr[pass_count], buf[buf_count];
    size_t elem_count[pass_count];
    size_t obj_count[pass_count];


    void create_program(Pass pass, const char *name,
        const unsigned char *vert_src, unsigned vert_len,
        const unsigned char *frag_src, unsigned frag_len);

    void make_food_shape();
    void make_creature_shape();

public:
    Representation();
    ~Representation();

    void update(const World &world);
    void draw(const World &world, const Camera &cam);
};
