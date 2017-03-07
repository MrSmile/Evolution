// graph.h : world rendering
//

#pragma once

#include "world.h"
#include "resource.h"
#include <epoxy/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>



void print_checksum(const World &world, const OutStream &stream);



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
    void apply() const;

    void resize(int w, int h);
    void move(int dx, int dy);
    void rescale(int delta);
};


constexpr double draw_scale = 1.0 / tile_size;

class Representation
{
    enum Pass
    {
        pass_food, pass_creature, pass_gui, pass_count
    };

    enum BufferType
    {
        vtx_food, inst_food, idx_food,
        vtx_creature, inst_creature, idx_creature,
        vtx_gui, inst_gui,
        buf_count
    };

    struct Selection
    {
        uint64_t id;
        const Creature *cr;
        GenomeProcessor proc;
        std::vector<uint32_t> mapping;
        Position pos;

        Selection() : id(-1), cr(nullptr)
        {
        }

        size_t make_creature(const World &world, bool skipUnused);
    };


    GLuint prog[pass_count], tex_gui;
    GLint i_transform[pass_count], i_gui;
    GLuint arr[pass_count], buf[buf_count];
    size_t elem_count[pass_count];
    size_t obj_count[pass_count];
    Selection sel;


    void create_program(Pass pass, Shader::Index id);

    void make_food_shape();
    void make_creature_shape();
    void make_quad_shape();

public:
    Representation();
    ~Representation();

    void select(const World &world, Camera &cam, int32_t x, int32_t y);
    void update(SDL_Window *window, const World &world, Camera &cam, bool checksum);
    void draw(const World &world, const Camera &cam);
};
