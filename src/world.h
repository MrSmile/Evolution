// world.h : world representation
//

#pragma once

#include "math.h"
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>



namespace Slot
{
    enum Type : uint8_t
    {
        link    =  0,  // link
        mouth   =  1,  // out
        stomach =  2,  // in
        womb    =  3,  // out
        eye     =  4,  // in
        radar   =  5,  // in
        claw    =  6,  // out
        hide    =  7,  // in
        leg     =  8,  // out
        rotator =  9,  // out
        signal  = 10,  // out
        invalid = 11
    };
}

constexpr uint8_t slot_type_bits = 4;
constexpr uint8_t flag_bits = 6;

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
    uint8_t slot_bits, base_bits;
    uint32_t genome_split_factor;
    uint32_t chromosome_replace_factor;
    uint32_t chromosome_copy_prob;
    uint32_t bit_mutate_factor;

    SlotCost base_cost, gene_cost, cost[Slot::invalid];
    uint32_t eating_cost, signal_cost;

    uint32_t spawn_mul, capacity_mul, hide_mul;
    uint32_t damage_mul, life_mul, life_regen;
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

    bool calc_derived();
    bool load(InStream &stream);
    void save(OutStream &stream) const;
};


struct Creature;

struct Detector
{
    uint64_t min_r2, id;
    const Creature *target;

    Detector() = default;
    explicit Detector(uint64_t r2);
    void reset(uint64_t r2);
    void update(uint64_t r2, const Creature *cr);
};


struct Position
{
    uint64_t x, y;
};

struct Food
{
    enum Type
    {
        dead, sprout, grass, meat
    };

    Type type;
    Position pos;
    Detector eater;

    Food() = default;
    Food(const Config &config, Type type, const Position &pos);
    Food(const Config &config, const Food &food);
    void set(const Config &config, const Food &food);

    void check_grass(const Config &config, const Food *food, size_t n);

    bool load(const Config &config, InStream &stream, uint64_t offs_x, uint64_t offs_y);
    void save(OutStream &stream) const;
};


struct Genome
{
    struct Gene
    {
        uint64_t data;

        Gene() = default;
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

    Genome() = default;
    explicit Genome(const Config &config);
    Genome(const Config &config, Random &rand, const Genome &parent, const Genome *father);

    bool load(const Config &config, InStream &stream);
    void save(OutStream &stream) const;
};


class GenomeProcessor
{
public:
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
    };

private:
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

    class State
    {
        uint32_t link_start, link_count, core_count;
        int32_t act_level, min_level, max_level;
        uint32_t type_and, type_or;

        uint32_t base, radius;
        int32_t angle1_x, angle1_y;
        int32_t angle2_x, angle2_y;
        uint8_t flags;

        void reset(size_t link_pos);
        bool update(SlotData &slot, int use);
        bool update(SlotData &slot);

    public:
        State()
        {
            reset(0);
        }

        void create_slot(SlotData &slot, size_t link_pos);
        void process_gene(const Config &config, Genome::Gene &gene, std::vector<LinkData> &links);
    };


    void update(const Config &config, const Genome &genome);
    void finalize();


public:
    uint32_t working_links;
    std::vector<SlotData> slots;
    std::vector<LinkData> links;
    Config::SlotCost passive_cost;
    uint32_t max_energy, max_life;
    uint32_t count[Slot::invalid];


    GenomeProcessor() = default;

    GenomeProcessor(const Config &config, const Genome &genome)
    {
        process(config, genome);
    }

    void process(const Config &config, const Genome &genome);
};


struct Creature
{
    enum Flags : uint8_t
    {
        f_signal1  = 1 << 0,
        f_signal2  = 1 << 1,
        f_signal3  = 1 << 2,
        f_creature = 1 << 3,
        f_grass    = 1 << 4,
        f_meat     = 1 << 5,
        f_eating   = 1 << 6,

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
    mutable std::atomic<uint32_t> food_energy;
    uint32_t energy, max_energy, passive_cost;
    uint32_t total_life, max_life, damage;
    uint64_t creature_vis_r2[f_creature];
    uint64_t food_vis_r2[2], claw_r2;
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

    std::vector<slot_t> order;
    std::vector<uint8_t> input;
    std::vector<Neiron> neirons;
    std::vector<Link> links;

    Creature *next;


    Creature() = delete;
    Creature(const Creature &) = delete;
    Creature &operator = (const Creature &) = delete;

    void update_max_visibility(uint8_t vis_flags, uint64_t r2);
    Slot::Type append_slot(const Config &config, const GenomeProcessor::SlotData &slot);
    static void calc_mapping(const GenomeProcessor &proc, std::vector<uint32_t> &mapping);
    Creature(const Config &config, Genome &genome, const GenomeProcessor &proc,
        uint64_t id, const Position &pos, angle_t angle, uint32_t spawn_energy);
    static Creature *spawn(const Config &config, Genome &genome,
        uint64_t id, const Position &pos, angle_t angle, uint32_t spawn_energy);
    static Creature *spawn(const Config &config, Random &rand, const Creature &parent,
        uint64_t id, const Position &pos, angle_t angle, uint32_t spawn_energy);

    void pre_process(const Config &config);
    void update_view(uint8_t tg_flags, uint64_t r2, angle_t dir);
    void update_damage(const Creature *cr, uint64_t r2, angle_t dir);
    void process_food(const std::vector<Food> &foods);
    void eat_food(std::vector<Food> &foods) const;
    void process_detectors(const Creature *cr);
    void post_process();

    uint32_t execute_step(const Config &config);

    static Creature *load(const Config &config, InStream &stream, uint64_t next_id, uint64_t *buf);
    bool load(InStream &stream, uint32_t load_energy, uint64_t *buf);
    void save(OutStream &stream, uint64_t *buf) const;
};


struct TileLayout
{
    struct Reference
    {
        uint32_t group, index;
    };

    struct TileDesc : public Reference
    {
        uint32_t neighbors[9];
        Reference refs[9];
        int ref_count;
    };

    struct GroupDesc
    {
        uint32_t tile_count, ref_count;

        GroupDesc() : tile_count(0), ref_count(0)
        {
        }
    };

    struct Offsets
    {
        uint32_t pos[3];
        int8_t offs[3];

        Offsets(uint32_t p1, int8_t n1, uint32_t p2, int8_t n2, uint32_t p3, int8_t n3) :
            pos{p1, p2, p3}, offs{n1, n2, n3}
        {
        }

        Offsets(uint32_t center, uint32_t step, int8_t n) :
            pos{center - step, center, center + step}, offs{n, 0, int8_t(-n)}
        {
        }
    };

    uint32_t size_x, size_y;
    std::vector<TileDesc> tiles;
    std::vector<GroupDesc> groups;

    TileLayout(uint32_t size_x, uint32_t size_y, uint32_t group_count);
    void process_tile(TileDesc &cur, const Offsets &offs_x, const Offsets &offs_y);
    void process_line(uint32_t pos, const Offsets &offs_y);
    void build_layout();
};


struct FoodData;
struct CreatureData;
struct Context;

struct TileGroup
{
    typedef TileLayout::Reference Reference;

    struct TileBuffer
    {
        std::vector<Food> foods;
        Creature *first, **last;
        uint32_t food_count, creature_count;

        void append(Creature *cr)
        {
            *last = cr;  last = &cr->next;  creature_count++;
        }
    };

    struct Tile : public TileBuffer
    {
        uint32_t x, y;
        uint32_t neighbors[9];
        Reference refs[9];
        int ref_count;

        Random rand;
        uint32_t spawn_start;
        uint32_t children_count;
        uint64_t id_offset;  // TODO: memory layout

        Tile();
        ~Tile();
        void init(const TileLayout::TileDesc &desc);

        void process_detectors(const Config &config,
            const std::vector<TileGroup> &groups, const Reference &ref);
        void update(const Config &config, uint64_t id, const Creature *&sel,
            FoodData *food_buf, CreatureData *creature_buf) const;
        bool hit_test(const Position pos, uint64_t max_r2, const Creature *&sel, uint64_t prev_id) const;

        bool load(const Config &config, InStream &stream, uint64_t next_id, uint64_t *buf);
        void save(OutStream &stream, uint64_t *buf) const;
    };


    uint64_t next_id;
    std::vector<Tile> tiles;
    std::vector<TileBuffer> buffers;


    void alloc(const TileLayout::GroupDesc &desc);

    uint32_t neighbor_index(const Config &config, const Tile &tile, Position &pos);
    void spawn_grass(const Config &config, Tile &tile);
    void spawn_meat(const Config &config, Tile &tile, Position pos, uint32_t energy);

    void execute_step(const Config &config);
    void consolidate(const std::vector<Reference> &layout, std::vector<TileGroup> &groups);
    void process_detectors(const Config &config,
        const std::vector<Reference> &layout, const std::vector<TileGroup> &groups);

    const Creature *update(const Config &config, uint64_t id,
        FoodData *food_buf, const std::vector<size_t> &food_offs,
        CreatureData *creature_buf, const std::vector<size_t> &creature_offs) const;

    static void thread_proc(Context *context, uint32_t index);
};


struct Context
{
    typedef TileLayout::Reference Reference;

    enum Command
    {
        c_none, c_step, c_draw, c_stop
    };

    Config config;
    std::vector<Reference> layout;
    std::vector<TileGroup> groups;

    FoodData *food_buf;
    CreatureData *creature_buf;
    std::vector<size_t> food_offs, creature_offs;
    uint64_t current_time, sel_id;
    const Creature *sel;

    std::mutex mutex;
    std::condition_variable cond_cmd, cond_work;
    uint32_t stage;  Command cmd;

    void start();
    void pre_execute();
    void post_execute(Command new_cmd);
    void execute(Command new_cmd);

    Command first_wait(uint32_t &target);
    void barrier(uint32_t &target);
    Command end_step(uint32_t &target);
    Command end_draw(uint32_t &target, const Creature *cr);
};


struct World : public Context
{
    typedef TileLayout::Reference Reference;
    typedef TileGroup::Tile Tile;


    uint32_t group_count;
    std::vector<std::thread> threads;


    World(uint32_t group_count);
    ~World();

    void init();
    void build_layout();

    void start();
    void next_step();
    void stop();

    void count_objects();
    const Creature *update(FoodData *food_buf, CreatureData *creature_buf, uint64_t sel_id);
    const Creature *hit_test(const Position &pos, uint32_t rad, uint64_t prev_id) const;

    bool load(InStream &stream);
    void save(OutStream &stream) const;

    size_t food_total() const
    {
        return *food_offs.rbegin();
    }

    size_t creature_total() const
    {
        return *creature_offs.rbegin();
    }

    const Tile &get_tile(uint32_t index) const
    {
        return groups[layout[index].group].tiles[layout[index].index];
    }
};
