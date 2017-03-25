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


struct VertexAttribute;

class Representation
{
    enum Program
    {
        prog_food, prog_creature, prog_sector, prog_leg, prog_sel, prog_back, prog_gui, prog_panel, prog_count
    };

    enum Pass
    {
        pass_sector, pass_food, pass_creature, pass_leg, pass_sel,
        pass_slot_bg, pass_gene_bg, pass_slot, pass_level, pass_link, pass_gene, pass_header, pass_panel,
        pass_count
    };

    enum Buffer
    {
        vtx_food, idx_food, inst_food, vtx_creature, idx_creature, inst_creature,
        vtx_sector, idx_sector, inst_sector, vtx_leg, idx_leg, inst_leg, vtx_sel, idx_sel,
        vtx_quad, inst_slot_bg, inst_gene_bg, inst_slot, inst_level, inst_link, inst_gene, inst_header,
        vtx_panel, idx_panel,
        buf_count
    };

    struct PassInfo
    {
        Program prog;
        int attr_count;
        const VertexAttribute *attr;
        Buffer base, inst, index;
        bool alpha_blending;
    };

    enum HitTest
    {
        t_none, t_field, t_show_all, t_slots, t_genes, t_slot_scroll, t_gene_scroll,
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

        Selection() : id(-1), cr(nullptr), skip_unused(true)
        {
        }

        void set_scroll(const Camera &cam, List list, int pos);
        void drag_scroll(const Camera &cam, List list, int base, int offs);

        void fill_sel_genes(const Config &config,
            GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui);
        void fill_sel_header(GLuint buf_gui, size_t &size_gui);
        void fill_sel_slots(GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui);
        void fill_sel_levels(GLuint buf, size_t &size);
        void fill_sel_links(GLuint buf, size_t &size);
        void fill_sel_limbs(GLuint buf_sector, GLuint buf_leg, size_t &size_sector, size_t &size_leg);
    };


    static const PassInfo pass_info[pass_count];

    World &world;
    Camera cam;  HitTest move;
    GLuint prog[pass_count], tex_gui, tex_panel;
    GLint i_transform[prog_count], i_sel, i_size, i_gui, i_panel;
    GLuint arr[pass_count], buf[buf_count];
    size_t count[buf_count];
    int scroll_base, mouse_start;
    Selection sel;


    void fill_sel_bufs();
    void make_food_shape();
    void make_creature_shape();
    void make_sector_shape();
    void make_leg_shape();
    void make_sel_shape();
    void make_quad_shape();
    void make_panel();

    HitTest hit_test(int &x, int &y);
    bool select_slot(List list, int y);

public:
    explicit Representation(World &world, SDL_Window *window);
    ~Representation();

    void resize(int w, int h);
    bool mouse_wheel(const SDL_MouseWheelEvent &evt);
    bool mouse_down(const SDL_MouseButtonEvent &evt);
    bool mouse_move(const SDL_MouseMotionEvent &evt);
    bool mouse_up(const SDL_MouseButtonEvent &evt);

    bool select(int x, int y);
    void update_title(SDL_Window *window, bool checksum);
    void update();
    void draw();
};
