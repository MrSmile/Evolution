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
    constexpr uint32_t back_filler = 0xDD000000;

    constexpr int margin       =  4;
    constexpr int spacing      =  8;
    constexpr int digit_width  =  8;
    constexpr int icon_width   = 16;
    constexpr int line_height  = 16;
    constexpr int flag_pos     = 16;
    constexpr int flag_width   =  8;
    constexpr int flag_height  =  8;

    constexpr unsigned icon_offset  = 8;
    constexpr unsigned icon_row     = 4;
    constexpr unsigned flag_row     = 3;
    constexpr unsigned end_flag = 1 << 7;

    constexpr int line_spacing = line_height + 2 * margin;
    constexpr int slot_width = spacing + 3 * digit_width + icon_width;
    constexpr int base_offs = margin + 5 * digit_width + icon_width + slot_width;
    constexpr int panel_width = base_offs + icon_width + 5 * slot_width + margin;

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

    struct Position
    {
        int x, y;
    };

    enum LinkTypes
    {
        l_end_up = 1, l_end_dn = 2, l_end_mid = 4,
        l_beg_up = 8, l_beg_dn, l_br_up, l_br_dn, l_up, l_dn
    };

    const Position links[] =
    {
        {0x30, 0x70}, {0x50, 0x70}, {0x50, 0x50}, {0x40, 0x18},
        {0x50, 0x10}, {0x50, 0x40}, {0x50, 0x20}, {0x40, 0x50},
        {0x40, 0x30}, {0x40, 0x70}, {0x40, 0x40}, {0x40, 0x60},
        {0x60, 0x00}, {0x70, 0x00}
    };
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

void Camera::resize(int32_t w, int32_t h)
{
    width = w;  height = h;
}

void Camera::move(int32_t dx, int32_t dy)
{
    x -= std::lround(dx * scale);
    y -= std::lround(dy * scale);
}

void Camera::rescale(int32_t delta)
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
    GLushort tx, ty, width, height;

    GuiQuad(GLshort x, GLshort y, GLushort tx, GLushort ty, GLushort width, GLushort height) :
        x(x), y(y), tx(tx), ty(ty), width(width), height(height)
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
    ATTR(Vertex,       x,     2,       GL_FLOAT,          0),
    ATTR(FoodData,     x,     4,       GL_FLOAT,          f_instance),
};
const VertexAttribute layout_creature[] =
{
    ATTR(Vertex,       x,     2,       GL_FLOAT,          0),
    ATTR(CreatureData, x,     2,       GL_FLOAT,          f_instance),
    ATTR(CreatureData, rad,   3,       GL_FLOAT,          f_instance),
    ATTR(CreatureData, angle, 4,       GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_back[] =
{
    ATTR(Vertex,       x,     2,       GL_FLOAT,          0),
    ATTR(GuiBack,      pos,   2,       GL_SHORT,          f_instance),
    ATTR(GuiBack,      color, GL_BGRA, GL_UNSIGNED_BYTE,  f_instance | f_normalize),
};
const VertexAttribute layout_gui[] =
{
    ATTR(Vertex,       x,     2,       GL_FLOAT,          0),
    ATTR(GuiQuad,      x,     2,       GL_SHORT,          f_instance),
    ATTR(GuiQuad,      tx,    4,       GL_UNSIGNED_SHORT, f_instance),
};

#undef ATTR

#define INFO(name, base, inst, index) \
    {Representation::prog_##name, sizeof(layout_##name) / sizeof(VertexAttribute), \
        layout_##name, Representation::base, Representation::inst, Representation::index}

const Representation::PassInfo Representation::pass_info[] =
{
    INFO(food,     vtx_food,     inst_food,     idx_food),
    INFO(creature, vtx_creature, inst_creature, idx_creature),
    INFO(back,     vtx_quad,     inst_back,     buf_count),
    INFO(gui,      vtx_quad,     inst_gui,      buf_count),
    INFO(gui,      vtx_quad,     inst_level,    buf_count),
    INFO(gui,      vtx_quad,     inst_link,     buf_count),
};

#undef INFO


GLuint load_shader(GLint type, const char *name, const char *data, GLint size)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &data, &size);

    char msg[65536];  GLsizei len;
    glCompileShader(shader);  glGetShaderInfoLog(shader, sizeof(msg), &len, msg);
    if(len)std::printf("%s shader log:\n%s\n", name, msg);  return shader;
}

GLuint create_program(Shader::Index id)
{
    GLuint prog = glCreateProgram();  const ShaderDesc &shader = shaders[id];
    GLuint vert = load_shader(GL_VERTEX_SHADER, "Vertex", shader.vert_src, shader.vert_len);
    GLuint frag = load_shader(GL_FRAGMENT_SHADER, "Fragment", shader.frag_src, shader.frag_len);
    glAttachShader(prog, vert);  glAttachShader(prog, frag);

    char msg[65536];  GLsizei len;
    glLinkProgram(prog);  glGetProgramInfoLog(prog, sizeof(msg), &len, msg);
    if(len)std::printf("Shader program \"%s\" log:\n%s\n", shader.name, msg);

    glDetachShader(prog, vert);  glDetachShader(prog, frag);
    glDeleteShader(vert);  glDeleteShader(frag);  return prog;
}


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

void Representation::make_quad_shape()
{
    elem_count[pass_back] = elem_count[pass_gui] = elem_count[pass_level] = elem_count[pass_link] = 4;

    Vertex vertex[4] =
    {
        {0, 0}, {0, 1}, {1, 0}, {1, 1}
    };

    glBindBuffer(GL_ARRAY_BUFFER, buf[vtx_quad]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

    sel.fill_sel_bufs(buf[inst_back], buf[inst_gui], obj_count[pass_back], obj_count[pass_gui]);
    sel.fill_sel_levels(buf[inst_level], obj_count[pass_level]);
    sel.fill_sel_links(buf[inst_link], obj_count[pass_link]);
}

Representation::Representation(const World &world, SDL_Window *window) : world(world), cam(window), move(false)
{
    prog[prog_food] = create_program(Shader::food);
    i_transform[prog_food] = glGetUniformLocation(prog[prog_food], "transform");

    prog[prog_creature] = create_program(Shader::creature);
    i_transform[prog_creature] = glGetUniformLocation(prog[prog_creature], "transform");

    prog[prog_back] = create_program(Shader::back);
    i_transform[prog_back] = glGetUniformLocation(prog[prog_back], "transform");
    i_size = glGetUniformLocation(prog[prog_back], "size");

    prog[prog_gui] = create_program(Shader::gui);
    i_transform[prog_gui] = glGetUniformLocation(prog[prog_gui], "transform");
    i_gui = glGetUniformLocation(prog[prog_gui], "gui");


    glGenVertexArrays(pass_count, arr);
    glGenBuffers(buf_count, buf);
    glGenTextures(1, &tex_gui);

    for(int pass = 0; pass < pass_count; pass++)
    {
        glBindVertexArray(arr[pass]);  auto &info = pass_info[pass];
        register_attributes(info.attr, info.attr_count, buf[info.base], buf[info.inst]);
        if(info.index < buf_count)glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[info.index]);
    }
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, tex_gui);  const ImageDesc &image = images[Image::gui];
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    make_food_shape();
    make_creature_shape();
    make_quad_shape();
}

Representation::~Representation()
{
    for(int i = 0; i < prog_count; i++)glDeleteProgram(prog[i]);
    glDeleteVertexArrays(pass_count, arr);  glDeleteBuffers(buf_count, buf);
    glDeleteTextures(1, &tex_gui);
}


void Representation::resize(int32_t w, int32_t h)
{
    cam.resize(w, h);
}

bool Representation::mouse_wheel(int32_t delta)
{
    cam.rescale(delta);  return true;
}

bool Representation::mouse_down(int32_t x, int32_t y, uint8_t button)
{
    if(x >= cam.width - Gui::panel_width)
    {
        if(y < 0)return false;

        uint32_t pos = y / Gui::line_spacing;
        int slot = pos < sel.mapping.size() ? sel.mapping[pos] : -1;
        if(sel.slot == slot)return false;

        sel.slot = slot;
        sel.fill_sel_links(buf[inst_link], obj_count[pass_link]);
        return true;
    }

    switch(button)
    {
    case SDL_BUTTON_LEFT:  move = true;  return false;
    case SDL_BUTTON_RIGHT:  return select(x, y);
    default:  return false;
    }
}

bool Representation::mouse_move(int32_t dx, int32_t dy)
{
    if(!move)return false;  cam.move(dx, dy);  return true;
}

bool Representation::mouse_up(uint8_t button)
{
    if(button == SDL_BUTTON_LEFT)move = false;  return false;
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

void put_icon(std::vector<GuiQuad> &buf, int x, int y, unsigned index)
{
    index += Gui::icon_offset;
    unsigned tx = (index % Gui::icon_row) * Gui::icon_width;
    unsigned ty = (index / Gui::icon_row) * Gui::line_height;
    buf.emplace_back(x, y, tx, ty, Gui::icon_width, Gui::line_height);
}

void put_flags(std::vector<GuiQuad> &buf, int x, int y, unsigned flags, unsigned flag_count)
{
    unsigned flag = Gui::end_flag >> flag_count;
    unsigned rows = (flag_count - 1) / Gui::flag_row + 1;
    y += (Gui::line_height - rows * Gui::flag_height) / 2;
    for(unsigned i = 0; i < flag_count; i++)
    {
        unsigned ty = flags & (flag << i) ? Gui::flag_pos : Gui::flag_pos + Gui::flag_height;
        buf.emplace_back(x, y, i * Gui::flag_width, ty, Gui::flag_width, Gui::flag_height);
        x += Gui::flag_width;  if((i + 1) % Gui::flag_row)continue;
        x -= Gui::flag_row * Gui::flag_width;  y += Gui::flag_height;
    }
}

int put_link(std::vector<GuiQuad> &buf, int x, int y, size_t &prev, int type1, int type2, int line)
{
    int type = type1;
    if(prev != size_t(-1))
    {
        int yy = buf[prev].y + buf[prev].height;  type = type2;
        buf.emplace_back(x, yy, Gui::links[line].x, Gui::links[line].y, Gui::icon_width, y - yy);
    }
    prev = buf.size();  assert(type);
    buf.emplace_back(x, y, Gui::links[type].x, Gui::links[type].y, Gui::icon_width, Gui::line_height);
    return type;
}

struct LinkPainter
{
    std::vector<GuiQuad> &buf;
    int x, y;  uint32_t dst;

    size_t prev;  bool output;  int type;

    LinkPainter(std::vector<GuiQuad> &buf, int x, int y, int dst) :
        buf(buf), x(x), y(y), dst(dst), prev(-1), output(false)
    {
    }

    void put_weight(int xx, int yy, int32_t weight)
    {
        put_icon(buf, xx, yy, Gui::i_weight);  xx = write_number(buf, xx, yy, std::abs(weight)) - Gui::digit_width;
        buf.emplace_back(xx, yy, (weight < 0 ? 10 : 11) * Gui::digit_width, 0, Gui::digit_width, Gui::line_height);
    }

    void process(uint32_t src, int32_t weight)
    {
        if(!weight)return;  int yy = y + src * Gui::line_spacing;
        put_weight(x - Gui::slot_width - Gui::icon_width, yy, weight);

        if(src < dst)
        {
            type = put_link(buf, x, yy, prev, Gui::l_beg_up, Gui::l_br_up, Gui::l_up);
            return;
        }
        if(src == dst)
        {
            output = true;
            type = put_link(buf, x, yy, prev,
                Gui::l_end_dn | Gui::l_end_mid, Gui::l_end_up | Gui::l_end_dn | Gui::l_end_mid, Gui::l_up);
            return;
        }
        if(!output)
        {
            output = true;
            put_link(buf, x, y + dst * Gui::line_spacing, prev,
                Gui::l_end_dn, Gui::l_end_up | Gui::l_end_dn, Gui::l_up);
        }
        type = put_link(buf, x, yy, prev, 0, Gui::l_br_dn, Gui::l_dn);
    }

    void finalize(uint32_t last, int32_t level)
    {
        if(!output)
            type = put_link(buf, x, y + dst * Gui::line_spacing, prev,
                Gui::l_end_dn, Gui::l_end_up | Gui::l_end_dn, Gui::l_up);

        if(prev == size_t(-1))return;
        if(level)
        {
            int yy = y + last * Gui::line_spacing;
            put_weight(x - Gui::icon_width, yy, -level);
            put_link(buf, x, yy, prev, 0, Gui::l_beg_dn, Gui::l_dn);
        }
        else
        {
            buf[prev].tx = Gui::links[type - Gui::l_end_dn].x;
            buf[prev].ty = Gui::links[type - Gui::l_end_dn].y;
        }
    }
};

void Representation::Selection::fill_sel_bufs(GLuint buf_back, GLuint buf_gui, size_t &size_back, size_t &size_gui)
{
    if(id == uint64_t(-1))
    {
        GuiBack filler(0, -2, Gui::back_filler);
        glBindBuffer(GL_ARRAY_BUFFER, buf_back);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GuiBack), &filler, GL_DYNAMIC_DRAW);
        size_back = 1;

        glBindBuffer(GL_ARRAY_BUFFER, buf_gui);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size_gui = 0;

        mapping.clear();  return;
    }

    const auto &slots = proc.slots;
    std::vector<GuiBack> data_back;
    data_back.reserve(slots.size() + 1);

    std::vector<GuiQuad> data_gui;
    data_gui.reserve(26 * slots.size());

    mapping.clear();  int y = 0;
    refs.clear();  refs.resize(slots.size(), -1);
    for(size_t i = 0; i < slots.size(); i++)
    {
        if(skip_unused && !slots[i].used)continue;

        refs[i] = mapping.size();  mapping.push_back(i);
        data_back.emplace_back(y, i, slots[i].used ? Gui::back_used : Gui::back_unused);
        int x = Gui::base_offs - Gui::icon_width;  y += Gui::margin;

        switch(slots[i].neiro_state)
        {
        case GenomeProcessor::s_always_off:
            write_number(data_gui, x, y, 0);
            put_icon(data_gui, x, y, Gui::i_off);
            break;

        case GenomeProcessor::s_always_on:
            write_number(data_gui, x, y, 255);
            put_icon(data_gui, x, y, Gui::i_on);
            break;

        default:
            put_icon(data_gui, x, y, Gui::i_active);
        }
        x += Gui::icon_width + Gui::slot_width;

        write_number(data_gui, x, y, i);
        put_icon(data_gui, x, y, slots[i].type);
        const Gui::TypeIcons &icons = Gui::icons[slots[i].type];
        if(icons.base)
        {
            write_number(data_gui, x += Gui::slot_width, y, slots[i].base - 1);
            put_icon(data_gui, x, y, icons.base);
        }
        if(icons.angle1)
        {
            write_number(data_gui, x += Gui::slot_width, y, slots[i].angle1);
            put_icon(data_gui, x, y, icons.angle1);
        }
        if(icons.angle2)
        {
            write_number(data_gui, x += Gui::slot_width, y, slots[i].angle2);
            put_icon(data_gui, x, y, icons.angle2);
        }
        if(icons.radius)
        {
            write_number(data_gui, x += Gui::slot_width, y, slots[i].radius);
            put_icon(data_gui, x, y, icons.radius);
        }
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
    data.reserve(3 * mapping.size());

    const auto &slots = proc.slots;
    int x = Gui::base_offs - Gui::icon_width, y = Gui::margin;
    for(size_t i = 0; i < mapping.size(); i++, y += Gui::line_spacing)
    {
        if(slots[mapping[i]].neiro_state > GenomeProcessor::s_input)continue;
        uint32_t index = input_mapping[mapping[i]];  if(index == uint32_t(-1))continue;
        write_number(data, x, y, cr->input[index]);
    }

    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GuiQuad), data.data(), GL_STREAM_DRAW);
    size = data.size();
}

void Representation::Selection::fill_sel_links(GLuint buf, size_t &size)
{
    const auto &slots = proc.slots;
    const auto &links = proc.links;
    if(slot < 0 || slots[slot].neiro_state == GenomeProcessor::s_input)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size = 0;  return;
    }

    std::vector<GuiQuad> data;
    uint32_t beg = slots[slot].link_start;
    uint32_t end = beg + slots[slot].link_count;

    uint32_t cur = links[beg].source;
    int32_t weight = 0, level = slots[slot].act_level;
    LinkPainter painter(data, Gui::base_offs, Gui::margin, refs[slot]);
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
    painter.finalize(mapping.size(), level);

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
    sel.cr = hit_test(world, x0, y0, rad, sel.id);
    if(!sel.cr)
    {
        if(sel.id == uint64_t(-1))return false;

        sel.id = uint64_t(-1);  sel.slot = -1;
        sel.fill_sel_bufs(buf[inst_back], buf[inst_gui], obj_count[pass_back], obj_count[pass_gui]);
        sel.fill_sel_levels(buf[inst_level], obj_count[pass_level]);
        sel.fill_sel_links(buf[inst_link], obj_count[pass_link]);
        return true;
    }
    if(sel.cr->id == sel.id)return false;

    sel.id = sel.cr->id;  sel.pos = sel.cr->pos;  sel.slot = -1;
    sel.proc.process(world.config, sel.cr->genome);  Creature::calc_mapping(sel.proc, sel.input_mapping);
    sel.fill_sel_bufs(buf[inst_back], buf[inst_gui], obj_count[pass_back], obj_count[pass_gui]);
    sel.fill_sel_levels(buf[inst_level], obj_count[pass_level]);
    sel.fill_sel_links(buf[inst_link], obj_count[pass_link]);
    return true;
}

void Representation::update(SDL_Window *window, bool checksum)
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
    sel.cr = nullptr;
    for(const auto &tile : world.tiles)
    {
        for(const auto &food : tile.foods)if(food.type > Food::Sprout)
            (food_ptr++)->set(world.config, food);

        for(const Creature *cr = tile.first; cr; cr = cr->next)
        {
            if(cr->id == sel.id)sel.cr = cr;
            (creature_ptr++)->set(world.config, *cr);
        }
    }
    assert(food_ptr == food_end);
    assert(creature_ptr == creature_end);

    if(sel.cr)
    {
        sel.fill_sel_levels(buf[inst_level], obj_count[pass_level]);
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
    glDisable(GL_BLEND);

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

    Program cur = prog_count;
    for(int pass = 0; pass < pass_back; pass++)
    {
        if(cur != pass_info[pass].prog)glUseProgram(prog[cur = pass_info[pass].prog]);

        glBindVertexArray(arr[pass]);
        for(int i = 0; i <= ny; i++)for(int j = 0; j <= nx; j++)
        {
            glUniform4f(i_transform[cur], j * dx - x0, y0 - i * dy, mul_x * tile_size, -mul_y * tile_size);
            glDrawElementsInstanced(GL_TRIANGLES, elem_count[pass], GL_UNSIGNED_BYTE, nullptr, obj_count[pass]);
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mul_x = 2.0 / cam.width;  mul_y = 2.0 / cam.height;

    glUseProgram(prog[prog_back]);  glBindVertexArray(arr[pass_back]);
    glUniform4f(i_transform[prog_back], 1 - Gui::panel_width * mul_x, 1, mul_x, -mul_y);
    glUniform3f(i_size, Gui::panel_width, Gui::line_spacing, sel.slot);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, elem_count[pass_back], obj_count[pass_back]);

    glUseProgram(prog[prog_gui]);  glBindVertexArray(arr[pass_gui]);
    glUniform4f(i_transform[prog_gui], 1 - Gui::panel_width * mul_x, 1, mul_x, -mul_y);
    glUniform1i(i_gui, 0);  glActiveTexture(GL_TEXTURE0);  glBindTexture(GL_TEXTURE_2D, tex_gui);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, elem_count[pass_gui], obj_count[pass_gui]);

    glBindVertexArray(arr[pass_level]);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, elem_count[pass_level], obj_count[pass_level]);

    glBindVertexArray(arr[pass_link]);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, elem_count[pass_link], obj_count[pass_link]);
}
