// video.h : GUI metrics and GPU structs
//

#pragma once

#include "world.h"
#include <SDL2/SDL_opengl.h>
#include <cmath>


namespace Gui
{
    constexpr uint32_t back_used   = 0xDD000000;
    constexpr uint32_t back_unused = 0xDD330000;
    constexpr uint32_t back_header = 0xDD000033;
    constexpr uint32_t back_filler = 0xDD000000;

    constexpr int panel_border  =   8;
    constexpr int panel_stretch =   8;
    constexpr int sel_icon_size =  48;
    constexpr int sel_all_width =  72;
    constexpr int bar_width     = 256;
    constexpr int header_height =  64;
    constexpr int scroll_width  =  16;

    constexpr int margin        =   4;
    constexpr int spacing       =   8;
    constexpr int digit_width   =   8;
    constexpr int icon_width    =  16;
    constexpr int line_height   =  16;
    constexpr int flag_pos      =  16;
    constexpr int flag_width    =   8;
    constexpr int flag_height   =   8;

    constexpr unsigned icon_offset  = 8;
    constexpr unsigned icon_row     = 4;
    constexpr unsigned flag_row     = 3;

    constexpr int line_spacing = line_height + 2 * margin;
    constexpr int item_width = spacing + 3 * digit_width + icon_width;
    constexpr int base_offs = margin + 5 * digit_width + icon_width + item_width;
    constexpr int slot_width = base_offs + icon_width + 5 * item_width + margin;
    constexpr int gene_width = 5 * item_width + 2 * margin - spacing;
    constexpr int gene_header = (gene_width + 3 * digit_width) / 2;
    constexpr int gene_offs = margin + 3 * digit_width;

    constexpr int panel_width = slot_width + gene_width + 2 * scroll_width;
    constexpr int control_height = line_height + 2 * panel_border;
    constexpr int sel_all_offs_x = sel_icon_size + bar_width + 4 * panel_border;
    constexpr int sel_all_offs_y = margin + line_spacing;


    enum Icon
    {
        i_weight = Slot::invalid + 1, i_target, i_volume,
        i_angle, i_radius, i_damage, i_life, i_speed,
        i_off, i_active, i_on, i_none = 0
    };

    struct TypeIcons
    {
        Icon base, angle1, angle2, radius;
        uint8_t flag_count;
    };

    const TypeIcons icons[Slot::invalid + 1] =
    {
        {i_none,   i_none,  i_none,  i_none,   0},  // neiron
        {i_none,   i_none,  i_none,  i_none,   0},  // mouth
        {i_volume, i_none,  i_none,  i_none,   0},  // stomach
        {i_volume, i_none,  i_none,  i_none,   0},  // womb
        {i_none,   i_angle, i_angle, i_radius, 6},  // eye
        {i_none,   i_angle, i_angle, i_none,   6},  // radar
        {i_damage, i_angle, i_angle, i_radius, 0},  // claw
        {i_life,   i_none,  i_none,  i_none,   0},  // hide
        {i_speed,  i_angle, i_none,  i_none,   0},  // leg
        {i_none,   i_none,  i_angle, i_none,   0},  // rotator
        {i_none,   i_none,  i_none,  i_none,   3},  // signal
        {i_none,   i_none,  i_none,  i_none,   0},  // invalid
    };

    constexpr int link_spacing = 32;
    constexpr int link_pos_x = 64;
    constexpr int link_pos_y = (link_spacing - line_spacing) / 2;
    constexpr unsigned link_row = 4;

    enum LinkTypes
    {
        l_up = 1, l_beg_up, l_br_up,
        l_dn = 5, l_beg_dn, l_br_dn,
        l_end_dn = 9, l_end_up = 10, l_end_mid = 12
    };

    constexpr int mark_pos_x =      64;
    constexpr int mark_pos_y =      16;
    constexpr int slash_pos_x =     80;
    constexpr int slash_pos_y =     16;

    constexpr int sel_icon_pos_x =   0;
    constexpr int sel_icon_pos_y =  64;
    constexpr int sel_all_pos_x  =  56;
    constexpr int sel_all_pos_y  =  64;
    constexpr int sel_bar1_pos_x =  48;
    constexpr int sel_bar1_pos_y =  96;
    constexpr int sel_bar2_pos_x =  88;
    constexpr int sel_bar2_pos_y =  96;
    constexpr int scroll_pos_x   = 104;
    constexpr int scroll_pos_y   =   8;
}


constexpr double draw_scale = 1.0 / tile_size;
constexpr double speed_scale = 1.0 / 512;


struct Vertex
{
    GLfloat x, y;
};

struct Triangle
{
    GLubyte p0, p1, p2;

    Triangle() = default;

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
        type = food.type - Food::grass;
    }
};

struct CreatureData
{
    GLfloat x, y, rad[3];
    GLubyte angle, signal, energy, life;

    void set(const Config &config, const Creature &cr)
    {
        GLfloat energy_mul = config.base_r2 * draw_scale * draw_scale / (config.capacity_mul << config.base_bits);
        GLfloat life_mul = energy_mul * config.hide_mul / config.life_mul;

        x = cr.pos.x * draw_scale;
        y = cr.pos.y * draw_scale;

        GLfloat sqr = cr.passive_cost.initial * energy_mul - cr.max_life * life_mul;
        rad[0] = std::sqrt(sqr);  sqr += cr.max_energy * energy_mul;
        rad[1] = std::sqrt(sqr);  sqr += cr.max_life * life_mul;
        rad[2] = std::sqrt(sqr);

        angle = cr.angle;  signal = cr.flags;
        energy = std::lround(255.0 * cr.energy / cr.max_energy);
        life = std::lround(255.0 * cr.total_life / cr.max_life);
    }
};

struct SectorData
{
    GLfloat x, y, rad;
    GLubyte angle, delta;
    uint32_t color1, color2;

    SectorData(const Creature &cr, angle_t angle1, angle_t angle2, GLfloat radius, uint32_t color, bool fade) :
        x(cr.pos.x * draw_scale), y(cr.pos.y * draw_scale), rad(radius),
        angle(angle_t(cr.angle + angle1)), delta(angle_t(angle2 - angle1 - 1)),
        color1(color), color2(fade ? color & 0xFFFFFF : color)
    {
    }

    void set(const Config &config, const Creature &cr, const Creature::Claw &claw)
    {
        x = cr.pos.x * draw_scale;
        y = cr.pos.y * draw_scale;
        rad = sqrt(claw.rad_sqr) * draw_scale;
        angle = angle_t(cr.angle + claw.angle);  delta = claw.delta;
        color1 = color2 = 0x22FF0000;
    }
};

struct LegData
{
    GLfloat x, y, speed;
    GLubyte angle;
    uint32_t color;

    LegData(const Creature &cr, angle_t angle, uint32_t speed, uint32_t color) :
        x(cr.pos.x * draw_scale), y(cr.pos.y * draw_scale), speed(speed * speed_scale),
        angle(angle_t(cr.angle + angle)), color(color)
    {
    }
};

struct GuiBack
{
    GLshort pos, slot;
    uint32_t color;

    GuiBack(int pos, int slot, uint32_t color) : pos(pos), slot(slot), color(color)
    {
    }
};

struct GuiQuad
{
    GLshort x, y;
    GLubyte tx, ty, width, height;

    GuiQuad(GLshort x, GLshort y, GLubyte tx, GLubyte ty, GLubyte width, GLubyte height) :
        x(x), y(y), tx(tx), ty(ty), width(width), height(height)
    {
    }
};

struct PanelVertex
{
    GLshort x, y;
    GLushort stretch;
    GLubyte tx, ty;

    PanelVertex() = default;

    PanelVertex(GLshort x, GLshort y, GLubyte tx, GLubyte ty) : x(x), y(y), stretch(0), tx(tx), ty(ty)
    {
    }
};
