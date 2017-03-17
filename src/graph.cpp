// graph.cpp : world rendering
//

#include "graph.h"
#include "stream.h"
#include <algorithm>
#include <cassert>
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

#define INFO(name, base, inst, index) \
    {Representation::prog_##name, sizeof(layout_##name) / sizeof(VertexAttribute), \
        layout_##name, Representation::base, Representation::inst, Representation::index}

const Representation::PassInfo Representation::pass_info[] =
{
    INFO(food,     vtx_food,     inst_food,     idx_food),
    INFO(creature, vtx_creature, inst_creature, idx_creature),
    INFO(back,     vtx_quad,     inst_slot_bg,  buf_count),
    INFO(back,     vtx_quad,     inst_gene_bg,  buf_count),
    INFO(gui,      vtx_quad,     inst_slot,     buf_count),
    INFO(gui,      vtx_quad,     inst_level,    buf_count),
    INFO(gui,      vtx_quad,     inst_link,     buf_count),
    INFO(gui,      vtx_quad,     inst_gene,     buf_count),
    INFO(gui,      vtx_quad,     inst_header,   buf_count),
    INFO(panel,    vtx_panel,    buf_count,     idx_panel),
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
    sel.fill_sel_header(buf[inst_header], count[inst_header]);
    sel.fill_sel_slots(buf[inst_slot_bg], buf[inst_slot], count[inst_slot_bg], count[inst_slot]);
    sel.fill_sel_levels(buf[inst_level], count[inst_level]);
    sel.fill_sel_links(buf[inst_link], count[inst_link]);
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

Representation::Representation(const World &world, SDL_Window *window) : world(world), cam(window), move(t_none)
{
    prog[prog_food] = create_program("food", VertShader::food, FragShader::color);
    i_transform[prog_food] = glGetUniformLocation(prog[prog_food], "transform");

    prog[prog_creature] = create_program("creature", VertShader::creature, FragShader::creature);
    i_transform[prog_creature] = glGetUniformLocation(prog[prog_creature], "transform");

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
    make_quad_shape();
    make_panel();
}

Representation::~Representation()
{
    for(int i = 0; i < prog_count; i++)glDeleteProgram(prog[i]);
    glDeleteVertexArrays(pass_count, arr);  glDeleteBuffers(buf_count, buf);
    glDeleteTextures(1, &tex_gui);  glDeleteTextures(1, &tex_panel);
}


void Representation::Selection::set_scroll(const Camera &cam, List list, int pos)
{
    int list_height = cam.height - Gui::header_height;
    int max_scroll = int(mapping[list].size() + 1) * Gui::line_spacing - list_height;
    scroll[list] = std::max(0, std::min(max_scroll, pos));
}

void Representation::Selection::drag_scroll(const Camera &cam, List list, int base, int offs)
{
    int gap = cam.height - Gui::header_height - 2 * Gui::panel_border;  if(gap <= 0)return;
    double scale = int(mapping[list].size() + 1) * Gui::line_spacing / double(gap);
    set_scroll(cam, list, base + std::lround(offs * scale));
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
        sel.fill_sel_header(buf[inst_header], count[inst_header]);
        sel.fill_sel_slots(buf[inst_slot_bg], buf[inst_slot], count[inst_slot_bg], count[inst_slot]);
        sel.fill_sel_levels(buf[inst_level], count[inst_level]);
        sel.fill_sel_links(buf[inst_link], count[inst_link]);
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


int write_number(std::vector<GuiQuad> &buf, int x, int y, uint32_t num)
{
    do
    {
        x -= Gui::digit_width;
        unsigned tx = (num % 10) * Gui::digit_width;  num /= 10;
        buf.emplace_back(x, y, tx, 0, Gui::digit_width, Gui::line_height);
    }
    while(num);  return x;
}

void write_number_right(std::vector<GuiQuad> &buf, int x, int y, uint32_t num)
{
    size_t pos = buf.size();
    int offs = x - write_number(buf, 0, y, num);
    for(; pos < buf.size(); pos++)buf[pos].x += offs;
}

void put_number_pair(std::vector<GuiQuad> &buf, int x, int y, uint32_t num1, uint32_t num2)
{
    x -= Gui::icon_width / 2;
    if(num1 != uint32_t(-1))write_number(buf, x, y, num1);
    else buf.emplace_back(x - Gui::digit_width, y, 10 * Gui::digit_width, 0, Gui::digit_width, Gui::line_height);
    buf.emplace_back(x, y, Gui::slash_pos_x, Gui::slash_pos_y, Gui::icon_width, Gui::line_height);
    write_number_right(buf, x + Gui::icon_width, y, num2);
}

void put_icon(std::vector<GuiQuad> &buf, int x, int y, unsigned index)
{
    index += Gui::icon_offset;
    unsigned tx = (index % Gui::icon_row) * Gui::icon_width;
    unsigned ty = (index / Gui::icon_row) * Gui::line_height;
    buf.emplace_back(x, y, tx, ty, Gui::icon_width, Gui::line_height);
}

void put_item(std::vector<GuiQuad> &buf, int x, int y, unsigned index, uint32_t num)
{
    put_icon(buf, x, y, index);  write_number(buf, x, y, num);
}

void put_weight(std::vector<GuiQuad> &buf, int x, int y, int32_t weight)
{
    put_icon(buf, x, y, Gui::i_weight);  x = write_number(buf, x, y, std::abs(weight)) - Gui::digit_width;
    buf.emplace_back(x, y, (weight < 0 ? 10 : 11) * Gui::digit_width, 0, Gui::digit_width, Gui::line_height);
}

void put_flags(std::vector<GuiQuad> &buf, int x, int y, unsigned flags, unsigned flag_count)
{
    unsigned rows = (flag_count - 1) / Gui::flag_row + 1;
    y += (Gui::line_height - rows * Gui::flag_height) / 2;
    for(unsigned i = 0; i < flag_count; i++)
    {
        unsigned ty = flags & (1 << i) ? Gui::flag_pos : Gui::flag_pos + Gui::flag_height;
        buf.emplace_back(x, y, i * Gui::flag_width, ty, Gui::flag_width, Gui::flag_height);
        x += Gui::flag_width;  if((i + 1) % Gui::flag_row)continue;
        x -= Gui::flag_row * Gui::flag_width;  y += Gui::flag_height;
    }
}

void put_link(std::vector<GuiQuad> &buf, int x, int y, int &y_prev, unsigned type1, unsigned type2, unsigned line)
{
    unsigned type = type1;
    if(y_prev >= 0)
    {
        type = type2;
        int tx = Gui::link_pos_x + (line / Gui::link_row) * Gui::icon_width;
        int ty = Gui::link_pos_y + (line % Gui::link_row) * Gui::link_spacing;
        for(int pos = y_prev + Gui::line_spacing; pos < y; pos += Gui::line_spacing)
            buf.emplace_back(x, pos, tx, ty, Gui::icon_width, Gui::line_spacing);
    }
    int tx = Gui::link_pos_x + (type / Gui::link_row) * Gui::icon_width;
    int ty = Gui::link_pos_y + (type % Gui::link_row) * Gui::link_spacing;
    buf.emplace_back(x, y, tx, ty, Gui::icon_width, Gui::line_spacing);
    y_prev = y;
}

struct LinkPainter
{
    std::vector<GuiQuad> &buf;
    int x, y_dst, y_prev;

    LinkPainter(std::vector<GuiQuad> &buf, int x, int dst) :
        buf(buf), x(x), y_dst(dst * Gui::line_spacing), y_prev(-1)
    {
    }

    void process(uint32_t src, int32_t weight)
    {
        if(!weight)return;  int y = src * Gui::line_spacing;
        put_weight(buf, x - Gui::item_width - Gui::icon_width, y + Gui::margin, weight);
        if(y < y_dst)
        {
            put_link(buf, x, y, y_prev, Gui::l_beg_up, Gui::l_br_up, Gui::l_up);  return;
        }
        if(y == y_dst)
        {
            put_link(buf, x, y, y_prev, Gui::l_end_dn | Gui::l_end_mid,
                Gui::l_end_up | Gui::l_end_dn | Gui::l_end_mid, Gui::l_up);  return;
        }
        if(y_prev < y_dst)put_link(buf, x, y_dst, y_prev, Gui::l_end_dn, Gui::l_end_up | Gui::l_end_dn, Gui::l_up);
        put_link(buf, x, y, y_prev, Gui::l_br_dn, Gui::l_br_dn, Gui::l_dn);
    }

    void finalize(uint32_t last, int32_t level)
    {
        if(y_prev < y_dst)put_link(buf, x, y_dst, y_prev, Gui::l_end_dn, Gui::l_end_up | Gui::l_end_dn, Gui::l_up);
        if(!level)
        {
            buf[buf.size() - 1].ty -= Gui::link_spacing;  return;
        }
        int y = last * Gui::line_spacing;
        put_weight(buf, x - Gui::icon_width, y + Gui::margin, -level);
        put_link(buf, x, y, y_prev, Gui::l_beg_dn, Gui::l_beg_dn, Gui::l_dn);
    }
};

void Representation::Selection::fill_sel_genes(const Config &config,
    GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui)
{
    mapping[l_gene].clear();
    if(id == uint64_t(-1))
    {
        GuiBack filler(0, -2, Gui::back_filler);
        glBindBuffer(GL_ARRAY_BUFFER, buf_back);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GuiBack), &filler, GL_DYNAMIC_DRAW);
        size_back = 1;

        glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size_gui = 0;  return;
    }

    const auto &chromosomes = cr->genome.chromosomes;
    const auto &genes = cr->genome.genes;

    std::vector<GuiBack> data_back;
    data_back.reserve(chromosomes.size() + genes.size() + 1);

    std::vector<GuiQuad> data_gui;
    data_gui.reserve(3 * chromosomes.size() + 22 * genes.size());

    uint32_t index = -1, n = 0;  int y = 0;
    for(size_t i = 0; i < genes.size(); i++, n--)
    {
        if(!n)
        {
            do index++;
            while(!chromosomes[index]);
            n = chromosomes[index];

            data_back.emplace_back(y, proc.slots.size(), Gui::back_header);
            write_number(data_gui, Gui::gene_header, y + Gui::margin, index);
            mapping[l_gene].push_back(-1);  y += Gui::line_spacing;
        }
        Genome::Gene gene = genes[i];
        uint32_t slot = gene.take_bits(config.slot_bits);
        uint32_t type = std::min<uint32_t>(Slot::invalid, gene.take_bits(slot_type_bits));
        data_back.emplace_back(y, slot, Gui::back_used);  mapping[l_gene].push_back(slot);
        int x = Gui::gene_offs;  y += Gui::margin;
        put_item(data_gui, x, y, type, slot);

        if(!type)  // link
        {
            uint32_t source = gene.take_bits(config.slot_bits);
            int32_t weight = gene.take_bits_signed(config.base_bits);
            uint32_t offset = gene.take_bits(8);

            put_item(data_gui, x += Gui::item_width, y, Gui::i_target, source);
            put_weight(data_gui, x += Gui::item_width + Gui::digit_width, y, weight);
            put_item(data_gui, x += Gui::item_width, y, Gui::i_active, offset);
            y += Gui::line_spacing - Gui::margin;  continue;
        }

        uint32_t base = gene.take_bits(config.base_bits);
        uint32_t angle1 = gene.take_bits(angle_bits);
        uint32_t angle2 = gene.take_bits(angle_bits);
        uint32_t radius = gene.take_bits(radius_bits);
        uint32_t flags = gene.take_bits(flag_bits);

        const Gui::TypeIcons &icons = Gui::icons[type];
        if(icons.base)
            put_item(data_gui, x += Gui::item_width, y, icons.base, base);
        if(icons.angle1)
            put_item(data_gui, x += Gui::item_width, y, icons.angle1, angle1);
        if(icons.angle2)
            put_item(data_gui, x += Gui::item_width, y, icons.angle2, angle2);
        if(icons.radius)
            put_item(data_gui, x += Gui::item_width, y, icons.radius, radius);
        if(icons.flag_count)
        {
            x += Gui::icon_width + Gui::flag_width;
            put_flags(data_gui, x, y, flags, icons.flag_count);
        }
        y += Gui::line_spacing - Gui::margin;
    }
    data_back.emplace_back(y, -2, Gui::back_filler);

    glBindBuffer(GL_ARRAY_BUFFER, buf_back);
    glBufferData(GL_ARRAY_BUFFER, data_back.size() * sizeof(GuiQuad), data_back.data(), GL_DYNAMIC_DRAW);
    size_back = data_back.size();

    glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
    glBufferData(GL_ARRAY_BUFFER, data_gui.size() * sizeof(GuiQuad), data_gui.data(), GL_DYNAMIC_DRAW);
    size_gui = data_gui.size();
}

void Representation::Selection::fill_sel_header(GLuint buf_gui, size_t &size_gui)
{
    std::vector<GuiQuad> data_gui;
    data_gui.reserve(54);

    int x1 = Gui::sel_icon_size + 2 * Gui::panel_border + Gui::bar_width / 2;
    int x2 = Gui::sel_all_offs_x + Gui::panel_border;
    int y1 = Gui::margin + Gui::panel_border;
    int y2 = y1 + Gui::line_spacing;

    if(id != uint64_t(-1))
    {
        put_number_pair(data_gui, x1, y1, cr ? cr->energy : -1, proc.max_energy);
        put_number_pair(data_gui, x1, y2, cr ? cr->total_life : -1, proc.max_life);

        put_icon(data_gui, x2, y1, Slot::womb);
        write_number_right(data_gui, x2 + Gui::icon_width, y1, proc.passive_cost.initial);
    }

    if(!skip_unused)
        data_gui.emplace_back(x2, y2, Gui::mark_pos_x, Gui::mark_pos_y, Gui::icon_width, Gui::line_height);

    glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
    glBufferData(GL_ARRAY_BUFFER, data_gui.size() * sizeof(GuiQuad), data_gui.data(), GL_DYNAMIC_DRAW);
    size_gui = data_gui.size();
}

void Representation::Selection::fill_sel_slots(GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui)
{
    mapping[l_slot].clear();
    if(id == uint64_t(-1))
    {
        GuiBack filler(0, -2, Gui::back_filler);
        glBindBuffer(GL_ARRAY_BUFFER, buf_back);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GuiBack), &filler, GL_DYNAMIC_DRAW);
        size_back = 1;

        glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size_gui = 0;  return;
    }

    const auto &slots = proc.slots;
    std::vector<GuiBack> data_back;
    data_back.reserve(slots.size() + 1);

    std::vector<GuiQuad> data_gui;
    data_gui.reserve(26 * slots.size());

    refs.clear();  refs.resize(slots.size(), -1);  int y = 0;
    for(size_t i = 0; i < slots.size(); i++)
    {
        if(skip_unused && !slots[i].used)continue;

        refs[i] = mapping[l_slot].size();  mapping[l_slot].push_back(i);
        data_back.emplace_back(y, i, slots[i].used ? Gui::back_used : Gui::back_unused);
        int x = Gui::base_offs - Gui::icon_width;  y += Gui::margin;

        switch(slots[i].neiro_state)
        {
        case GenomeProcessor::s_always_off:
            put_item(data_gui, x, y, Gui::i_off, 0);  break;
        case GenomeProcessor::s_always_on:
            put_item(data_gui, x, y, Gui::i_on, 255);  break;
        default:
            put_icon(data_gui, x, y, Gui::i_active);
        }
        x += Gui::icon_width + Gui::item_width;
        put_item(data_gui, x, y, slots[i].type, i);

        const Gui::TypeIcons &icons = Gui::icons[slots[i].type];
        if(icons.base)
            put_item(data_gui, x += Gui::item_width, y, icons.base, slots[i].base - 1);
        if(icons.angle1)
            put_item(data_gui, x += Gui::item_width, y, icons.angle1, slots[i].angle1);
        if(icons.angle2)
            put_item(data_gui, x += Gui::item_width, y, icons.angle2, slots[i].angle2);
        if(icons.radius)
            put_item(data_gui, x += Gui::item_width, y, icons.radius, slots[i].radius);
        if(icons.flag_count)
        {
            x += Gui::icon_width + Gui::flag_width;
            put_flags(data_gui, x, y, slots[i].flags, icons.flag_count);
        }
        y += Gui::line_spacing - Gui::margin;
    }
    data_back.emplace_back(y, -2, Gui::back_filler);

    glBindBuffer(GL_ARRAY_BUFFER, buf_back);
    glBufferData(GL_ARRAY_BUFFER, data_back.size() * sizeof(GuiQuad), data_back.data(), GL_DYNAMIC_DRAW);
    size_back = data_back.size();

    glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
    glBufferData(GL_ARRAY_BUFFER, data_gui.size() * sizeof(GuiQuad), data_gui.data(), GL_DYNAMIC_DRAW);
    size_gui = data_gui.size();
}

void Representation::Selection::fill_sel_levels(GLuint buf, size_t &size)
{
    std::vector<GuiQuad> data;
    data.reserve(3 * mapping[l_slot].size());

    const auto &slots = proc.slots;
    int x = Gui::base_offs - Gui::icon_width, y = Gui::margin;
    if(cr)for(size_t i = 0; i < mapping[l_slot].size(); i++, y += Gui::line_spacing)
    {
        uint32_t slot = mapping[l_slot][i];
        if(slots[slot].neiro_state > GenomeProcessor::s_input)continue;
        uint32_t index = input_mapping[slot];  if(index == uint32_t(-1))continue;
        write_number(data, x, y, cr->input[index]);
    }

    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GuiQuad), data.data(), GL_STREAM_DRAW);
    size = data.size();
}

void Representation::Selection::fill_sel_links(GLuint buf, size_t &size)
{
    const auto &slots = proc.slots;  const auto &links = proc.links;
    if(slot < 0 || slots[slot].neiro_state == GenomeProcessor::s_input ||
        !slots[slot].link_count || refs[slot] == uint32_t(-1))
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size = 0;  return;
    }

    std::vector<GuiQuad> data;
    data.reserve(5 * slots[slot].link_count + mapping[l_slot].size() + 8);
    uint32_t beg = slots[slot].link_start, end = beg + slots[slot].link_count;

    uint32_t cur = links[beg].source;
    int32_t weight = 0, level = slots[slot].act_level;
    LinkPainter painter(data, Gui::base_offs, refs[slot]);
    for(uint32_t j = beg; j < end; j++)
    {
        if(links[j].source != cur)
        {
            uint32_t src = refs[cur];
            if(src != uint32_t(-1))painter.process(src, weight);
            else if(slots[cur].neiro_state == GenomeProcessor::s_always_on)level -= 255 * weight;
            cur = links[j].source;
        }
        weight += links[j].weight;
    }
    uint32_t src = refs[cur];
    if(src != uint32_t(-1))painter.process(src, weight);
    else if(slots[cur].neiro_state == GenomeProcessor::s_always_on)level -= 255 * weight;
    painter.finalize(mapping[l_slot].size(), level);

    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GuiQuad), data.data(), GL_DYNAMIC_DRAW);
    size = data.size();
}

bool hit_test(const World::Tile &tile,
    uint64_t x0, uint64_t y0, uint64_t max_r2, const Creature *&sel, uint64_t prev_id)
{
    for(const Creature *cr = tile.first; cr; cr = cr->next)
    {
        int32_t dx = cr->pos.x - x0;
        int32_t dy = cr->pos.y - y0;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(r2 >= max_r2)continue;

        if(cr->id == prev_id && sel)return true;  sel = cr;
    }
    return false;
}

const Creature *hit_test(const World &world, uint64_t x0, uint64_t y0, uint32_t rad, uint64_t prev_id)
{
    uint32_t x1 = (x0 - rad) >> tile_order & world.config.mask_x;
    uint32_t y1 = (y0 - rad) >> tile_order & world.config.mask_y;
    uint32_t x2 = (x0 + rad) >> tile_order & world.config.mask_x;
    uint32_t y2 = (y0 + rad) >> tile_order & world.config.mask_y;

    uint64_t r2 = uint64_t(rad) * rad;
    bool test[] = {true, x1 != x2, y1 != y2, x1 != x2 && y1 != y2};  const Creature *sel = nullptr;
    if(test[0] && hit_test(world.tiles[x1 | (y1 << world.config.order_x)], x0, y0, r2, sel, prev_id))return sel;
    if(test[1] && hit_test(world.tiles[x2 | (y1 << world.config.order_x)], x0, y0, r2, sel, prev_id))return sel;
    if(test[2] && hit_test(world.tiles[x1 | (y2 << world.config.order_x)], x0, y0, r2, sel, prev_id))return sel;
    if(test[3] && hit_test(world.tiles[x2 | (y2 << world.config.order_x)], x0, y0, r2, sel, prev_id))return sel;
    return sel;
}

bool Representation::select(int32_t x, int32_t y)
{
    constexpr int click_zone = 8;

    uint64_t x0 = cam.x + std::lround(x * cam.scale);
    uint64_t y0 = cam.y + std::lround(y * cam.scale);
    uint32_t rad = std::min<long>(tile_size, world.config.base_radius + std::lround(click_zone * cam.scale));
    sel.cr = ::hit_test(world, x0, y0, rad, sel.id);
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

void Representation::update(SDL_Window *window, bool checksum)
{
    count[inst_food] = world.total_food_count;
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_food]);  FoodData *food_ptr = nullptr;
    glBufferData(GL_ARRAY_BUFFER, count[inst_food] * sizeof(FoodData), nullptr, GL_STREAM_DRAW);
    if(count[inst_food])food_ptr = static_cast<FoodData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    count[inst_creature] = world.total_creature_count;
    glBindBuffer(GL_ARRAY_BUFFER, buf[inst_creature]);  CreatureData *creature_ptr = nullptr;
    glBufferData(GL_ARRAY_BUFFER, count[inst_creature] * sizeof(CreatureData), nullptr, GL_STREAM_DRAW);
    if(count[inst_creature])creature_ptr = static_cast<CreatureData *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

#ifndef NDEBUG
    FoodData *food_end = food_ptr + count[inst_food];
    CreatureData *creature_end = creature_ptr + count[inst_creature];
#endif
    sel.cr = nullptr;
    for(const auto &tile : world.tiles)
    {
        for(const auto &food : tile.foods)if(food.type > Food::sprout)
            (food_ptr++)->set(world.config, food);

        for(const Creature *cr = tile.first; cr; cr = cr->next)
        {
            if(cr->id == sel.id)sel.cr = cr;
            (creature_ptr++)->set(world.config, *cr);
        }
    }
    assert(food_ptr == food_end);
    assert(creature_ptr == creature_end);

    sel.fill_sel_header(buf[inst_header], count[inst_header]);
    sel.fill_sel_levels(buf[inst_level], count[inst_level]);

    if(sel.cr)
    {
        cam.x += sel.cr->pos.x - sel.pos.x;
        cam.y += sel.cr->pos.y - sel.pos.y;
        sel.pos = sel.cr->pos;
    }

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

    if(checksum || !(world.current_time % 1000))
    {
        OutStream stream;  stream.initialize();
        stream << world;  stream.finalize();
        print_checksum(world, stream);
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
        "Evolution - Time: %llu, Food: %lu, Creature: %lu", (unsigned long long)world.current_time,
        (unsigned long)world.total_food_count, (unsigned long)world.total_creature_count);
    SDL_SetWindowTitle(window, buf);
}


void Representation::draw()
{
    glViewport(0, 0, cam.width, cam.height);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_PRIMITIVE_RESTART);
    glPrimitiveRestartIndex(255);
    glDisable(GL_BLEND);

    double mul_x = 2 / (cam.width  * cam.scale);
    double mul_y = 2 / (cam.height * cam.scale);
    double scale_x = tile_size * mul_x, dx = (world.config.mask_x + 1) * scale_x;
    double scale_y = tile_size * mul_y, dy = (world.config.mask_y + 1) * scale_y;
    double x_end = 1 + scale_x, x_beg = x_end + ((cam.x - tile_size) & world.config.full_mask_x) * mul_x;
    double y_end = 1 + scale_y, y_beg = y_end + ((cam.y - tile_size) & world.config.full_mask_y) * mul_y;

    Program cur = prog_count;
    for(int pass = 0; pass < pass_field_end; pass++)
    {
        const PassInfo &info = pass_info[pass];
        if(cur != info.prog)glUseProgram(prog[cur = info.prog]);

        glBindVertexArray(arr[pass]);
        for(double y = -y_beg; y < y_end; y += dy)for(double x = -x_beg; x < x_end; x += dx)
        {
            glUniform4f(i_transform[cur], x, -y, scale_x, -scale_y);
            glDrawElementsInstanced(GL_TRIANGLES, count[info.index], GL_UNSIGNED_BYTE, nullptr, count[info.inst]);
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
