// world.cpp : world mechanics implementation
//

#include "world.h"
#include "stream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <cmath>



// Config struct

bool Config::calc_derived()
{
    constexpr uint32_t max_cost = 1ul << 16;
    constexpr uint32_t max_value = 1ul << 24;

    if(!order_x || order_x >= 16)return false;
    if(!order_y || order_y >= 16)return false;
    if(base_radius > tile_size)return false;

    if(!chromosome_bits || chromosome_bits > 16)return false;
    if(slot_bits > 8 || base_bits > 16)return false;
    if(genome_split_factor > 0xFFFFFF00)return false;  // TODO: handle overflows
    if(chromosome_replace_factor > 0xFFFFFF00)return false;
    if(bit_mutate_factor > 0xFFFFFF00)return false;

    for(const auto &c : cost)
        if(c.initial > max_cost || c.per_tick > max_cost)return false;
    if(gene_init_cost > max_cost || gene_pass_rate > max_cost)return false;
    if(eating_cost > max_cost || signal_cost > max_cost)return false;

    uint32_t limit = max_value >> base_bits;
    if(spawn_mul > limit || capacity_mul > limit || hide_mul > limit)return false;
    if(damage_mul > limit || life_mul > limit || life_regen > 65536)return false;
    if(speed_mul > (uint32_t(-1) >> base_bits))return false;
    if(rotate_mul > (uint32_t(-1) >> 7))return false;
    if(mass_order >= 64)return false;

    if(food_energy > max_value)return false;
    if(repression_range > tile_size)return false;
    if(sprout_dist_x4 <= 4 * repression_range)return false;

    mask_x = (uint32_t(1) << order_x) - 1;
    mask_y = (uint32_t(1) << order_y) - 1;
    full_mask_x = (uint64_t(1) << (order_x + tile_order)) - 1;
    full_mask_y = (uint64_t(1) << (order_x + tile_order)) - 1;;
    base_r2 = uint64_t(base_radius) * base_radius;
    repression_r2 = uint64_t(repression_range) * repression_range;
    return true;
}

bool Config::load(InStream &stream)
{
    stream.assert_align(4);
    stream >> order_x >> order_y >> align(4) >> base_radius;

    stream >> chromosome_bits >> slot_bits >> base_bits >> align(4);
    stream >> genome_split_factor >> chromosome_replace_factor;
    stream >> chromosome_copy_prob >> bit_mutate_factor;

    for(auto &c : cost)stream >> c.initial >> c.per_tick;
    stream >> gene_init_cost >> gene_pass_rate;
    stream >> eating_cost >> signal_cost;

    stream >> spawn_mul >> capacity_mul >> hide_mul;
    stream >> damage_mul >> life_mul >> life_regen;
    stream >> speed_mul >> rotate_mul >> mass_order >> align(4);

    stream >> food_energy >> exp_sprout_per_tile >> exp_sprout_per_grass;
    stream >> repression_range >> sprout_dist_x4 >> meat_dist_x4;

    return stream && calc_derived();
}

void Config::save(OutStream &stream) const
{
    stream.assert_align(4);
    stream << order_x << order_y << align(4) << base_radius;

    stream << chromosome_bits << slot_bits << base_bits << align(4);
    stream << genome_split_factor << chromosome_replace_factor;
    stream << chromosome_copy_prob << bit_mutate_factor;

    for(auto &c : cost)stream << c.initial << c.per_tick;
    stream << gene_init_cost << gene_pass_rate;
    stream << eating_cost << signal_cost;

    stream << spawn_mul << capacity_mul << hide_mul;
    stream << damage_mul << life_mul << life_regen;
    stream << speed_mul << rotate_mul << mass_order << align(4);

    stream << food_energy << exp_sprout_per_tile << exp_sprout_per_grass;
    stream << repression_range << sprout_dist_x4 << meat_dist_x4;
}



// Detector struct

Detector::Detector(uint64_t r2)
{
    reset(r2);
}

void Detector::reset(uint64_t r2)
{
    min_r2 = r2;  id = 0;  target = nullptr;
}

void Detector::update(uint64_t r2, Creature *cr)
{
    if(r2 > min_r2)return;
    if(r2 == min_r2 && cr->id > id)return;
    min_r2 = r2;  id = cr->id;  target = cr;
}



// Food struct

Food::Food(const Config &config, Type type, const Position &pos) : type(type), pos(pos), eater(config.base_r2)
{
}

Food::Food(const Config &config, const Food &food) : Food(config, food.type > Sprout ? food.type : Grass, food.pos)
{
}


void Food::check_grass(const Config &config, const Food *food, size_t n)
{
    if(type != Sprout)return;

    for(size_t i = 0; i < n; i++)if(food[i].type == Grass)
    {
        int32_t dx = pos.x - food[i].pos.x;
        int32_t dy = pos.y - food[i].pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(r2 >= config.repression_r2)continue;
        type = Dead;  return;
    }
}


bool Food::load(const Config &config, InStream &stream, uint64_t offs_x, uint64_t offs_y)
{
    eater.reset(config.base_r2);

    uint32_t x, y;  stream >> x >> y;  if(!stream)return false;

    type = Type(x >> tile_order);
    pos.x = x & tile_mask | offs_x;  pos.y = y | offs_y;
    return type > Dead && type <= Meat && !(y >> tile_order);
}

void Food::save(OutStream &stream) const
{
    uint32_t x = pos.x & tile_mask;
    uint32_t y = pos.y & tile_mask;
    x |= uint32_t(type) << tile_order;
    stream << x << y;
}



// Genome struct

Genome::Gene::Gene(const Config &config, uint32_t slot, Slot::Type type,
    uint32_t base, angle_t angle1, angle_t angle2, uint32_t radius, uint8_t flags)
{
    int shift = 64;  data = 0;
    shift -= config.slot_bits;  data |= uint64_t(slot)   << shift;
    shift -= slot_type_bits;    data |= uint64_t(type)   << shift;
    shift -= config.base_bits;  data |= uint64_t(base)   << shift;
    shift -= angle_bits;        data |= uint64_t(angle1) << shift;
    shift -= angle_bits;        data |= uint64_t(angle2) << shift;
    shift -= radius_bits;       data |= uint64_t(radius) << shift;
    shift -= flag_bits;         data |= uint64_t(flags)  << shift;
    assert(shift >= 0);
}

Genome::Gene::Gene(const Config &config, uint32_t slot, int32_t weight, uint32_t source, uint8_t offset)
{
    Slot::Type type = Slot::Link;
    uint32_t base = weight & ((1 << config.base_bits) - 1);

    int shift = 64;  data = 0;
    shift -= config.slot_bits;  data |= uint64_t(slot)   << shift;
    shift -= slot_type_bits;    data |= uint64_t(type)   << shift;
    shift -= config.base_bits;  data |= uint64_t(base)   << shift;
    shift -= config.slot_bits;  data |= uint64_t(source) << shift;
    shift -= 8;                 data |= uint64_t(offset) << shift;
    assert(shift >= 0);
}

Genome::Genome(const Config &config)
{
    genes.emplace_back(config, 0, Slot::Mouth,     0,   0,   0,   0, 0);
    genes.emplace_back(config, 1, Slot::Stomach, 255,   0,   0,   0, 0);
    genes.emplace_back(config, 2, Slot::Womb,     63,   0,   0,   0, 0);
    genes.emplace_back(config, 3, Slot::Eye,       0, -12,  -4,  64, Creature::f_grass);
    genes.emplace_back(config, 4, Slot::Eye,       0,  -4,   4,  64, Creature::f_grass);
    genes.emplace_back(config, 5, Slot::Eye,       0,   4,  12,  64, Creature::f_grass);
    genes.emplace_back(config, 6, Slot::Leg,     255,   0,   0,   0, 0);
    genes.emplace_back(config, 7, Slot::Rotator,   0,   0,  -8,   0, 0);
    genes.emplace_back(config, 8, Slot::Rotator,   0,   0,   8,   0, 0);

    genes.emplace_back(config, 0,  -64, 9, 255);
    genes.emplace_back(config, 2,   64, 1, 250);
    genes.emplace_back(config, 6,   64, 4,   0);
    genes.emplace_back(config, 7,    1, 3,   0);
    genes.emplace_back(config, 7, -128, 4,   0);
    genes.emplace_back(config, 8, -128, 3,   0);
    genes.emplace_back(config, 8, -128, 4,   0);
    genes.emplace_back(config, 8,   -1, 9,   1);

    chromosomes.resize(size_t(1) << config.chromosome_bits);
    chromosomes[0] = genes.size();
}


struct GeneSequence
{
    const Genome::Gene *start;
    size_t count;  uint32_t next;

    GeneSequence(const Genome::Gene *start, size_t count) : start(start), count(count), next(-1)
    {
    }
};

Genome::Genome(const Config &config, Random &rand, const Genome &parent, const Genome *father)
{
    uint32_t chromosome_count = uint32_t(1) << config.chromosome_bits;
    assert(parent.chromosomes.size() == chromosome_count);

    // stage 1: clone or take one of every pair from parents

    uint32_t n = 10;  // TODO: config?
    std::vector<GeneSequence> seqs;
    seqs.reserve(chromosome_count + n);
    if(father)
    {
        assert(father->chromosomes.size() == chromosome_count);
        std::vector<uint8_t> pairs(std::max<uint32_t>(1, chromosome_count >> 5));
        for(auto &pair : pairs)pair = rand.uint32();

        const Gene *pos_m = parent.genes.data();
        const Gene *pos_f = father->genes.data();
        for(uint32_t i = 0; i < chromosome_count; i += 2)
        {
            if(pairs[i >> 5] & (1 << (i & 31)))
                seqs.emplace_back(pos_m + parent.chromosomes[i], parent.chromosomes[i + 1]);
            else seqs.emplace_back(pos_m, parent.chromosomes[i]);
            pos_m += parent.chromosomes[i] + parent.chromosomes[i + 1];

            if(pairs[i >> 5] & (2 << (i & 31)))
                seqs.emplace_back(pos_f + father->chromosomes[i], father->chromosomes[i + 1]);
            else seqs.emplace_back(pos_f, father->chromosomes[i]);
            pos_f += father->chromosomes[i] + father->chromosomes[i + 1];
        }
        assert(pos_m == parent.genes.data() + parent.genes.size());
        assert(pos_f == father->genes.data() + father->genes.size());
    }
    else
    {
        const Gene *pos = parent.genes.data();
        for(uint32_t i = 0; i < chromosome_count; i++)
        {
            seqs.emplace_back(pos, parent.chromosomes[i]);  pos += parent.chromosomes[i];
        }
        assert(pos == parent.genes.data() + parent.genes.size());
    }

    // stage 2: split chromosomes

    uint32_t len = rand.geometric(config.genome_split_factor);
    for(uint32_t i = 0; i < chromosome_count; i++)
    {
        uint32_t pos = i;
        while(seqs[pos].count > len)
        {
            uint32_t last = seqs.size();
            seqs.emplace_back(seqs[pos].start + len, seqs[pos].count - len);
            seqs[pos].count = len;

            pos = chromosome_count + rand.uniform(last - chromosome_count + 1);
            len = rand.geometric(config.genome_split_factor);
            std::swap(seqs[pos], seqs[last]);
        }
        len -= seqs[pos].count;
    }

    std::vector<uint32_t> last(chromosome_count);
    for(uint32_t i = 0; i < chromosome_count; i++)last[i] = i;
    for(uint32_t i = chromosome_count; i < seqs.size(); i++)
    {
        uint32_t prev = rand.uint32() & (chromosome_count - 1);
        seqs[last[prev]].next = i;  last[prev] = i;
    }

    // stage 3: delete or duplicate whole chromosomes

    uint32_t pos = rand.geometric(config.chromosome_replace_factor);
    while(pos < chromosome_count)
    {
        uint32_t index = rand.uint32();
        if(index > config.chromosome_copy_prob)
        {
            seqs[pos].count = 0;  seqs[pos].next = -1;
        }
        else seqs[pos] = seqs[index & (chromosome_count - 1)];

        pos += rand.geometric(config.chromosome_replace_factor) + 1;
    }

    // consolidate genome

    uint32_t total_size = 0;
    chromosomes.resize(chromosome_count);
    for(uint32_t i = 0; i < chromosome_count; i++)
    {
        uint32_t size = 0;  uint32_t pos = i;
        do
        {
            size += seqs[pos].count;  pos = seqs[pos].next;
        }
        while(pos != uint32_t(-1));

        total_size += chromosomes[i] = size;
    }
    genes.reserve(total_size);
    for(uint32_t i = 0; i < chromosome_count; i++)
    {
        uint32_t pos = i;
        do
        {
            const Gene *gene = seqs[pos].start;
            const Gene *end = gene + seqs[pos].count;
            while(gene < end)genes.push_back(*gene++);
            pos = seqs[pos].next;
        }
        while(pos != uint32_t(-1));
    }
    assert(genes.size() == total_size);

    // stage 4: mutate individual bits

    pos = rand.geometric(config.bit_mutate_factor);
    while(pos < 64 * total_size)
    {
        genes[pos >> 6].data ^= 1 << (pos & 63);
        pos += rand.geometric(config.bit_mutate_factor) + 1;
    }
}


bool Genome::load(const Config &config, InStream &stream)
{
    constexpr uint32_t max_genes = 1ul << 24;

    uint32_t gene_count = 0;
    chromosomes.resize(uint32_t(1) << config.chromosome_bits);
    for(auto &chromosome : chromosomes)
    {
        stream >> chromosome;
        if(!stream || chromosome > max_genes - gene_count)return false;
        gene_count += chromosome;
    }
    genes.resize(gene_count);  stream >> align(8);
    for(auto &gene : genes)stream >> gene.data;
    return stream;
}

void Genome::save(OutStream &stream) const
{
    stream.assert_align(8);
    for(auto &chromosome : chromosomes)stream << chromosome;  stream << align(8);
    for(auto &gene : genes)stream << gene.data;
}



// GenomeProcessor struct

void GenomeProcessor::reset()
{
    link_start = links.size();
    link_count = core_count = 0;
    act_level = min_level = max_level = 0;
    type_and = -1;  type_or = 0;

    base = 0;  radius = 0;
    angle1_x = angle1_y = 0;
    angle2_x = angle2_y = 0;
    flags = 0;
}

bool GenomeProcessor::update(SlotData &slot, int use)
{
    if(use & f_base)slot.base = base / core_count + 1;
    if(use & f_radius)
    {
        slot.radius = radius / core_count;
        if(!slot.radius)return false;
    }
    if(use & f_angle1)
    {
        if(!angle1_x && !angle1_y)return false;
        slot.angle1 = calc_angle(angle1_x, angle1_y);
    }
    if(use & f_angle2)
    {
        if(!angle2_x && !angle2_y)return false;
        slot.angle2 = calc_angle(angle2_x, angle2_y);
    }
    if(use & f_vision)
    {
        slot.flags = flags & Creature::f_visible;
        if(!slot.flags)return false;
    }
    if(use & f_signal)
    {
        slot.flags = flags & Creature::f_signals;
        if(!slot.flags)return false;
    }
    if(use & f_useful)slot.used = true;
    if(use & f_output)slot.used = true;
    else slot.neiro_state = s_input;
    return true;
}

bool GenomeProcessor::update(SlotData &slot)
{
    if(!core_count)return true;
    if(type_or != type_and)return false;
    switch(type_or)
    {
    case Slot::Mouth:    return update(slot, f_output);
    case Slot::Stomach:  return update(slot, f_base | f_useful);
    case Slot::Womb:     return update(slot, f_base | f_output);
    case Slot::Eye:      return update(slot, f_angle1 | f_angle2 | f_radius | f_vision);
    case Slot::Radar:    return update(slot, f_angle1 | f_angle2 | f_vision);
    case Slot::Claw:     return update(slot, f_base | f_angle1 | f_angle2 | f_output);
    case Slot::Hide:     return update(slot, f_base | f_useful);
    case Slot::Leg:      return update(slot, f_base | f_angle1 | f_output);
    case Slot::Rotator:  return update(slot, f_angle2 | f_output);
    case Slot::Signal:   return update(slot, f_signal | f_output);
    default:             return false;
    }
}

void GenomeProcessor::append_slot()
{
    size_t index = slots.size();
    slots.emplace_back(link_start, link_count, act_level, min_level, max_level);
    if(update(slots[index]))slots[index].type = Slot::Type(type_or);
    else slots[index].neiro_state = s_always_off;
}


GenomeProcessor::GenomeProcessor(const Config &config, uint32_t max_links) :
    slot_count(uint32_t(1) << config.slot_bits)
{
    links.reserve(max_links);  reset();
}

void GenomeProcessor::process(const Config &config, Genome::Gene gene)
{
    uint32_t slot = gene.take_bits(config.slot_bits);
    uint32_t type = gene.take_bits(slot_type_bits);
    while(slots.size() < slot)
    {
        append_slot();  reset();
    }
    if(type)
    {
        core_count++;
        type_or  |= type;
        type_and &= type;

        base += gene.take_bits(config.base_bits);
        angle_t angle1 = gene.take_bits(angle_bits);
        angle_t angle2 = gene.take_bits(angle_bits);
        radius += gene.take_bits(radius_bits);
        flags |= gene.take_bits(flag_bits);

        // core_count < 2^14
        uint32_t r_x4 = uint32_t(1) << 18;
        angle1_x += r_sin(r_x4, angle1 + angle_90);
        angle1_y += r_sin(r_x4, angle1);
        angle2_x += r_sin(r_x4, angle2 + angle_90);
        angle2_y += r_sin(r_x4, angle2);

        angle1++;  angle2++;
        angle1_x += r_sin(r_x4, angle1 + angle_90);
        angle1_y += r_sin(r_x4, angle1);
        angle2_x += r_sin(r_x4, angle2 + angle_90);
        angle2_y += r_sin(r_x4, angle2);
    }
    else
    {
        int32_t weight = gene.take_bits_signed(config.base_bits);
        uint32_t source = gene.take_bits(config.slot_bits);
        act_level += weight * gene.take_bits(8);

        (weight < 0 ? min_level : max_level) += 255 * weight;
        links.emplace_back(weight, source);  link_count++;
    }
}

struct Reference
{
    uint32_t source, target;
    int32_t weight;

    explicit Reference(uint32_t source) : source(source)
    {
    }

    Reference(uint32_t target, const GenomeProcessor::LinkData &link) :
        source(link.source), target(target), weight(link.weight)
    {
    }

    bool operator < (const Reference &cmp) const
    {
        return source < cmp.source;
    }
};

void GenomeProcessor::finalize()
{
    while(slots.size() < slot_count)
    {
        append_slot();  reset();
    }

    std::vector<uint32_t> queue;  queue.reserve(slot_count);
    std::vector<Reference> refs;  refs.reserve(links.size() + 1);
    for(uint32_t i = 0; i < slot_count; i++)
    {
        if(slots[i].neiro_state)
        {
            if(slots[i].neiro_state != s_input)queue.push_back(i);  continue;
        }
        if(slots[i].max_level <= slots[i].act_level)
        {
            slots[i].neiro_state = s_always_off;  queue.push_back(i);  continue;
        }
        if(slots[i].min_level > slots[i].act_level)
        {
            slots[i].neiro_state = s_always_on;  queue.push_back(i);  continue;
        }
        uint32_t pos = slots[i].link_start;
        uint32_t end = pos + slots[i].link_count;
        while(pos < end)refs.emplace_back(i, links[pos++]);
    }
    std::sort(refs.begin(), refs.end());
    refs.emplace_back(slot_count);

    std::vector<uint32_t> ref_pos;
    ref_pos.reserve(slot_count + 1);
    ref_pos.push_back(0);  uint32_t pos = 0;
    for(uint32_t i = 0; i < slot_count; i++)
    {
        while(refs[pos].source == i)pos++;
        ref_pos.push_back(pos);
    }

    while(queue.size())
    {
        uint32_t last = queue.size() - 1;
        uint32_t slot = queue[last];  queue.resize(last);

        int32_t mul = (slots[slot].neiro_state == s_always_on ? 255 : -255);
        for(uint32_t pos = ref_pos[slot]; pos < ref_pos[slot + 1]; pos++)
        {
            uint32_t tg = refs[pos].target;
            if(slots[tg].neiro_state)continue;

            int32_t offset = mul * refs[pos].weight;
            if(offset < 0)
            {
                slots[tg].max_level += offset;
                if(slots[tg].max_level > slots[tg].act_level)continue;
                slots[tg].neiro_state = s_always_off;
            }
            else
            {
                slots[tg].min_level += offset;
                if(slots[tg].min_level <= slots[tg].act_level)continue;
                slots[tg].neiro_state = s_always_on;
            }
            queue.push_back(tg);
        }
    }

    for(uint32_t i = 0; i < slot_count; i++)if(slots[i].used)
    {
        if(!slots[i].neiro_state)queue.push_back(i);
        else if(slots[i].neiro_state == s_always_off)slots[i].used = false;
    }
    working_links = 0;
    while(queue.size())
    {
        uint32_t last = queue.size() - 1;
        uint32_t slot = queue[last];  queue.resize(last);

        uint32_t pos = slots[slot].link_start;
        uint32_t end = pos + slots[slot].link_count;
        while(pos < end)
        {
            uint32_t src = links[pos++].source;
            if(slots[src].neiro_state > s_input)continue;
            if(!slots[src].used && !slots[src].neiro_state)queue.push_back(src);
            slots[src].used = true;  working_links++;
        }
    }
};



// Creature struct

Creature::Womb::Womb(const Config &config, const GenomeProcessor::SlotData &slot) :
    energy(slot.base * config.spawn_mul), active(false)
{
    assert(slot.type == Slot::Womb);
}

Creature::Claw::Claw(const Config &config, const GenomeProcessor::SlotData &slot) :
    rad_sqr(slot.radius * slot.radius * sqrt_scale), angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1),
    damage(slot.base * config.damage_mul), active(false)
{
    assert(slot.type == Slot::Claw);
    act_cost = slot.radius * slot.radius * uint32_t(delta + 1) * slot.base;
}

Creature::Leg::Leg(const Config &config, const GenomeProcessor::SlotData &slot) :
    dist_x4(slot.base * config.speed_mul), angle(slot.angle1)
{
    assert(slot.type == Slot::Leg);
}

Creature::Signal::Signal(const Config &config, const GenomeProcessor::SlotData &slot)
{
    if(slot.type == Slot::Mouth)
    {
        flags = f_eating;  act_cost = config.eating_cost;
    }
    else
    {
        assert(slot.type == Slot::Signal);
        flags = slot.flags;  act_cost = config.signal_cost;
    }
}

Creature::Stomach::Stomach(const Config &config, const GenomeProcessor::SlotData &slot) :
    capacity(slot.base * config.capacity_mul), mul(((255ul << 24) - 1) / capacity + 1)
{
    assert(slot.type == Slot::Stomach);
}

Creature::Hide::Hide(const Config &config, const GenomeProcessor::SlotData &slot) :
    life(slot.base * config.life_mul), max_life(life), regen(config.life_regen),
    mul(((255ul << 24) - 1) / max_life + 1)
{
    assert(slot.type == Slot::Hide);
}

Creature::Eye::Eye(const Config &config, const GenomeProcessor::SlotData &slot) :
    rad_sqr(slot.radius * slot.radius * sqrt_scale), angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1),
    flags(slot.flags), count(0)
{
    assert(slot.type == Slot::Eye);
}

Creature::Radar::Radar(const Config &config, const GenomeProcessor::SlotData &slot) :
    angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1), flags(slot.flags), min_r2(max_r2)
{
    assert(slot.type == Slot::Radar);
}

Slot::Type Creature::append_slot(const Config &config, const GenomeProcessor::SlotData &slot)
{
    switch(slot.type)
    {
    case Slot::Womb:
        wombs.emplace_back(config, slot);  break;

    case Slot::Claw:
        claws.emplace_back(config, slot);  break;

    case Slot::Leg:
        legs.emplace_back(config, slot);  break;

    case Slot::Rotator:
        rotators.push_back(slot.angle2);  break;

    case Slot::Mouth:  case Slot::Signal:
        signals.emplace_back(config, slot);  return Slot::Signal;

    case Slot::Stomach:
        stomachs.emplace_back(config, slot);
        max_energy += stomachs.rbegin()->capacity;  break;

    case Slot::Hide:
        hides.emplace_back(config, slot);
        max_life += hides.rbegin()->max_life;  break;

    case Slot::Eye:
        eyes.emplace_back(config, slot);  break;

    case Slot::Radar:
        radars.emplace_back(config, slot);  break;

    default:
        assert(slot.type == Slot::Link);  break;
    }
    return slot.type;
}

uint32_t update_counters(const uint32_t *count, uint32_t *offset, uint32_t &pos, Slot::Type type)
{
    offset[type] = pos;  pos += count[type];  return count[type];
}

Creature::Creature(const Config &config, Genome &genome, const GenomeProcessor &processor, uint32_t count[],
    uint64_t id, const Position &pos, angle_t angle, const Config::SlotCost &passive, uint32_t spawn_energy) :
    id(id), genome(std::move(genome)), pos(pos), angle(angle), passive_energy(passive.initial), max_energy(0),
    passive_cost(passive.per_tick), max_life(0), damage(0), father(config.base_r2), flags(f_creature)
{
    uint32_t offset[Slot::Invalid], n = 0;
    count[Slot::Signal] += count[Slot::Mouth];
    wombs.reserve(update_counters(count, offset, n, Slot::Womb));
    claws.reserve(update_counters(count, offset, n, Slot::Claw));
    legs.reserve(update_counters(count, offset, n, Slot::Leg));
    rotators.reserve(update_counters(count, offset, n, Slot::Rotator));
    signals.reserve(update_counters(count, offset, n, Slot::Signal));
    update_counters(count, offset, n, Slot::Link);
    order.reserve(n);  neirons.resize(n);

    stomachs.reserve(update_counters(count, offset, n, Slot::Stomach));
    hides.reserve(update_counters(count, offset, n, Slot::Hide));
    eyes.reserve(update_counters(count, offset, n, Slot::Eye));
    radars.reserve(update_counters(count, offset, n, Slot::Radar));
    std::vector<slot_t> slots(n);  input.resize(n, 0);

    std::vector<uint32_t> mapping(processor.slot_count, -1);
    for(uint32_t i = 0; i < processor.slot_count; i++)
    {
        const auto &slot = processor.slots[i];  if(!slot.used)continue;

        uint32_t index = offset[append_slot(config, slot)]++;
        mapping[i] = index;  slots[index] = i;

        if(index < neirons.size())order.push_back(index);
    }
    assert(order.size() == neirons.size());
    energy = std::min(spawn_energy - passive.initial, max_energy);
    total_life = max_life;

    assert(wombs.size()    == count[Slot::Womb]);
    assert(claws.size()    == count[Slot::Claw]);
    assert(legs.size()     == count[Slot::Leg]);
    assert(rotators.size() == count[Slot::Rotator]);
    assert(signals.size()  == count[Slot::Signal]);

    assert(stomachs.size() == count[Slot::Stomach]);
    assert(hides.size()    == count[Slot::Hide]);
    assert(eyes.size()     == count[Slot::Eye]);
    assert(radars.size()   == count[Slot::Radar]);

    links.reserve(processor.working_links);
    for(size_t i = 0; i < neirons.size(); i++)
    {
        const auto &slot = processor.slots[slots[i]];
        switch(slot.neiro_state)
        {
        case GenomeProcessor::s_input:       continue;
        case GenomeProcessor::s_always_off:  neirons[i].act_level = +1;  continue;
        case GenomeProcessor::s_always_on:   neirons[i].act_level = -1;  continue;
        default:           /* s_normal */    neirons[i].act_level = slot.act_level;
        }
        uint32_t beg = slot.link_start, end = beg + slot.link_count;
        for(uint32_t j = beg; j < end; j++)
        {
            const auto &link = processor.links[j];
            uint32_t source = mapping[link.source];
            if(source != uint32_t(-1))links.emplace_back(source, i, link.weight);
            else if(processor.slots[link.source].neiro_state == GenomeProcessor::s_always_on)
                neirons[i].act_level -= 255 * link.weight;
        }
    }
    assert(links.size() == processor.working_links);
}

Creature *Creature::spawn(const Config &config, Genome &genome,
    uint64_t id, const Position &pos, angle_t angle, uint32_t spawn_energy)
{
    std::vector<Genome::Gene> genes = genome.genes;
    std::sort(genes.begin(), genes.end());

    GenomeProcessor processor(config, genes.size());
    for(const auto &gene : genes)processor.process(config, gene);
    processor.finalize();

    Config::SlotCost passive;
    passive.initial  = genes.size() * config.gene_init_cost;
    passive.per_tick = genes.size() * config.gene_pass_rate;
    uint32_t count[Slot::Invalid] = {};
    for(auto &slot : processor.slots)
    {
        passive.initial  += config.cost[slot.type].initial;
        passive.per_tick += config.cost[slot.type].per_tick;
        if(slot.type == Slot::Hide)passive.initial += slot.base * config.hide_mul;
        if(slot.used)count[slot.type]++;
    }
    if(spawn_energy < passive.initial)return nullptr;  // not enough energy

    return new Creature(config, genome, processor, count, id, pos, angle, passive, spawn_energy);
}

Creature *Creature::spawn(const Config &config, Random &rand, const Creature &parent,
    uint64_t id, const Position &pos, angle_t angle, uint32_t spawn_energy)
{
    const Creature *father = parent.father.target;
    Genome genome(config, rand, parent.genome, father ? &father->genome : nullptr);
    return spawn(config, genome, id, pos, angle, spawn_energy);
}


void Creature::pre_process(const Config &config)
{
    father.reset(config.base_r2);
    for(auto &eye : eyes)eye.count = 0;
    for(auto &radar : radars)radar.min_r2 = max_r2;
    damage = 0;
}

void Creature::update_view(uint8_t flags, uint64_t r2, angle_t test)
{
    for(auto &eye : eyes)if(eye.flags & flags)
    {
        if(angle_t(test - eye.angle) > eye.delta)continue;
        if(r2 < eye.rad_sqr)eye.count++;
    }
    for(auto &radar : radars)if(radar.flags & flags)
    {
        if(angle_t(test - radar.angle) > radar.delta)continue;
        radar.min_r2 = std::min(radar.min_r2, r2);
    }
}

void Creature::process_detectors(Creature *cr, uint64_t r2, angle_t dir)
{
    father.update(r2, cr);  if(!r2)return;  // invalid angle

    update_view(cr->flags, r2, dir - angle);
    angle_t test = angle_t(dir - cr->angle) ^ flip_angle;
    for(const auto &claw : cr->claws)if(claw.active)
    {
        if(angle_t(test - claw.angle) > claw.delta)continue;
        if(r2 < claw.rad_sqr)damage += claw.damage;
    }
}

void Creature::process_detectors(Creature *cr1, Creature *cr2)
{
    int32_t dx = cr2->pos.x - cr1->pos.x;
    int32_t dy = cr2->pos.y - cr1->pos.y;
    uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
    angle_t angle = calc_angle(dx, dy);

    cr1->process_detectors(cr2, r2, angle);
    cr2->process_detectors(cr1, r2, angle ^ flip_angle);
}

void Creature::process_food(std::vector<Food> &foods)
{
    for(auto &food : foods)if(food.type > Food::Sprout)
    {
        int32_t dx = food.pos.x - pos.x;
        int32_t dy = food.pos.y - pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(flags & f_eating)food.eater.update(r2, this);
        if(!r2)continue;  // invalid angle

        update_view(1 << food.type, r2, calc_angle(dx, dy) - angle);
    }
}

void Creature::post_process()
{
    uint8_t *cur = input.data() + neirons.size();  uint32_t left = energy;
    for(const auto &stomach : stomachs)
    {
        uint32_t level = std::min(left, stomach.capacity);
        *cur++ = uint64_t(level << 8) * stomach.mul >> 32;
        left -= level;
    }
    for(const auto &hide : hides)
    {
        *cur++ = uint64_t(hide.life << 8) * hide.mul >> 32;
    }
    for(const auto &eye : eyes)*cur++ = std::min<uint32_t>(255, eye.count);
    for(const auto &radar : radars)*cur++ = calc_radius(radar.min_r2);
    assert(cur == input.data() + input.size());
}

uint32_t Creature::execute_step(const Config &config)
{
    energy = std::min(energy, max_energy);
    uint32_t total_energy = passive_energy + energy;

    flags = f_creature;
    for(auto &neiron : neirons)neiron.level = 0;
    for(const auto &link : links)
        neirons[link.output].level += link.weight * int16_t(input[link.input]);

    for(size_t i = 0; i < neirons.size(); i++)
        input[i] = neirons[i].level > neirons[i].act_level ? 255 : 0;

    total_life = 0;
    for(size_t i = hides.size() - 1; i != size_t(-1); i--)
    {
        hides[i].life = std::min(hides[i].max_life, hides[i].life + hides[i].regen);
        uint32_t hit = std::min(hides[i].life, damage);
        hides[i].life -= hit;  damage -= hit;
        total_life += hides[i].life;
    }
    if(damage)return total_energy;

    int32_t dx = 0, dy = 0;  angle_t rot = 0;
    uint32_t cost = passive_cost;  uint8_t *cur = input.data();
    for(auto &womb : wombs)if((womb.active = *cur++))cost += womb.energy;
    for(auto &claw : claws)if((claw.active = *cur++))cost += claw.act_cost;
    for(auto &leg : legs)if(*cur++)
    {
        dx += r_sin(leg.dist_x4, angle + leg.angle + angle_90);
        dy += r_sin(leg.dist_x4, angle + leg.angle);
    }
    for(auto &rotator : rotators)if(*cur++)rot += rotator;
    for(auto &sig : signals)if(*cur++)
    {
        flags |= sig.flags;  cost += sig.act_cost;
    }

    pos.x += dx;  pos.y += dy;  angle += rot;
    uint64_t kin = int64_t(dx) * dx + int64_t(dy) * dy;
    uint32_t rot_mul = std::min<angle_t>(256 - rot, rot) * config.rotate_mul;
    kin = (kin + uint64_t(rot_mul) * rot_mul) >> config.mass_order;

    if(kin > uint32_t(-1))return total_energy;
    cost += uint32_t(kin) * uint64_t(total_energy) >> 32;
    if(energy < cost)return total_energy;
    energy -= cost;  return 0;
}


Creature *Creature::load(const Config &config, InStream &stream, uint64_t next_id, uint64_t *buf)
{
    uint64_t id;  stream >> id;  Genome genome;
    if(!stream || !genome.load(config, stream))return nullptr;

    uint32_t x, y;  angle_t angle;  uint32_t energy;
    stream >> x >> y >> angle >> align(4) >> energy;
    if(!stream || id >= next_id || (x >> tile_order) || (y >> tile_order))return nullptr;
    std::unique_ptr<Creature> cr(spawn(config, genome, id, Position{x, y}, angle, uint32_t(-1)));
    return cr && cr->load(stream, energy, buf) ? cr.release() : nullptr;
}

bool Creature::load(InStream &stream, uint32_t load_energy, uint64_t *buf)
{
    if(load_energy > max_energy)return false;  energy = load_energy;

    for(auto &hide : hides)
    {
        stream >> hide.life;  if(!stream || hide.life > hide.max_life)return false;
    }
    stream >> align(8);

    uint32_t n = (order.size() + 63) >> 6;
    for(uint32_t i = 0; i < n; i++)stream >> buf[i];

    uint32_t tail = order.size() & 63;
    if(!stream || tail && (buf[n - 1] & uint64_t(-1) << tail))return false;

    for(size_t i = 0; i < order.size(); i++)
    {
        slot_t slot = order[i];
        input[i] = buf[slot >> 6] & uint64_t(1) << (slot & 63) ? 255 : 0;
    }

    uint8_t *cur = input.data();
    for(auto &womb : wombs)womb.active = *cur++;
    for(auto &claw : claws)claw.active = *cur++;
    cur += legs.size() + rotators.size();  flags = f_creature;
    for(auto &sig : signals)if(*cur++)flags |= sig.flags;
    return true;
}

void Creature::save(OutStream &stream, uint64_t *buf) const
{
    stream << id << genome;
    stream << uint32_t(pos.x & tile_mask) << uint32_t(pos.y & tile_mask);
    stream << angle << align(4) << energy;

    for(auto &hide : hides)stream << hide.life;  stream << align(8);

    uint32_t n = (order.size() + 63) >> 6;
    for(uint32_t i = 0; i < n; i++)buf[i] = 0;
    for(size_t i = 0; i < order.size(); i++)if(input[i])
    {
        slot_t slot = order[i];
        buf[slot >> 6] |= uint64_t(1) << (slot & 63);
    }

    for(uint32_t i = 0; i < n; i++)stream << buf[i];
}



// World struct

World::Tile::Tile(uint64_t seed, size_t index) :
    rand(seed, index), first(nullptr), last(&first), spawn_start(0)
{
}

World::Tile::Tile(const Config &config, const Tile &old, size_t &total_food, size_t reserve) :
    rand(old.rand), first(nullptr), last(&first)
{
    foods.reserve(old.foods.size() + reserve);
    for(const auto &food : old.foods)
        if(food.eater.target)food.eater.target->energy += config.food_energy;
        else if(food.type)foods.emplace_back(config, food);
    total_food += spawn_start = foods.size();
}


bool World::Tile::load(const Config &config, InStream &stream, uint32_t x, uint32_t y,
    uint64_t next_id, size_t &total_food, size_t &total_creature, uint64_t *buf)
{
    assert(foods.empty() && !first);
    uint64_t offs_x = uint64_t(x) << tile_order;
    uint64_t offs_y = uint64_t(y) << tile_order;

    stream.assert_align(8);
    uint32_t food_count, creature_count;
    stream >> rand >> food_count >> creature_count;
    if(!stream)return false;  // TODO: check counts

    foods.resize(spawn_start = food_count);
    for(auto &food : foods)
    {
        if(!food.load(config, stream, offs_x, offs_y))return false;
        if(food.type > Food::Sprout)total_food++;
    }

    for(uint32_t i = 0; i < creature_count; i++)
    {
        Creature *cr = Creature::load(config, stream, next_id, buf);
        if(!cr)
        {
            *last = nullptr;  return false;
        }
        *last = cr;  last = &cr->next;
        cr->pos.x |= offs_x;  cr->pos.y |= offs_y;
    }
    *last = nullptr;  total_creature += creature_count;  return true;
}

void World::Tile::save(OutStream &stream, uint64_t *buf) const
{
    stream.assert_align(8);  stream << rand;

    uint32_t food_count = 0, creature_count = 0;
    for(auto &food : foods)if(food.type)food_count++;
    for(Creature *cr = first; cr; cr = cr->next)creature_count++;
    stream << food_count << creature_count;

    for(auto &food : foods)if(food.type)stream << food;
    for(Creature *cr = first; cr; cr = cr->next)cr->save(stream, buf);
}


World::~World()
{
    for(auto &tile : tiles)for(Creature *ptr = tile.first; ptr;)
    {
        Creature *cr = ptr;  ptr = ptr->next;  delete cr;
    }
}


size_t World::tile_index(Position &pos) const
{
    pos.x &= config.full_mask_x;  pos.y &= config.full_mask_y;
    return (pos.x >> tile_order) | (size_t(pos.y >> tile_order) << config.order_x);
}

void World::spawn_grass(Tile &tile, uint32_t x, uint32_t y)
{
    uint64_t offs_x = uint64_t(x) << tile_order;
    uint64_t offs_y = uint64_t(y) << tile_order;
    uint32_t n = tile.rand.poisson(config.exp_sprout_per_tile);
    for(uint32_t k = 0; k < n; k++)
    {
        uint64_t xx = (tile.rand.uint32() & tile_mask) | offs_x;
        uint64_t yy = (tile.rand.uint32() & tile_mask) | offs_y;
        tile.foods.emplace_back(config, Food::Sprout, Position{xx, yy});
    }
    for(size_t i = 0; i < tile.spawn_start; i++)
    {
        if(tile.foods[i].type != Food::Grass)continue;
        uint32_t n = tile.rand.poisson(config.exp_sprout_per_grass);
        for(uint32_t k = 0; k < n; k++)
        {
            Position pos = tile.foods[i].pos;
            angle_t angle = tile.rand.uint32();
            pos.x += r_sin(config.sprout_dist_x4, angle + angle_90);
            pos.y += r_sin(config.sprout_dist_x4, angle);
            size_t index = tile_index(pos);

            tiles[index].foods.emplace_back(config, Food::Sprout, pos);
        }
    }
}

void World::spawn_meat(Tile &tile, Position pos, uint32_t energy)
{
    if(energy < config.food_energy)return;
    for(energy -= config.food_energy;;)
    {
        size_t index = tile_index(pos);  total_food_count++;
        tiles[index].foods.emplace_back(config, Food::Meat, pos);
        if(energy < config.food_energy)return;
        energy -= config.food_energy;

        angle_t angle = tile.rand.uint32();
        pos.x += r_sin(config.meat_dist_x4, angle + angle_90);
        pos.y += r_sin(config.meat_dist_x4, angle);
    }
}

void World::process_tile_pair(Tile &tile1, Tile &tile2)
{
    if(&tile1 == &tile2)
    {
        for(Creature *cr1 = tile1.first; cr1; cr1 = cr1->next)
            for(Creature *cr2 = cr1->next; cr2; cr2 = cr2->next)
                Creature::process_detectors(cr1, cr2);
    }
    else
    {
        for(Creature *cr1 = tile1.first; cr1; cr1 = cr1->next)
            for(Creature *cr2 = tile2.first; cr2; cr2 = cr2->next)
                Creature::process_detectors(cr1, cr2);
    }

    for(Creature *cr = tile1.first; cr; cr = cr->next)
        cr->process_food(tile2.foods);

    for(Creature *cr = tile2.first; cr; cr = cr->next)
        cr->process_food(tile1.foods);

    for(size_t i = tile1.spawn_start; i < tile1.foods.size(); i++)
        tile1.foods[i].check_grass(config, tile2.foods.data(), tile2.spawn_start);

    for(size_t i = tile2.spawn_start; i < tile2.foods.size(); i++)
        tile2.foods[i].check_grass(config, tile1.foods.data(), tile1.spawn_start);
}

void World::process_detectors()
{
    size_t spawn = 0;
    for(auto &tile : tiles)
    {
        *tile.last = nullptr;
        spawn = std::max(spawn, tile.foods.size() - tile.spawn_start);
        for(Creature *cr = tile.first; cr; cr = cr->next)
            cr->pre_process(config);
    }
    spawn_per_tile = std::max(spawn_per_tile / 2, 2 * spawn + 1);

    for(size_t i = 0; i < tiles.size(); i++)
    {
        uint32_t x = i & config.mask_x, y = i >> config.order_x;
        uint32_t x1 = (x + 1) & config.mask_x, xm = (x - 1) & config.mask_x;
        uint32_t y1 = (y + 1) & config.mask_y;

        process_tile_pair(tiles[i], tiles[i]);
        process_tile_pair(tiles[i], tiles[x1 | (y  << config.order_x)]);
        process_tile_pair(tiles[i], tiles[xm | (y1 << config.order_x)]);
        process_tile_pair(tiles[i], tiles[x  | (y1 << config.order_x)]);
        process_tile_pair(tiles[i], tiles[x1 | (y1 << config.order_x)]);
    }

    for(auto &tile : tiles)
        for(Creature *cr = tile.first; cr; cr = cr->next)
            cr->post_process();
}

void World::next_step()
{
    std::vector<Tile> old;  old.reserve(tiles.size());  old.swap(tiles);

    total_food_count = 0;
    for(size_t i = 0; i < old.size(); i++)
        tiles.emplace_back(config, old[i], total_food_count, spawn_per_tile);

    total_creature_count = 0;
    for(size_t i = 0; i < old.size(); i++)
    {
        spawn_grass(tiles[i], i & config.mask_x, i >> config.order_x);

        for(Creature *ptr = old[i].first; ptr;)
        {
            Creature *cr = ptr;  ptr = ptr->next;

            Position prev_pos = cr->pos;
            angle_t prev_angle = cr->angle;
            uint32_t dead_energy = cr->execute_step(config);
            if(dead_energy)
            {
                delete cr;  spawn_meat(tiles[i], prev_pos, dead_energy);  continue;
            }

            size_t index = tile_index(cr->pos);
            *tiles[index].last = cr;  tiles[index].last = &cr->next;  total_creature_count++;
            for(const auto &womb : cr->wombs)if(womb.active)
            {
                Creature *child = Creature::spawn(config, tiles[i].rand, *cr,
                    next_id++, prev_pos, prev_angle ^ flip_angle, womb.energy);
                if(child)
                {
                    *tiles[i].last = child;  tiles[i].last = &child->next;  total_creature_count++;
                }
                else spawn_meat(tiles[i], prev_pos, womb.energy);
            }
        }
    }
    old.clear();

    process_detectors();  current_time++;
}


const char version_string[] = "Evol0000";

void World::init()
{
    config.order_x = config.order_y = 3;  // 8 x 8
    config.base_radius = tile_size / 64;

    config.chromosome_bits = 6;  // 64 = 32 pair
    config.genome_split_factor = ~(uint32_t(-1) / 64);
    config.chromosome_replace_factor = ~(uint32_t(-1) / 64);
    config.chromosome_copy_prob = uint32_t(-1) / 2;
    config.bit_mutate_factor = ~(uint32_t(-1) / 1024);

    config.slot_bits = 8;  // 256 slots
    config.base_bits = 8;
    config.gene_init_cost = 64;
    config.gene_pass_rate = 1;

    config.cost[Slot::Womb]    = {256, 1};
    config.cost[Slot::Claw]    = {256, 1};
    config.cost[Slot::Leg]     = {256, 1};
    config.cost[Slot::Rotator] = {256, 1};
    config.cost[Slot::Mouth]   = {256, 1};
    config.cost[Slot::Signal]  = {256, 1};

    config.cost[Slot::Stomach] = {256, 1};
    config.cost[Slot::Hide]    = {256, 1};
    config.cost[Slot::Eye]     = {256, 4};
    config.cost[Slot::Radar]   = {256, 4};

    config.cost[Slot::Link]    = {0,   0};

    config.spawn_mul = 256;
    config.capacity_mul = 256;
    config.hide_mul = 256;
    config.damage_mul = 256;
    config.life_mul = 256;
    config.life_regen = 8;
    config.eating_cost = 8;
    config.signal_cost = 8;
    config.speed_mul = tile_size >> 14;
    config.rotate_mul = 8 * config.speed_mul;
    config.mass_order = 2 * tile_order - 38;

    config.food_energy = 4096;
    config.exp_sprout_per_tile  = ~(uint32_t(-1) / 64);
    config.exp_sprout_per_grass = ~(uint32_t(-1) / 64);
    config.repression_range = tile_size / 16;
    config.sprout_dist_x4 = 5 * config.repression_range;
    config.meat_dist_x4 = tile_size / 64;

    bool res = config.calc_derived();
    assert(res);  (void)res;


    uint64_t seed = 1234;
    uint32_t exp_grass_gen    = uint32_t(-1) >> 16;
    uint32_t exp_creature_gen = uint32_t(-1) >> 16;


    spawn_per_tile = 4;  // TODO: config?

    total_food_count = total_creature_count = 0;  next_id = 0;
    size_t size = size_t(1) << (config.order_x + config.order_y);
    tiles.reserve(size);  Genome init_genome(config);
    for(size_t i = 0; i < size; i++)
    {
        tiles.emplace_back(seed, i);
        uint32_t x = i & config.mask_x, y = i >> config.order_x;
        uint64_t offs_x = uint64_t(x) << tile_order;
        uint64_t offs_y = uint64_t(y) << tile_order;

        uint32_t n = tiles[i].rand.poisson(exp_grass_gen);
        for(uint32_t k = 0; k < n; k++)
        {
            uint64_t xx = (tiles[i].rand.uint32() & tile_mask) | offs_x;
            uint64_t yy = (tiles[i].rand.uint32() & tile_mask) | offs_y;
            tiles[i].foods.emplace_back(config, Food::Grass, Position{xx, yy});
        }
        total_food_count += tiles[i].spawn_start = n;

        n = tiles[i].rand.poisson(exp_creature_gen);
        for(uint32_t k = 0; k < n; k++)
        {
            angle_t angle = tiles[i].rand.uint32();
            uint64_t xx = (tiles[i].rand.uint32() & tile_mask) | offs_x;
            uint64_t yy = (tiles[i].rand.uint32() & tile_mask) | offs_y;
            Genome genome(config, tiles[i].rand, init_genome, nullptr);
            Creature *cr = Creature::spawn(config, genome,
                next_id++, Position{xx, yy}, angle, uint32_t(-1));
            *tiles[i].last = cr;  tiles[i].last = &cr->next;
        }
        total_creature_count += n;
    }
    process_detectors();  current_time = 0;
}

bool World::load(InStream &stream)
{
    spawn_per_tile = 4;  // TODO: config?

    stream.assert_align(8);  char header[8];  stream.get(header, 8);
    if(!stream || std::memcmp(header, version_string, sizeof(header)))return false;
    stream >> config >> align(8) >> current_time >> next_id;
    if(!stream)return false;

    size_t size = size_t(1) << (config.order_x + config.order_y);
    tiles.resize(size);  total_food_count = total_creature_count = 0;
    std::vector<uint64_t> buf(std::max<uint32_t>(1, config.slot_bits >> 6));
    for(size_t i = 0; i < size; i++)
    {
        uint32_t x = i & config.mask_x, y = i >> config.order_x;
        if(!tiles[i].load(config, stream, x, y, next_id,
            total_food_count, total_creature_count, buf.data()))return false;
    }
    process_detectors();  return true;
}

void World::save(OutStream &stream) const
{
    stream.assert_align(8);  stream.put(version_string, 8);
    stream << config << align(8) << current_time << next_id;
    std::vector<uint64_t> buf(std::max<uint32_t>(1, config.slot_bits >> 6));
    for(auto &tile : tiles)tile.save(stream, buf.data());
}
