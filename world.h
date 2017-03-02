// world.h : world representation
//

#pragma once

#include "math.h"
#include <vector>



namespace Slot
{
    enum Type : uint8_t
    {
        Link    =  0,  // link
        Mouth   =  1,  // out
        Stomach =  2,  // in
        Womb    =  3,  // out
        Eye     =  4,  // in
        Radar   =  5,  // in
        Claw    =  6,  // out
        Hide    =  7,  // in
        Leg     =  8,  // out
        Rotator =  9,  // out
        Signal  = 10,  // out
        Invalid = 11
    };
}

constexpr uint8_t slot_type_bits = 4;
constexpr uint8_t flag_bits = 4;

typedef uint8_t slot_t;


struct Config
{
    struct SlotCost
    {
        uint32_t initial, per_tick;
    };

    uint8_t order_x, order_y;
    uint32_t base_radius;

    uint8_t chromosome_bits;
    uint32_t genome_split_factor;
    uint32_t chromosome_replace_factor;
    uint32_t chromosome_copy_prob;
    uint32_t bit_mutate_factor;
    uint8_t slot_bits, base_bits;
    uint32_t gene_init_cost, gene_pass_rate;

    SlotCost cost[Slot::Invalid];
    uint32_t spawn_mul, capacity_mul, hide_mul;
    uint32_t damage_mul, life_mul, life_regen;
    uint32_t eating_cost, signal_cost;
    uint32_t speed_mul, rotate_mul;
    uint8_t mass_order;

    uint32_t food_energy;
    uint32_t exp_sprout_per_tile;
    uint32_t exp_sprout_per_grass;
    uint32_t repression_range;
    uint32_t sprout_dist_x4;
    uint32_t meat_dist_x4;

    // derived
    uint32_t mask_x, mask_y;
    uint64_t full_mask_x, full_mask_y;
    uint64_t base_r2, repression_r2;

    void calc_derived();
};


struct Creature;

struct Detector
{
    uint64_t min_r2, id;
    Creature *target;

    explicit Detector(uint64_t r2);
    void reset(uint64_t r2);
    void update(uint64_t r2, Creature *cr);
};


struct Position
{
    uint64_t x, y;
};

struct Food
{
    enum Type
    {
        Dead, Sprout, Grass, Meat
    };

    Type type;
    Position pos;
    Detector eater;

    Food(const Config &config, Type type, const Position &pos);
    Food(const Config &config, const Food &food);

    void check_grass(const Config &config, const Food *food, size_t n);
};


struct Genome
{
    struct Gene
    {
        uint64_t data;

        Gene(const Config &config, uint32_t slot, Slot::Type type,
            uint32_t base, angle_t angle1, angle_t angle2, uint32_t radius, uint8_t flags);
        Gene(const Config &config, uint32_t slot, int32_t weight, uint32_t source, uint8_t offset);

        uint32_t take_bits(int n)
        {
            uint32_t res = data >> (64 - n);  data <<= n;  return res;
        }

        int32_t take_bits_signed(int n)
        {
            int32_t res = int64_t(data) >> (64 - n);  data <<= n;  return res;
        }

        bool operator < (const Gene &cmp) const
        {
            return data < cmp.data;
        }
    };

    std::vector<uint32_t> chromosomes;
    std::vector<Gene> genes;

    explicit Genome(const Config &config);
    Genome(const Config &config, Random &rand, const Genome &parent, const Genome *father);
};


struct GenomeProcessor
{
    struct LinkData
    {
        int32_t weight;
        uint32_t source;

        LinkData(int32_t weight, uint32_t source) : weight(weight), source(source)
        {
        }
    };

    enum NeiroState : uint8_t
    {
        s_normal, s_input, s_always_off, s_always_on
    };

    struct SlotData
    {
        uint32_t link_start, link_count;
        int32_t act_level, min_level, max_level;
        NeiroState neiro_state;  bool used;

        Slot::Type type;
        uint32_t base, radius;
        angle_t angle1, angle2;
        uint8_t flags;

        SlotData(uint32_t link_start, uint32_t link_count, int32_t act_level, int32_t min_level, int32_t max_level) :
            link_start(link_start), link_count(link_count), act_level(act_level), min_level(min_level), max_level(max_level),
            neiro_state(s_normal), used(false), type(Slot::Invalid)
        {
        }
    };

    enum UseFlags
    {
        f_base   = 1 << 0,
        f_radius = 1 << 1,
        f_angle1 = 1 << 2,
        f_angle2 = 1 << 3,
        f_vision = 1 << 4,
        f_signal = 1 << 5,
        f_useful = 1 << 6,
        f_output = 1 << 7
    };


    uint32_t slot_count;
    uint32_t link_start, link_count, core_count;
    int32_t act_level, min_level, max_level;
    uint32_t type_and, type_or;

    uint32_t base, radius;
    int32_t angle1_x, angle1_y;
    int32_t angle2_x, angle2_y;
    uint8_t flags;

    std::vector<LinkData> links;
    std::vector<SlotData> slots;
    uint32_t working_links;


    void reset();
    bool update(SlotData &slot, int use);
    bool update(SlotData &slot);
    void append_slot();

    GenomeProcessor(const Config &config, uint32_t max_links);
    void process(const Config &config, Genome::Gene gene);
    void finalize();
};


struct Creature
{
    enum Flags : uint8_t
    {
        f_eating   = 1 << 0,
        f_creature = 1 << 1,
        f_grass    = 1 << 2,  // 1 << Food::Grass
        f_meat     = 1 << 3,  // 1 << Food::Meat
        f_signal1  = 1 << 4,
        f_signal2  = 1 << 5,
        f_signal3  = 1 << 6,

        f_signals = f_signal1 | f_signal2 | f_signal3,
        f_visible = f_creature | f_grass | f_meat | f_signals
    };


    struct Womb
    {
        uint32_t energy;
        bool active;

        Womb(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Claw
    {
        uint64_t rad_sqr;
        angle_t angle, delta;
        uint32_t damage, act_cost;
        bool active;

        Claw(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Leg
    {
        uint32_t dist_x4;
        angle_t angle;

        Leg(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Signal
    {
        uint8_t flags;
        uint32_t act_cost;

        Signal(const Config &config, const GenomeProcessor::SlotData &slot);
    };


    struct Stomach
    {
        uint32_t capacity, mul;

        Stomach(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Hide
    {
        uint32_t life, max_life, regen, mul;

        Hide(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Eye
    {
        uint64_t rad_sqr;
        angle_t angle, delta;
        uint8_t flags;
        uint32_t count;

        Eye(const Config &config, const GenomeProcessor::SlotData &slot);
    };

    struct Radar
    {
        angle_t angle, delta;
        uint8_t flags;
        uint64_t min_r2;

        Radar(const Config &config, const GenomeProcessor::SlotData &slot);
    };


    struct Neiron
    {
        int32_t act_level;
        int32_t level;
    };

    struct Link
    {
        slot_t input, output;
        int8_t weight;

        Link(slot_t input, slot_t output, int8_t weight) : input(input), output(output), weight(weight)
        {
        }
    };


    uint64_t id;
    Genome genome;

    Position pos;
    angle_t angle;
    uint32_t passive_energy;
    uint32_t energy, max_energy, passive_cost;
    uint32_t total_life, max_life, damage;
    Detector father;
    uint8_t flags;

    std::vector<Womb> wombs;
    std::vector<Claw> claws;
    std::vector<Leg> legs;
    std::vector<angle_t> rotators;
    std::vector<Signal> signals;

    std::vector<Stomach> stomachs;
    std::vector<Hide> hides;
    std::vector<Eye> eyes;
    std::vector<Radar> radars;

    std::vector<uint8_t> input;
    std::vector<Neiron> neirons;
    std::vector<Link> links;

    Creature *next;


    Creature() = delete;
    Creature(const Creature &) = delete;
    Creature &operator = (const Creature &) = delete;

    Slot::Type append_slot(const Config &config, const GenomeProcessor::SlotData &slot);
    Creature(const Config &config, Random &rand, uint64_t id,
        const Position &pos, angle_t angle, uint32_t spawn_energy, const Genome &parent, const Genome *father);
    static Creature *spawn(const Config &config, Random &rand, uint64_t id,
        const Position &pos, angle_t angle, uint32_t spawn_energy, const Creature &parent);

    void pre_process(const Config &config);
    void update_view(uint8_t flags, uint64_t r2, angle_t test);
    void process_detectors(Creature *cr, uint64_t r2, angle_t dir);
    static void process_detectors(Creature *cr1, Creature *cr2);
    void process_food(std::vector<Food> &foods);
    void post_process();

    uint32_t execute_step(const Config &config);
};


struct World
{
    struct Tile
    {
        Random rand;
        std::vector<Food> foods;
        Creature *first, **last;
        size_t spawn_start;

        Tile(uint64_t seed, size_t index);
        Tile(const Config &config, const Tile &old, size_t &total_food, size_t reserve);
    };


    Config config;
    uint64_t current_time;
    std::vector<Tile> tiles;
    size_t total_food_count, total_creature_count;
    size_t spawn_per_tile;
    uint64_t next_id;


    World();
    ~World();
    size_t tile_index(Position &pos) const;
    void spawn_grass(Tile &tile, uint32_t x, uint32_t y);
    void spawn_meat(Tile &tile, Position pos, uint32_t energy);
    void process_tile_pair(Tile &tile1, Tile &tile2);
    void process_detectors();
    void next_step();
};
