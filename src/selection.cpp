// selection.cpp : selected creature related functions
//

#include "graph.h"
#include "video.h"



// Representation::Selection struct

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

void Representation::Selection::fill_sel_header(const Config &config, GLuint buf_gui, size_t &size_gui)
{
    std::vector<GuiQuad> data_gui;
    data_gui.reserve(54);

    int x1 = Gui::sel_icon_size + 2 * Gui::panel_border + Gui::bar_width / 2;
    int x2 = Gui::sel_all_offs_x + Gui::panel_border;
    int y1 = Gui::margin + Gui::panel_border;
    int y2 = y1 + Gui::line_spacing;

    if(id != uint64_t(-1))
    {
        uint64_t passive = 256 * proc.passive_cost.initial / config.capacity_mul;
        uint64_t energy = cr ? 256 * cr->energy / config.capacity_mul : -1;
        uint64_t max_energy = 256 * proc.max_energy / config.capacity_mul;
        uint32_t life = cr ? 256 * cr->total_life / config.life_mul : -1;
        uint32_t max_life = 256 * proc.max_life / config.life_mul;

        put_number_pair(data_gui, x1, y1, energy, max_energy);
        put_number_pair(data_gui, x1, y2, life, max_life);

        put_icon(data_gui, x2, y1, Slot::womb);
        write_number_right(data_gui, x2 + Gui::icon_width, y1, passive);
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

void Representation::Selection::fill_sel_limbs(GLuint buf_sector, GLuint buf_leg, size_t &size_sector, size_t &size_leg)
{
    if(!cr)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buf_sector);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size_sector = 0;

        glBindBuffer(GL_ARRAY_BUFFER, buf_leg);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        size_leg = 0;  return;
    }

    const auto &slots = proc.slots;
    std::vector<SectorData> data_sector;
    data_sector.reserve(slots.size());

    std::vector<LegData> data_leg;
    data_leg.reserve(slots.size());

    const double scale = sqrt(sqrt_scale) * draw_scale;
    for(size_t i = 0; i < slots.size(); i++)
    {
        if(skip_unused && !slots[i].used)continue;
        uint32_t alpha = (int(i) == slot ? 0x66000000 : 0x22000000);
        switch(slots[i].type)
        {
        case Slot::claw:
            data_sector.emplace_back(*cr, slots[i].angle1, slots[i].angle2,
                slots[i].radius * scale, alpha | 0xFFFF00, false);  break;

        case Slot::leg:
            {
                uint32_t index = input_mapping[i], color = 0xFF00FF;
                if(index != uint32_t(-1) && cr->input[index])
                {
                    alpha *= 2;  color = 0xFFFFFF;
                }
                data_leg.emplace_back(*cr, slots[i].angle1, slots[i].base + 1, alpha | color);  break;
            }

        case Slot::eye:
            data_sector.emplace_back(*cr, slots[i].angle1, slots[i].angle2,
                slots[i].radius * scale, alpha | 0x00FF00, false);  break;

        case Slot::radar:
            data_sector.emplace_back(*cr, slots[i].angle1, slots[i].angle2,
                1, alpha | 0x00FFFF, true);  break;

        default:
            continue;
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, buf_sector);
    glBufferData(GL_ARRAY_BUFFER, data_sector.size() * sizeof(SectorData), data_sector.data(), GL_DYNAMIC_DRAW);
    size_sector = data_sector.size();

    glBindBuffer(GL_ARRAY_BUFFER, buf_leg);
    glBufferData(GL_ARRAY_BUFFER, data_leg.size() * sizeof(LegData), data_leg.data(), GL_DYNAMIC_DRAW);
    size_leg = data_leg.size();
}
