// world.cpp : world mechanics implementation
//

#include "video.h"
#include "stream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <cmath>



// Config struct

bool Config::calc_derived()
{
    constexpr uint64_t max_cost = 1ull << 48;
    constexpr uint32_t max_value = 1ul << 24;

    if(!order_x || order_x >= 16)return false;
    if(!order_y || order_y >= 16)return false;
    if(base_radius > tile_size)return false;

    if(!chromosome_bits || chromosome_bits > 16)return false;
    if(slot_bits > 8 || base_bits > 16)return false;
    if(genome_split_factor > 0xFFFFFF00)return false;  // TODO: handle overflows
    if(chromosome_replace_factor > 0xFFFFFF00)return false;
    if(bit_mutate_factor > 0xFFFFFF00)return false;

    if(!base_cost.initial || !base_cost.per_tick)return false;
    if(base_cost.initial > max_cost || base_cost.per_tick > max_cost)return false;
    if(gene_cost.initial > max_cost || gene_cost.per_tick > max_cost)return false;
    for(const auto &c : cost)if(c.initial > max_cost || c.per_tick > max_cost)return false;
    if(eating_cost > max_cost || signal_cost > max_cost)return false;

    uint64_t limit_cost = max_cost >> base_bits;
    uint32_t limit_value = max_value >> base_bits;
    if(spawn_mul > limit_cost || capacity_mul > limit_cost || hide_mul > limit_cost)return false;
    if(damage_mul > limit_value || life_mul > limit_value || life_regen > 65536)return false;
    if(speed_mul > (uint32_t(-1) >> base_bits))return false;
    if(rotate_mul > (uint32_t(-1) >> 7))return false;
    if(mass_order >= 64)return false;

    if(food_energy > max_cost)return false;
    if(repression_range > tile_size)return false;
    if(sprout_dist_x4 <= 4 * repression_range)return false;

    mask_x = (uint32_t(1) << order_x) - 1;
    mask_y = (uint32_t(1) << order_y) - 1;
    full_mask_x = (uint64_t(1) << (order_x + tile_order)) - 1;
    full_mask_y = (uint64_t(1) << (order_x + tile_order)) - 1;;
    base_r2 = uint64_t(base_radius) * base_radius;
    repression_r2 = uint64_t(repression_range) * repression_range;

    shift_base = 23 - base_bits;
    shift_cap = shift_base - ilog2(capacity_mul) + 40;
    shift_life = shift_base - ilog2(life_mul) + 8;
    return true;
}

bool Config::load(InStream &stream)
{
    stream.assert_align(8);
    stream >> order_x >> order_y >> align(4) >> base_radius;

    stream >> chromosome_bits >> slot_bits >> base_bits >> align(4);
    stream >> genome_split_factor >> chromosome_replace_factor;
    stream >> chromosome_copy_prob >> bit_mutate_factor >> align(8);

    stream >> base_cost.initial >> base_cost.per_tick;
    stream >> gene_cost.initial >> gene_cost.per_tick;
    for(auto &c : cost)stream >> c.initial >> c.per_tick;
    stream >> eating_cost >> signal_cost;

    stream >> spawn_mul >> capacity_mul >> hide_mul;
    stream >> damage_mul >> life_mul >> life_regen;
    stream >> speed_mul >> rotate_mul >> mass_order >> align(8);

    stream >> food_energy >> exp_sprout_per_tile >> exp_sprout_per_grass;
    stream >> repression_range >> sprout_dist_x4 >> meat_dist_x4 >> align(8);

    return stream && calc_derived();
}

void Config::save(OutStream &stream) const
{
    stream.assert_align(4);
    stream << order_x << order_y << align(4) << base_radius;

    stream << chromosome_bits << slot_bits << base_bits << align(4);
    stream << genome_split_factor << chromosome_replace_factor;
    stream << chromosome_copy_prob << bit_mutate_factor << align(8);

    stream << base_cost.initial << base_cost.per_tick;
    stream << gene_cost.initial << gene_cost.per_tick;
    for(auto &c : cost)stream << c.initial << c.per_tick;
    stream << eating_cost << signal_cost;

    stream << spawn_mul << capacity_mul << hide_mul;
    stream << damage_mul << life_mul << life_regen;
    stream << speed_mul << rotate_mul << mass_order << align(8);

    stream << food_energy << exp_sprout_per_tile << exp_sprout_per_grass;
    stream << repression_range << sprout_dist_x4 << meat_dist_x4 << align(8);
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

void Detector::update(uint64_t r2, const Creature *cr)
{
    if(r2 > min_r2)return;
    if(r2 == min_r2 && cr->id > id)return;
    min_r2 = r2;  id = cr->id;  target = cr;
}



// Food struct

Food::Food(const Config &config, Type type, const Position &pos) : type(type), pos(pos), eater(config.base_r2)
{
}

Food::Food(const Config &config, const Food &food) : Food(config, food.type > sprout ? food.type : grass, food.pos)
{
}

void Food::set(const Config &config, const Food &food)
{
    type = food.type > sprout ? food.type : grass;
    pos = food.pos;  eater.reset(config.base_r2);
}


void Food::check_grass(const Config &config, const Food *food, size_t n)
{
    assert(type == sprout);
    for(size_t i = 0; i < n; i++)if(food[i].type == grass)
    {
        int32_t dx = pos.x - food[i].pos.x;
        int32_t dy = pos.y - food[i].pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(r2 >= config.repression_r2)continue;
        type = dead;  return;
    }
}


bool Food::load(const Config &config, InStream &stream, uint64_t offs_x, uint64_t offs_y)
{
    eater.reset(config.base_r2);

    uint32_t x, y;  stream >> x >> y;  if(!stream)return false;

    type = Type(x >> tile_order);
    pos.x = x & tile_mask | offs_x;  pos.y = y | offs_y;
    return type > dead && type <= meat && !(y >> tile_order);
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
    Slot::Type type = Slot::link;
    uint32_t base = weight & ((1 << config.base_bits) - 1);

    int shift = 64;  data = 0;
    shift -= config.slot_bits;  data |= uint64_t(slot)   << shift;
    shift -= slot_type_bits;    data |= uint64_t(type)   << shift;
    shift -= config.slot_bits;  data |= uint64_t(source) << shift;
    shift -= config.base_bits;  data |= uint64_t(base)   << shift;
    shift -= 8;                 data |= uint64_t(offset) << shift;
    assert(shift >= 0);
}

Genome::Genome(const Config &config)
{
    genes.emplace_back(config, 0, Slot::mouth,     0, 0, 0, 0, 0);
    genes.emplace_back(config, 1, Slot::stomach, 255, 0, 0, 0, 0);
    genes.emplace_back(config, 2, Slot::womb,     63, 0, 0, 0, 0);
    genes.emplace_back(config, 3, Slot::leg,     255, 0, 0, 0, 0);

    genes.emplace_back(config, 0,  -64, 9, 255);
    genes.emplace_back(config, 2,   64, 1, 250);
    genes.emplace_back(config, 3,  -64, 9, 255);

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
            if(pairs[i >> 5] & uint32_t(1) << (i & 31))
                seqs.emplace_back(pos_m + parent.chromosomes[i], parent.chromosomes[i + 1]);
            else seqs.emplace_back(pos_m, parent.chromosomes[i]);
            pos_m += parent.chromosomes[i] + parent.chromosomes[i + 1];

            if(pairs[i >> 5] & uint32_t(2) << (i & 31))
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
        genes[pos >> 6].data ^= uint64_t(1) << (pos & 63);
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



// GenomeProcessor class

void GenomeProcessor::State::reset(size_t link_pos)
{
    link_start = link_pos;
    link_count = core_count = 0;
    act_level = min_level = max_level = 0;
    type_and = -1;  type_or = 0;

    base = 0;  radius = 0;
    angle1_x = angle1_y = 0;
    angle2_x = angle2_y = 0;
    flags = 0;
}

bool GenomeProcessor::State::update(SlotData &slot, int use)
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

bool GenomeProcessor::State::update(SlotData &slot)
{
    if(!core_count)return true;
    if(type_or != type_and)return false;
    switch(type_or)
    {
    case Slot::mouth:    return update(slot, f_output);
    case Slot::stomach:  return update(slot, f_base | f_useful);
    case Slot::womb:     return update(slot, f_base | f_output);
    case Slot::eye:      return update(slot, f_angle1 | f_angle2 | f_radius | f_vision);
    case Slot::radar:    return update(slot, f_angle1 | f_angle2 | f_vision);
    case Slot::claw:     return update(slot, f_base | f_angle1 | f_angle2 | f_radius | f_output);
    case Slot::hide:     return update(slot, f_base | f_useful);
    case Slot::leg:      return update(slot, f_base | f_angle1 | f_output);
    case Slot::rotator:  return update(slot, f_angle2 | f_output);
    case Slot::signal:   return update(slot, f_signal | f_output);
    default:             return false;
    }
}

void GenomeProcessor::State::create_slot(SlotData &slot, size_t link_pos)
{
    slot.link_start = link_start;  slot.link_count = link_count;
    slot.act_level = act_level;  slot.min_level = min_level;  slot.max_level = max_level;
    slot.neiro_state = s_normal;  slot.used = false;  slot.type = Slot::invalid;
    if(update(slot))slot.type = Slot::Type(type_or);
    else slot.neiro_state = s_always_off;
    reset(link_pos);
}

void GenomeProcessor::State::process_gene(const Config &config, Genome::Gene &gene, std::vector<LinkData> &links)
{
    uint32_t type = gene.take_bits(slot_type_bits);
    if(!type)
    {
        uint32_t source = gene.take_bits(config.slot_bits);
        int32_t weight = gene.take_bits_signed(config.base_bits);
        if(!weight)return;  act_level += weight * gene.take_bits(8);

        (weight < 0 ? min_level : max_level) += 255 * weight;
        links.emplace_back(weight, source);  link_count++;  return;
    }

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


void GenomeProcessor::update(const Config &config, const Genome &genome)
{
    uint32_t slot_count = uint32_t(1) << config.slot_bits;
    slots.resize(slot_count);  links.clear();

    std::vector<Genome::Gene> genes = genome.genes;
    std::sort(genes.begin(), genes.end());
    links.reserve(genes.size());

    State state;  size_t index = 0;
    for(Genome::Gene gene : genes)
    {
        uint32_t slot = gene.take_bits(config.slot_bits);
        while(index < slot)state.create_slot(slots[index++], links.size());
        state.process_gene(config, gene, links);
    }
    while(index < slot_count)state.create_slot(slots[index++], links.size());
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
    std::vector<uint32_t> queue;  queue.reserve(slots.size());
    std::vector<Reference> refs;  refs.reserve(links.size() + 1);
    for(size_t i = 0; i < slots.size(); i++)
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
    refs.emplace_back(slots.size());

    std::vector<uint32_t> ref_pos;
    ref_pos.reserve(slots.size() + 1);
    ref_pos.push_back(0);  uint32_t pos = 0;
    for(size_t i = 0; i < slots.size(); i++)
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

    for(size_t i = 0; i < slots.size(); i++)if(slots[i].used)
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

void GenomeProcessor::process(const Config &config, const Genome &genome)
{
    update(config, genome);  finalize();
    passive_cost.initial  = config.base_cost.initial  + genome.genes.size() * config.gene_cost.initial;
    passive_cost.per_tick = config.base_cost.per_tick + genome.genes.size() * config.gene_cost.per_tick;
    max_energy = max_life = 0;  std::memset(count, 0, sizeof(count));
    for(const auto &slot : slots)
    {
        passive_cost.initial  += config.cost[slot.type].initial;
        passive_cost.per_tick += config.cost[slot.type].per_tick;
        if(slot.type == Slot::stomach)
            max_energy += slot.base * config.capacity_mul;
        else if(slot.type == Slot::hide)
        {
            passive_cost.initial += slot.base * config.hide_mul;
            max_life += slot.base * config.life_mul;
        }
        if(slot.used)count[slot.type]++;
    }
}



// Creature struct

Creature::Womb::Womb(const Config &config, const GenomeProcessor::SlotData &slot) :
    energy(slot.base * config.spawn_mul), active(false)
{
    assert(slot.type == Slot::womb);
}

Creature::Claw::Claw(const Config &config, const GenomeProcessor::SlotData &slot) :
    rad_sqr(slot.radius * slot.radius * sqrt_scale), damage(slot.base * config.damage_mul),
    angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1), active(false)
{
    assert(slot.type == Slot::claw);
    act_cost = slot.radius * slot.radius * uint32_t(delta + 1) * slot.base;
}

Creature::Leg::Leg(const Config &config, const GenomeProcessor::SlotData &slot) :
    dist_x4(slot.base * config.speed_mul), angle(slot.angle1)
{
    assert(slot.type == Slot::leg);
}

Creature::Signal::Signal(const Config &config, const GenomeProcessor::SlotData &slot)
{
    if(slot.type == Slot::mouth)
    {
        flags = f_eating;  act_cost = config.eating_cost;
    }
    else
    {
        assert(slot.type == Slot::signal);
        flags = slot.flags;  act_cost = config.signal_cost;
    }
}

Creature::Stomach::Stomach(const Config &config, const GenomeProcessor::SlotData &slot) :
    capacity(slot.base * config.capacity_mul)
{
    assert(slot.type == Slot::stomach);
    int shift = config.shift_cap - config.shift_base - 8;
    mul = uint64_t(-1) / (shift < 0 ? capacity >> -shift : capacity << shift);
}

Creature::Hide::Hide(const Config &config, const GenomeProcessor::SlotData &slot) :
    life(slot.base * config.life_mul), max_life(life), regen(config.life_regen),
    mul(((255ul << 24) - 1) / max_life + 1)
{
    assert(slot.type == Slot::hide);
    int shift = config.shift_life - config.shift_base + 24;
    mul = uint64_t(-1) / (uint64_t(max_life) << shift);
}

Creature::Eye::Eye(const Config &config, const GenomeProcessor::SlotData &slot) :
    rad_sqr(slot.radius * slot.radius * sqrt_scale), angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1),
    flags(slot.flags), count(0)
{
    assert(slot.type == Slot::eye);
}

Creature::Radar::Radar(const Config &config, const GenomeProcessor::SlotData &slot) :
    angle(slot.angle1), delta(slot.angle2 - slot.angle1 - 1), flags(slot.flags), min_r2(max_r2)
{
    assert(slot.type == Slot::radar);
}

void Creature::update_max_visibility(uint8_t vis_flags, uint64_t r2)
{
    for(int i = 0; i < f_creature; i++)if(vis_flags & (f_creature | i))
        creature_vis_r2[i] = std::max(creature_vis_r2[i], r2);
    if(vis_flags & f_grass)food_vis_r2[0] = std::max(food_vis_r2[0], r2);
    if(vis_flags &  f_meat)food_vis_r2[1] = std::max(food_vis_r2[1], r2);
}

Slot::Type Creature::append_slot(const Config &config, const GenomeProcessor::SlotData &slot)
{
    switch(slot.type)
    {
    case Slot::womb:
        wombs.emplace_back(config, slot);  break;

    case Slot::claw:
        claws.emplace_back(config, slot);  break;

    case Slot::leg:
        legs.emplace_back(config, slot);  break;

    case Slot::rotator:
        rotators.push_back(slot.angle2);  break;

    case Slot::mouth:  case Slot::signal:
        signals.emplace_back(config, slot);  return Slot::signal;

    case Slot::stomach:
        stomachs.emplace_back(config, slot);  break;

    case Slot::hide:
        hides.emplace_back(config, slot);  break;

    case Slot::eye:
        eyes.emplace_back(config, slot);
        update_max_visibility(eyes.rbegin()->flags, eyes.rbegin()->rad_sqr);
        break;

    case Slot::radar:
        radars.emplace_back(config, slot);
        update_max_visibility(radars.rbegin()->flags, max_r2);
        break;

    default:
        assert(slot.type == Slot::link);  break;
    }
    return slot.type;
}

uint32_t update_counters(const uint32_t *count, uint32_t *offset, uint32_t &pos, Slot::Type type)
{
    uint32_t n = count[type];
    if(type == Slot::signal)n += count[Slot::mouth];
    offset[type] = pos;  pos += n;  return n;
}

void Creature::calc_mapping(const GenomeProcessor &proc, std::vector<uint32_t> &mapping)
{
    uint32_t offset[Slot::invalid], n = 0;
    update_counters(proc.count, offset, n, Slot::womb);
    update_counters(proc.count, offset, n, Slot::claw);
    update_counters(proc.count, offset, n, Slot::leg);
    update_counters(proc.count, offset, n, Slot::rotator);
    update_counters(proc.count, offset, n, Slot::signal);
    update_counters(proc.count, offset, n, Slot::link);
    update_counters(proc.count, offset, n, Slot::stomach);
    update_counters(proc.count, offset, n, Slot::hide);
    update_counters(proc.count, offset, n, Slot::eye);
    update_counters(proc.count, offset, n, Slot::radar);

    mapping.resize(proc.slots.size());
    for(size_t i = 0; i < proc.slots.size(); i++)
    {
        if(!proc.slots[i].used)
        {
            mapping[i] = uint32_t(-1);  continue;
        }
        Slot::Type type = proc.slots[i].type;
        if(type == Slot::mouth)type = Slot::signal;
        mapping[i] = offset[type]++;
    }
}

Creature::Creature(const Config &config, Genome &genome, const GenomeProcessor &proc,
    uint64_t id, const Position &pos, angle_t angle, uint64_t spawn_energy) :
    id(id), genome(std::move(genome)), pos(pos), angle(angle),
    energy(std::min(spawn_energy - proc.passive_cost.initial, proc.max_energy)),
    max_energy(proc.max_energy), passive_cost(proc.passive_cost), food_energy(0),
    total_life(proc.max_life), max_life(proc.max_life), damage(0),
    attack_count(0), creature_vis_r2{}, food_vis_r2{}, claw_r2(0),
    father(config.base_r2), flags(f_creature)
{
    uint32_t offset[Slot::invalid], n = 0;
    wombs.reserve(update_counters(proc.count, offset, n, Slot::womb));
    claws.reserve(update_counters(proc.count, offset, n, Slot::claw));
    legs.reserve(update_counters(proc.count, offset, n, Slot::leg));
    rotators.reserve(update_counters(proc.count, offset, n, Slot::rotator));
    signals.reserve(update_counters(proc.count, offset, n, Slot::signal));
    update_counters(proc.count, offset, n, Slot::link);
    order.reserve(n);  neirons.resize(n);

    stomachs.reserve(update_counters(proc.count, offset, n, Slot::stomach));
    hides.reserve(update_counters(proc.count, offset, n, Slot::hide));
    eyes.reserve(update_counters(proc.count, offset, n, Slot::eye));
    radars.reserve(update_counters(proc.count, offset, n, Slot::radar));
    std::vector<slot_t> slots(n);  input.resize(n, 0);

    std::vector<uint32_t> mapping(proc.slots.size(), -1);
    for(size_t i = 0; i < proc.slots.size(); i++)
    {
        if(!proc.slots[i].used)continue;

        uint32_t index = offset[append_slot(config, proc.slots[i])]++;
        mapping[i] = index;  slots[index] = i;

        if(index < neirons.size())order.push_back(index);
    }
    assert(order.size() == neirons.size());

    assert(wombs.size()    == proc.count[Slot::womb]);
    assert(claws.size()    == proc.count[Slot::claw]);
    assert(legs.size()     == proc.count[Slot::leg]);
    assert(rotators.size() == proc.count[Slot::rotator]);
    assert(signals.size()  == proc.count[Slot::mouth] + proc.count[Slot::signal]);

    assert(stomachs.size() == proc.count[Slot::stomach]);
    assert(hides.size()    == proc.count[Slot::hide]);
    assert(eyes.size()     == proc.count[Slot::eye]);
    assert(radars.size()   == proc.count[Slot::radar]);

    links.reserve(proc.working_links);
    for(size_t i = 0; i < neirons.size(); i++)
    {
        const auto &slot = proc.slots[slots[i]];
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
            const auto &link = proc.links[j];
            switch(proc.slots[link.source].neiro_state)
            {
            case GenomeProcessor::s_always_off:  break;
            case GenomeProcessor::s_always_on:  neirons[i].act_level -= 255 * link.weight;  break;
            default:  links.emplace_back(mapping[link.source], i, link.weight);
            }
        }
    }
    assert(links.size() == proc.working_links);
}

Creature *Creature::spawn(const Config &config, Genome &genome,
    uint64_t id, const Position &pos, angle_t angle, uint64_t spawn_energy)
{
    GenomeProcessor proc(config, genome);
    if(spawn_energy < proc.passive_cost.initial)return nullptr;
    return new Creature(config, genome, proc, id, pos, angle, spawn_energy);
}

Creature *Creature::spawn(const Config &config, Random &rand, const Creature &parent,
    uint64_t id, const Position &pos, angle_t angle, uint64_t spawn_energy)
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

void Creature::update_view(uint8_t tg_flags, uint64_t r2, angle_t dir)
{
    dir -= angle;
    for(auto &eye : eyes)if(eye.flags & tg_flags)
    {
        if(angle_t(dir - eye.angle) > eye.delta)continue;
        if(r2 < eye.rad_sqr)eye.count++;
    }
    for(auto &radar : radars)if(radar.flags & tg_flags)
    {
        if(angle_t(dir - radar.angle) > radar.delta)continue;
        radar.min_r2 = std::min(radar.min_r2, r2);
    }
}

void Creature::update_damage(const Creature *cr, uint64_t r2, angle_t dir)
{
    angle_t test = angle_t(dir - cr->angle) ^ flip_angle;
    for(auto &claw : cr->claws)if(claw.active)
    {
        if(angle_t(test - claw.angle) > claw.delta)continue;
        if(r2 < claw.rad_sqr)damage += claw.damage;
    }
}

void Creature::process_food(const std::vector<Food> &foods)
{
    for(auto &food : foods)if(food.type > Food::sprout)
    {
        int32_t dx = food.pos.x - pos.x;
        int32_t dy = food.pos.y - pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(!r2)continue;  // invalid angle

        if(r2 >= food_vis_r2[food.type - Food::grass])continue;
        update_view(food.type == Food::grass ? f_grass : f_meat, r2, calc_angle(dx, dy));
    }
}

void Creature::eat_food(std::vector<Food> &foods) const
{
    assert(flags & f_eating);
    for(auto &food : foods)if(food.type > Food::sprout)
    {
        int32_t dx = food.pos.x - pos.x;
        int32_t dy = food.pos.y - pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        food.eater.update(r2, this);
    }
}

void Creature::process_detectors(const Creature *cr)
{
    int32_t dx = cr->pos.x - pos.x;
    int32_t dy = cr->pos.y - pos.y;
    uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
    father.update(r2, cr);  if(!r2)return;  // invalid angle

    uint64_t view = creature_vis_r2[cr->flags & f_signals];
    if(r2 >= std::max(view, cr->claw_r2))return;

    angle_t angle = calc_angle(dx, dy);
    if(r2 < view)update_view(cr->flags, r2, angle);
    if(r2 < cr->claw_r2)update_damage(cr, r2, angle);
}

void Creature::post_process(const Config &config)
{
    uint8_t *cur = input.data() + neirons.size();  uint64_t left = energy;
    for(const auto &stomach : stomachs)
    {
        uint64_t stock = std::min(left, stomach.capacity);
        uint32_t level = stock << config.shift_cap >> 32;
        *cur++ = uint64_t(level) * stomach.mul >> (config.shift_base + 32);
        left -= stock;
    }
    for(const auto &hide : hides)
    {
        uint32_t level = hide.life << config.shift_life;
        *cur++ = uint64_t(level) * hide.mul >> (config.shift_base + 32);
    }
    for(const auto &eye : eyes)*cur++ = std::min<uint32_t>(255, eye.count);
    for(const auto &radar : radars)*cur++ = calc_radius(radar.min_r2);
    assert(cur == input.data() + input.size());
}

uint64_t Creature::execute_step(const Config &config)
{
    energy = std::min(energy + food_energy, max_energy);
    uint64_t total_energy = passive_cost.initial + energy;
    food_energy = 0;

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
    attack_count = 0;  claw_r2 = 0;  flags = f_creature;
    uint64_t cost = passive_cost.per_tick;  uint8_t *cur = input.data();
    for(auto &womb : wombs)if((womb.active = *cur++))cost += womb.energy;
    for(auto &claw : claws)if((claw.active = *cur++))
    {
        cost += claw.act_cost;  attack_count++;
        claw_r2 = std::max(claw_r2, claw.rad_sqr);
    }
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
    cost += uint32_t(kin) * uint64_t(uint32_t(total_energy >> 32));
    cost += uint32_t(kin) * uint64_t(uint32_t(total_energy)) >> 32;
    if(energy < cost)return total_energy;
    energy -= cost;  return 0;
}


Creature *Creature::load(const Config &config, InStream &stream, uint64_t next_id, uint64_t *buf)
{
    uint64_t id;  stream >> id;  Genome genome;
    if(!stream || !genome.load(config, stream))return nullptr;

    uint32_t x, y;  angle_t angle;  uint64_t energy;
    stream >> x >> y >> angle >> align(8) >> energy;
    if(!stream || id >= next_id || (x >> tile_order) || (y >> tile_order))return nullptr;
    std::unique_ptr<Creature> cr(spawn(config, genome, id, Position{x, y}, angle, uint64_t(-1)));
    return cr && cr->load(stream, energy, buf) ? cr.release() : nullptr;
}

bool Creature::load(InStream &stream, uint64_t load_energy, uint64_t *buf)
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
    attack_count = 0;  claw_r2 = 0;  flags = f_creature;
    for(auto &womb : wombs)womb.active = *cur++;
    for(auto &claw : claws)if((claw.active = *cur++))
    {
        claw_r2 = std::max(claw_r2, claw.rad_sqr);  attack_count++;
    }
    cur += legs.size() + rotators.size();
    for(auto &sig : signals)if(*cur++)flags |= sig.flags;
    return true;
}

void Creature::save(OutStream &stream, uint64_t *buf) const
{
    stream << id << genome;
    stream << uint32_t(pos.x & tile_mask) << uint32_t(pos.y & tile_mask);
    stream << angle << align(8) << energy;

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



// TileLayout struct

TileLayout::TileLayout(uint32_t size_x, uint32_t size_y, uint32_t group_count) :
    size_x(size_x), size_y(size_y), tiles(size_x * size_y), groups(group_count)
{
    for(size_t i = 0; i < tiles.size(); i++)
    {
        uint32_t group = uint64_t(i) * group_count / tiles.size();

        tiles[i].group = group;
        tiles[i].index = groups[group].tile_count++;
        tiles[i].ref_count = 0;
    }
}

void TileLayout::process_tile(TileDesc &cur, const Offsets &offs_x, const Offsets &offs_y)
{
    uint32_t prev = -1, ref;
    for(int i = 0; i < 3; i++)for(int j = 0; j < 3; j++)
    {
        TileDesc &tile = tiles[offs_y.pos[i] + offs_x.pos[j]];
        if(prev != tile.group)
        {
            ref = groups[prev = tile.group].ref_count++;
            cur.refs[cur.ref_count++] = {prev, ref};
        }
        tile.neighbors[4 + offs_y.offs[i] + offs_x.offs[j]] = ref;
    }
}

void TileLayout::process_line(uint32_t pos, const Offsets &offs_y)
{
    uint32_t n = size_x - 1;
    process_tile(tiles[pos], Offsets(0, 0, 1, -1, n, 1), offs_y);
    for(uint32_t x = 1; x < n; x++)
        process_tile(tiles[pos + x], Offsets(x - 1, 1, x, 0, x + 1, -1), offs_y);
    process_tile(tiles[pos + n], Offsets(0, -1, n - 1, 1, n, 0), offs_y);
}

void TileLayout::build_layout()
{
    uint32_t n = size_x * (size_y - 1);
    process_line(0, Offsets(0, 0, size_x, -3, n, 3));
    for(uint32_t pos = size_x; pos < n; pos += size_x)
        process_line(pos, Offsets(pos - size_x, 3, pos, 0, pos + size_x, -3));
    process_line(n, Offsets(0, -3, n - size_x, 3, n, 0));
}



// TileGroup struct

void TileGroup::alloc(const TileLayout::GroupDesc &desc)
{
    tiles.resize(desc.tile_count);  buffers.resize(desc.ref_count);
}

TileGroup::Tile::Tile()
{
    first = nullptr;  last = &first;
}

TileGroup::Tile::~Tile()
{
    for(Creature *ptr = first; ptr;)
    {
        Creature *cr = ptr;  ptr = ptr->next;  delete cr;
    }
}

void TileGroup::Tile::init(const TileLayout::TileDesc &desc)
{
    std::memcpy(neighbors, desc.neighbors, sizeof(neighbors));
    for(int i = 0; i < desc.ref_count; i++)refs[i] = desc.refs[i];
    ref_count = desc.ref_count;
}


uint32_t TileGroup::neighbor_index(const Config &config, const Tile &tile, Position &pos)
{
    pos.x &= config.full_mask_x;
    pos.y &= config.full_mask_y;
    uint32_t dx = (uint32_t(pos.x >> tile_order) - tile.x + 1) & config.mask_x;
    uint32_t dy = (uint32_t(pos.y >> tile_order) - tile.y + 1) & config.mask_y;
    assert(dx < 3 && dy < 3);

    return tile.neighbors[dx + 3 * dy];
}

void TileGroup::spawn_grass(const Config &config, Tile &tile)  // TODO: tile relative position
{
    uint64_t offs_x = uint64_t(tile.x) << tile_order;
    uint64_t offs_y = uint64_t(tile.y) << tile_order;
    uint32_t n = tile.rand.poisson(config.exp_sprout_per_tile);
    for(uint32_t k = 0; k < n; k++)
    {
        uint64_t xx = (tile.rand.uint32() & tile_mask) | offs_x;
        uint64_t yy = (tile.rand.uint32() & tile_mask) | offs_y;
        buffers[tile.neighbors[4]].foods.emplace_back(config, Food::sprout, Position{xx, yy});
    }
    for(size_t i = 0; i < tile.foods.size(); i++)
    {
        if(tile.foods[i].type != Food::grass)continue;
        uint32_t n = tile.rand.poisson(config.exp_sprout_per_grass);
        for(uint32_t k = 0; k < n; k++)
        {
            Position pos = tile.foods[i].pos;
            angle_t angle = tile.rand.uint32();
            pos.x += r_sin(config.sprout_dist_x4, angle + angle_90);
            pos.y += r_sin(config.sprout_dist_x4, angle);

            uint32_t index = neighbor_index(config, tile, pos);
            buffers[index].foods.emplace_back(config, Food::sprout, pos);
        }
    }
}

void TileGroup::spawn_meat(const Config &config, Tile &tile, Position pos, uint64_t energy)
{
    if(energy < config.food_energy)return;
    for(energy -= config.food_energy;;)
    {
        auto &buf = buffers[neighbor_index(config, tile, pos)];
        buf.foods.emplace_back(config, Food::meat, pos);  buf.food_count++;
        if(energy < config.food_energy)return;  energy -= config.food_energy;

        angle_t angle = tile.rand.uint32();
        pos.x += r_sin(config.meat_dist_x4, angle + angle_90);
        pos.y += r_sin(config.meat_dist_x4, angle);
    }
}

void TileGroup::execute_step(const Config &config)
{
    for(auto &buf : buffers)
    {
        buf.foods.clear();  buf.last = &buf.first;
        buf.food_count = buf.creature_count = buf.attack_count = 0;
    }

    Creature **del_last = &del_queue;
    for(auto &tile : tiles)
    {
        auto &foods = tile.foods;  size_t n = 0;
        for(size_t i = 0; i < foods.size(); i++)
            if(!foods[i].eater.target && foods[i].type)foods[n++].set(config, foods[i]);
        foods.resize(tile.spawn_start = tile.food_count = n);
        spawn_grass(config, tile);

        uint64_t id = next_id;
        Creature *ptr = tile.first;  tile.last = &tile.first;
        tile.creature_count = tile.attack_count = 0;
        while(ptr)
        {
            Creature *cr = ptr;  ptr = ptr->next;

            Position prev_pos = cr->pos;
            angle_t prev_angle = cr->angle;
            uint64_t dead_energy = cr->execute_step(config);
            if(dead_energy)
            {
                *del_last = cr;  del_last = &cr->next;  // potential father
                spawn_meat(config, tile, prev_pos, dead_energy);  continue;
            }

            buffers[neighbor_index(config, tile, cr->pos)].append(cr);
            for(const auto &womb : cr->wombs)if(womb.active)
            {
                Creature *child = Creature::spawn(config, tile.rand, *cr,
                    id++, prev_pos, prev_angle ^ flip_angle, womb.energy);
                uint64_t leftover = womb.energy;
                if(child)
                {
                    leftover -= child->passive_cost.initial + child->energy;
                    tile.append(child);
                }
                spawn_meat(config, tile, prev_pos, leftover);
            }
        }
        tile.children_count = id - next_id;  *tile.last = nullptr;
    }
    *del_last = nullptr;
}

void TileGroup::consolidate(const std::vector<Reference> &layout, std::vector<TileGroup> &groups)
{
    uint64_t n = 0;
    for(const auto &ref : layout)
    {
        if(groups.data() + ref.group == this)tiles[ref.index].id_offset = n;
        n += groups[ref.group].tiles[ref.index].children_count;
    }
    next_id += n;

    for(Creature *ptr = del_queue; ptr;)
    {
        Creature *cr = ptr;  ptr = ptr->next;  delete cr;
    }

    for(auto &tile : tiles)
    {
        size_t n = tile.foods.size();
        for(int i = 0; i < tile.ref_count; i++)
        {
            const auto &ref = tile.refs[i];
            n += groups[ref.group].buffers[ref.index].foods.size();
        }
        tile.foods.reserve(n);

        Creature *first_child = tile.first, **last_child = tile.last;
        for(Creature *cr = first_child; cr; cr = cr->next)cr->id += tile.id_offset;

        tile.last = &tile.first;
        for(int i = 0; i < tile.ref_count; i++)
        {
            const auto &ref = tile.refs[i];
            auto &buf = groups[ref.group].buffers[ref.index];

            tile.foods.insert(tile.foods.end(), buf.foods.begin(), buf.foods.end());
            tile.food_count += buf.food_count;

            if(!buf.creature_count)continue;
            *tile.last = buf.first;  tile.last = buf.last;
            tile.creature_count += buf.creature_count;
            tile.attack_count += buf.attack_count;
        }
        if(first_child)
        {
            *tile.last = first_child;  tile.last = last_child;
        }
        *tile.last = nullptr;
    }
}


void TileGroup::Tile::process_detectors(const Config &config,
    const std::vector<TileGroup> &groups, const Reference &ref)
{
    const Tile &tile = groups[ref.group].tiles[ref.index];

    for(Creature *cr = first; cr; cr = cr->next)
    {
        cr->process_food(tile.foods);
        for(const Creature *tg = tile.first; tg; tg = tg->next)
            if(tg != cr)cr->process_detectors(tg);
    }

    for(const Creature *tg = tile.first; tg; tg = tg->next)
        if(tg->flags & Creature::f_eating)tg->eat_food(foods);

    for(size_t i = spawn_start; i < foods.size(); i++)if(foods[i].type == Food::sprout)
        foods[i].check_grass(config, tile.foods.data(), tile.spawn_start);
}

void TileGroup::process_detectors(const Config &config,
    const std::vector<Reference> &layout, const std::vector<TileGroup> &groups)
{
    for(auto &tile : tiles)
    {
        uint32_t x = tile.x, y = tile.y;
        uint32_t x1 = (x + 1) & config.mask_x, xm = (x - 1) & config.mask_x;
        uint32_t y1 = (y + 1) & config.mask_y, ym = (y - 1) & config.mask_y;

        for(Creature *cr = tile.first; cr; cr = cr->next)cr->pre_process(config);
        tile.process_detectors(config, groups, layout[xm | (ym << config.order_x)]);
        tile.process_detectors(config, groups, layout[x  | (ym << config.order_x)]);
        tile.process_detectors(config, groups, layout[x1 | (ym << config.order_x)]);
        tile.process_detectors(config, groups, layout[xm | (y  << config.order_x)]);
        tile.process_detectors(config, groups, layout[x  | (y  << config.order_x)]);
        tile.process_detectors(config, groups, layout[x1 | (y  << config.order_x)]);
        tile.process_detectors(config, groups, layout[xm | (y1 << config.order_x)]);
        tile.process_detectors(config, groups, layout[x  | (y1 << config.order_x)]);
        tile.process_detectors(config, groups, layout[x1 | (y1 << config.order_x)]);
        for(Creature *cr = tile.first; cr; cr = cr->next)cr->post_process(config);

        for(auto &food : tile.foods)if(food.eater.target)
            food.eater.target->food_energy += config.food_energy;
    }
}


void TileGroup::Tile::update(const Config &config, uint64_t id, const Creature *&sel,
    FoodData *food_buf, CreatureData *creature_buf, SectorData *attack_buf) const
{
    FoodData *food_ptr = food_buf;
    for(const auto &food : foods)if(food.type > Food::sprout)
        (food_ptr++)->set(config, food);
    assert(food_ptr == food_buf + food_count);

    CreatureData *creature_ptr = creature_buf;
    SectorData *attack_ptr = attack_buf;
    for(const Creature *cr = first; cr; cr = cr->next)
    {
        if(cr->id == id)sel = cr;
        (creature_ptr++)->set(config, *cr);
        for(auto &claw : cr->claws)if(claw.active)
            (attack_ptr++)->set(config, *cr, claw);
    }
    assert(creature_ptr == creature_buf + creature_count);
    assert(attack_ptr == attack_buf + attack_count);
}

const Creature *TileGroup::update(const Config &config, uint64_t id,
    FoodData *food_buf, const std::vector<size_t> &food_offs,
    CreatureData *creature_buf, const std::vector<size_t> &creature_offs,
    SectorData *attack_buf, const std::vector<size_t> &attack_offs) const
{
    const Creature *sel = nullptr;
    for(auto &tile : tiles)
    {
        uint32_t index = tile.x | (tile.y << config.order_x);
        assert(food_offs[index + 1] - food_offs[index] == tile.food_count);
        assert(creature_offs[index + 1] - creature_offs[index] == tile.creature_count);
        assert(attack_offs[index + 1] - attack_offs[index] == tile.attack_count);

        tile.update(config, id, sel, food_buf + food_offs[index],
            creature_buf + creature_offs[index], attack_buf + attack_offs[index]);
    }
    return sel;
}


bool TileGroup::Tile::hit_test(const Position pos, uint64_t max_r2, const Creature *&sel, uint64_t prev_id) const
{
    for(const Creature *cr = first; cr; cr = cr->next)
    {
        int32_t dx = cr->pos.x - pos.x;
        int32_t dy = cr->pos.y - pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        if(r2 >= max_r2)continue;

        if(cr->id == prev_id && sel)return true;  sel = cr;
    }
    return false;
}


void TileGroup::thread_proc(Context *context, uint32_t index)
{
    uint32_t stage = context->groups.size();
    TileGroup &group = context->groups[index];
    for(Context::Command cmd = context->first_wait(stage);;)switch(cmd)
    {
    case Context::c_step:
        group.execute_step(context->config);  context->barrier(stage);
        group.consolidate(context->layout, context->groups);  context->barrier(stage);
        group.process_detectors(context->config, context->layout, context->groups);
        cmd = context->end_step(stage);  continue;

    case Context::c_draw:
        {
            const Creature *sel = group.update(context->config, context->sel_id,
                context->food_buf, context->food_offs,
                context->creature_buf, context->creature_offs,
                context->attack_buf, context->attack_offs);
            cmd = context->end_draw(stage, sel);  continue;
        }

    default:
        return;
    }
}


bool TileGroup::Tile::load(const Config &config, InStream &stream, uint64_t next_id, uint64_t *buf)
{
    assert(foods.empty() && !first);
    uint64_t offs_x = uint64_t(x) << tile_order;
    uint64_t offs_y = uint64_t(y) << tile_order;

    stream.assert_align(8);
    stream >> rand >> spawn_start >> creature_count;
    if(!stream)return false;  // TODO: check counts

    foods.resize(spawn_start);  food_count = 0;
    for(auto &food : foods)
    {
        if(!food.load(config, stream, offs_x, offs_y))return false;
        if(food.type > Food::sprout)food_count++;
    }

    attack_count = 0;
    for(uint32_t i = 0; i < creature_count; i++)
    {
        Creature *cr = Creature::load(config, stream, next_id, buf);
        if(!cr)
        {
            *last = nullptr;  return false;
        }
        *last = cr;  last = &cr->next;
        cr->pos.x |= offs_x;  cr->pos.y |= offs_y;
        attack_count += cr->attack_count;
    }
    *last = nullptr;  return true;
}

void TileGroup::Tile::save(OutStream &stream, uint64_t *buf) const
{
    stream.assert_align(8);  stream << rand;

    uint32_t n = 0;
    for(auto &food : foods)if(food.type)n++;
    stream << n << creature_count;

    for(auto &food : foods)if(food.type)stream << food;
    for(Creature *cr = first; cr; cr = cr->next)cr->save(stream, buf);
}



// Context struct

void Context::start()
{
    stage = 0;  cmd = c_stop;
}

void Context::pre_execute()
{
    std::unique_lock<std::mutex> lock(mutex);
    while(cmd)cond_cmd.wait(lock);  lock.release();
}

void Context::post_execute(Command new_cmd)
{
    std::unique_lock<std::mutex> lock(mutex, std::adopt_lock);
    if((cmd = new_cmd))cond_work.notify_all();
}

void Context::execute(Command new_cmd)
{
    std::unique_lock<std::mutex> lock(mutex);
    while(cmd)cond_cmd.wait(lock);  cmd = new_cmd;
    if(new_cmd)cond_work.notify_all();
}


Context::Command Context::first_wait(uint32_t &target)
{
    uint32_t n = groups.size();
    std::unique_lock<std::mutex> lock(mutex);
    if(++stage == target)
    {
        cmd = c_none;  cond_cmd.notify_one();
    }
    else while(stage - target >= n)cond_work.wait(lock);
    while(!cmd)cond_work.wait(lock);
    target += n;  return cmd;
}

void Context::barrier(uint32_t &target)
{
    uint32_t n = groups.size();
    std::unique_lock<std::mutex> lock(mutex);
    if(++stage == target)cond_work.notify_all();
    else while(stage - target >= n)cond_work.wait(lock);
    target += n;
}

Context::Command Context::end_step(uint32_t &target)
{
    uint32_t n = groups.size();
    std::unique_lock<std::mutex> lock(mutex);
    assert(cmd == c_step);
    if(++stage == target)
    {
        current_time++;  cmd = c_none;
        cond_cmd.notify_one();
    }
    else while(stage - target >= n)cond_work.wait(lock);
    while(!cmd)cond_work.wait(lock);
    target += n;  return cmd;
}

Context::Command Context::end_draw(uint32_t &target, const Creature *cr)
{
    uint32_t n = groups.size();
    std::unique_lock<std::mutex> lock(mutex);
    assert(cmd == c_draw);  if(cr)sel = cr;
    if(++stage == target)
    {
        cmd = c_none;  cond_cmd.notify_one();
    }
    else while(stage - target >= n)cond_work.wait(lock);
    while(!cmd)cond_work.wait(lock);
    target += n;  return cmd;
}



// World struct

const char version_string[] = "Evol0004";


World::World(uint32_t group_count) : group_count(group_count)
{
}

World::~World()
{
    if(!threads.empty())stop();
}


void World::init()
{
    config.order_x = config.order_y = 6;  // 64 x 64
    config.base_radius = tile_size / 64;

    config.chromosome_bits = 4;  // 16 = 8 pair
    config.genome_split_factor = ~(uint32_t(-1) / 1024);
    config.chromosome_replace_factor = ~(uint32_t(-1) / 64);
    config.chromosome_copy_prob = ~(uint32_t(-1) / 1024);
    config.bit_mutate_factor = ~(uint32_t(-1) / 1024);

    config.slot_bits = 6;  // 64 slots
    config.base_bits = 8;

    uint64_t e = uint64_t(1) << 24, t = e >> 10;
    config.base_cost = {16 * e, 16 * t};
    config.gene_cost = {e >> 6, t >> 6};

    config.cost[Slot::womb]    = {e, 0};
    config.cost[Slot::claw]    = {e, 0};
    config.cost[Slot::leg]     = {e, 0};
    config.cost[Slot::rotator] = {e, 0};
    config.cost[Slot::mouth]   = {e, 0};
    config.cost[Slot::signal]  = {e, 0};

    config.cost[Slot::stomach] = {e, t};
    config.cost[Slot::hide]    = {e, t};
    config.cost[Slot::eye]     = {e, t};
    config.cost[Slot::radar]   = {e, t};

    config.cost[Slot::link]    = {0, 0};

    config.spawn_mul = e;
    config.capacity_mul = e;
    config.hide_mul = e;
    config.damage_mul = 256;
    config.life_mul = 1ul << 16;
    config.life_regen = 64;
    config.eating_cost = 8 * t;
    config.signal_cost = 8 * t;
    config.speed_mul = tile_size >> 14;
    config.rotate_mul = 8 * config.speed_mul;
    config.mass_order = 2 * tile_order - 38;

    config.food_energy = 8 * e;
    config.exp_sprout_per_tile  = ~(uint32_t(-1) / 1024);
    config.exp_sprout_per_grass = ~(uint32_t(-1) / 256);
    config.repression_range = tile_size / 32;
    config.sprout_dist_x4 = 5 * config.repression_range;
    config.meat_dist_x4 = tile_size / 16;

    bool res = config.calc_derived();
    assert(res);  (void)res;


    uint64_t seed = 1234;
    uint32_t exp_grass_gen    = uint32_t(-1) >> 8;
    uint32_t exp_creature_gen = uint32_t(-1) >> 4;
    int grass_gen_mul = 16;


    build_layout();
    Genome init_genome(config);  uint64_t next_id = 0;
    for(size_t i = 0; i < layout.size(); i++)
    {
        Tile &tile = groups[layout[i].group].tiles[layout[i].index];
        tile.rand = Random(seed, i);

        uint64_t offs_x = uint64_t(tile.x) << tile_order;
        uint64_t offs_y = uint64_t(tile.y) << tile_order;

        uint32_t n = 0;
        for(int k = 0; k < grass_gen_mul; k++)
            n += tile.rand.poisson(exp_grass_gen);
        for(uint32_t k = 0; k < n; k++)
        {
            uint64_t xx = (tile.rand.uint32() & tile_mask) | offs_x;
            uint64_t yy = (tile.rand.uint32() & tile_mask) | offs_y;
            tile.foods.emplace_back(config, Food::grass, Position{xx, yy});
        }
        tile.spawn_start = tile.food_count = n;

        n = tile.rand.poisson(exp_creature_gen);
        for(uint32_t k = 0; k < n; k++)
        {
            angle_t angle = tile.rand.uint32();
            uint64_t xx = (tile.rand.uint32() & tile_mask) | offs_x;
            uint64_t yy = (tile.rand.uint32() & tile_mask) | offs_y;
            Genome genome(config, tile.rand, init_genome, nullptr);
            Creature *cr = Creature::spawn(config, genome,
                next_id++, Position{xx, yy}, angle, uint64_t(-1));
            *tile.last = cr;  tile.last = &cr->next;
        }
        *tile.last = nullptr;  tile.creature_count = n;  tile.attack_count = 0;
    }
    for(auto &group : groups)
    {
        group.next_id = next_id;
        group.process_detectors(config, layout, groups);
    }
    current_time = 0;
}

void World::build_layout()
{
    TileLayout scheme(config.mask_x + 1, config.mask_y + 1, group_count);
    scheme.build_layout();

    groups.resize(group_count);
    for(uint32_t i = 0; i < group_count; i++)groups[i].alloc(scheme.groups[i]);

    layout.resize(scheme.tiles.size());
    for(size_t i = 0; i < layout.size(); i++)
    {
        layout[i] = scheme.tiles[i];
        Tile &tile = groups[layout[i].group].tiles[layout[i].index];
        tile.init(scheme.tiles[i]);

        tile.x = i & config.mask_x;
        tile.y = i >> config.order_x;
    }

    food_offs.resize(layout.size() + 1);
    creature_offs.resize(layout.size() + 1);
    attack_offs.resize(layout.size() + 1);
}


void World::start()
{
    assert(threads.empty());

    Context::start();
    threads.reserve(group_count);
    for(uint32_t i = 0; i < group_count; i++)
        threads.emplace_back(TileGroup::thread_proc, this, i);
    pre_execute();
}

void World::next_step()
{
    post_execute(c_step);  pre_execute();
}

void World::stop()
{
    post_execute(c_stop);
    for(auto &thread : threads)thread.join();
    threads.clear();
}


void World::count_objects()
{
    food_offs[0] = creature_offs[0] = attack_offs[0] = 0;
    size_t food_count = 0, creature_count = 0, attack_count = 0;
    for(size_t i = 0; i < layout.size(); i++)
    {
        Tile &tile = groups[layout[i].group].tiles[layout[i].index];

        food_offs[i + 1] = food_count += tile.food_count;
        creature_offs[i + 1] = creature_count += tile.creature_count;
        attack_offs[i + 1] = attack_count += tile.attack_count;
    }
}

const Creature *World::update(FoodData *food_buf, CreatureData *creature_buf, SectorData *attack_buf, uint64_t sel_id)
{
    // count_objects() should be called prior

    sel = nullptr;
    Context::sel_id = sel_id;
    Context::food_buf = food_buf;
    Context::creature_buf = creature_buf;
    Context::attack_buf = attack_buf;

    post_execute(c_draw);  pre_execute();  return sel;
}


const Creature *World::hit_test(const Position &pos, uint32_t rad, uint64_t prev_id) const
{
    uint32_t x1 = (pos.x - rad) >> tile_order & config.mask_x;
    uint32_t y1 = (pos.y - rad) >> tile_order & config.mask_y;
    uint32_t x2 = (pos.x + rad) >> tile_order & config.mask_x;
    uint32_t y2 = (pos.y + rad) >> tile_order & config.mask_y;

    uint64_t r2 = uint64_t(rad) * rad;
    bool test[] = {true, x1 != x2, y1 != y2, x1 != x2 && y1 != y2};  const Creature *sel = nullptr;
    if(test[0] && get_tile(x1 | (y1 << config.order_x)).hit_test(pos, r2, sel, prev_id))return sel;
    if(test[1] && get_tile(x2 | (y1 << config.order_x)).hit_test(pos, r2, sel, prev_id))return sel;
    if(test[2] && get_tile(x1 | (y2 << config.order_x)).hit_test(pos, r2, sel, prev_id))return sel;
    if(test[3] && get_tile(x2 | (y2 << config.order_x)).hit_test(pos, r2, sel, prev_id))return sel;
    return sel;
}


bool World::load(InStream &stream)
{
    stream.assert_align(8);  char header[8];  stream.get(header, 8);
    if(!stream || std::memcmp(header, version_string, sizeof(header)))return false;
    uint64_t next_id;  stream >> config >> align(8) >> current_time >> next_id;
    if(!stream)return false;

    build_layout();
    std::vector<uint64_t> buf(std::max<uint32_t>(1, config.slot_bits >> 6));
    for(size_t i = 0; i < layout.size(); i++)
    {
        Tile &tile = groups[layout[i].group].tiles[layout[i].index];
        if(!tile.load(config, stream, next_id, buf.data()))return false;
    }
    for(auto &group : groups)
    {
        group.next_id = next_id;
        group.process_detectors(config, layout, groups);
    }
    return true;
}

void World::save(OutStream &stream) const
{
    stream.assert_align(8);  stream.put(version_string, 8);
    stream << config << align(8) << current_time << groups[0].next_id;
    std::vector<uint64_t> buf(std::max<uint32_t>(1, config.slot_bits >> 6));
    for(size_t i = 0; i < layout.size(); i++)
    {
        const Tile &tile = groups[layout[i].group].tiles[layout[i].index];
        tile.save(stream, buf.data());
    }
}
