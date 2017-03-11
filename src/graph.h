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
    int log_scale;
    uint64_t x, y;
    double scale;


    void update_scale();

    Camera(SDL_Window *window);
    void resize(int w, int h);
    void move(int dx, int dy);
    void rescale(int delta);
};


constexpr double draw_scale = 1.0 / tile_size;

struct VertexAttribute;

class Representation
{
    enum Program
    {
        prog_food, prog_creature, prog_back, prog_gui, prog_panel, prog_count
    };

    enum Pass
    {
        pass_food, pass_creature, pass_slot_bg, pass_gene_bg,
        pass_slot, pass_level, pass_link, pass_gene, pass_panel,
        pass_count, pass_field_end = pass_slot_bg
    };

    enum Buffer
    {
        vtx_food, inst_food, idx_food,
        vtx_creature, inst_creature, idx_creature,
        vtx_quad, inst_slot_bg, inst_gene_bg,
        inst_slot, inst_level, inst_link, inst_gene,
        vtx_panel, idx_panel,
        buf_count
    };

    struct PassInfo
    {
        Program prog;
        int attr_count;
        const VertexAttribute *attr;
        Buffer base, inst, index;
    };

    enum HitTest
    {
        t_none, t_field, t_header, t_slots, t_genes, t_slot_scroll, t_gene_scroll,
        t_scroll = t_slot_scroll
    };

    enum List
    {
        l_slot, l_gene, list_count
    };

    struct Selection
    {
        uint64_t id;
        const Creature *cr;
        GenomeProcessor proc;
        std::vector<uint32_t> mapping[list_count];
        std::vector<uint32_t> input_mapping, refs;
        int slot, scroll[list_count];
        bool skip_unused;
        Position pos;

        Selection() : id(-1), cr(nullptr), skip_unused(false)
        {
        }

        void update_scroll(const Camera &cam, List list, int delta);
        void drag_scroll(const Camera &cam, List list, int delta);

        void fill_sel_genes(const Config &config,
            GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui);
        void fill_sel_slots(GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui);
        void fill_sel_levels(GLuint buf, size_t &size);
        void fill_sel_links(GLuint buf, size_t &size);
    };


    static const PassInfo pass_info[pass_count];

    const World &world;
    Camera cam;  HitTest move;
    GLuint prog[pass_count], tex_gui, tex_panel;
    GLint i_transform[prog_count], i_size, i_gui, i_panel;
    GLuint arr[pass_count], buf[buf_count];
    size_t elem_count[pass_count];
    size_t obj_count[pass_count];
    Selection sel;


    void fill_sel_bufs();
    void make_food_shape();
    void make_creature_shape();
    void make_quad_shape();
    void make_panel();

    HitTest hit_test(int &x, int &y);
    bool select_slot(List list, int y);

public:
    explicit Representation(const World &world, SDL_Window *window);
    ~Representation();

    void resize(int w, int h);
    bool mouse_wheel(int delta);
    bool mouse_down(int x, int y, uint8_t button);
    bool mouse_move(int dx, int dy);
    bool mouse_up(uint8_t button);

    bool select(int x, int y);
    void update(SDL_Window *window, bool checksum);
    void draw();
};
