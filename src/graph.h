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


    int32_t width, height;
    int32_t log_scale;
    uint64_t x, y;
    double scale;


    void update_scale();

    Camera(SDL_Window *window);
    void resize(int32_t w, int32_t h);
    void move(int32_t dx, int32_t dy);
    void rescale(int32_t delta);
};


constexpr double draw_scale = 1.0 / tile_size;

struct VertexAttribute;

class Representation
{
    enum Program
    {
        prog_food, prog_creature, prog_back, prog_gui, prog_count
    };

    enum Pass
    {
        pass_food, pass_creature, pass_back, pass_gui, pass_link, pass_count
    };

    enum Buffer
    {
        vtx_food, inst_food, idx_food,
        vtx_creature, inst_creature, idx_creature,
        vtx_quad, inst_back, inst_gui, inst_link,
        buf_count
    };

    struct PassInfo
    {
        Program prog;
        int attr_count;
        const VertexAttribute *attr;
        Buffer base, inst, index;
    };

    struct Selection
    {
        uint64_t id;
        const Creature *cr;
        GenomeProcessor proc;
        std::vector<uint32_t> mapping, refs;
        Position pos;  int slot;
        bool skip_unused;

        Selection() : id(-1), cr(nullptr), slot(-1), skip_unused(true)
        {
        }

        void fill_sel_bufs(GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui);
        void fill_sel_links(GLuint buf, size_t &size);
    };


    static const PassInfo pass_info[pass_count];

    const World &world;
    Camera cam;  bool move;
    GLuint prog[pass_count], tex_gui;
    GLint i_transform[prog_count], i_size, i_gui;
    GLuint arr[pass_count], buf[buf_count];
    size_t elem_count[pass_count];
    size_t obj_count[pass_count];
    Selection sel;


    void make_food_shape();
    void make_creature_shape();
    void make_quad_shape();

public:
    explicit Representation(const World &world, SDL_Window *window);
    ~Representation();

    void resize(int32_t w, int32_t h);
    bool mouse_wheel(int32_t delta);
    bool mouse_down(int32_t x, int32_t y, uint8_t button);
    bool mouse_move(int32_t dx, int32_t dy);
    bool mouse_up(uint8_t button);

    bool select(int32_t x, int32_t y);
    void update(SDL_Window *window, bool checksum);
    void draw();
};
