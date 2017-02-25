// world.h : world representation
//

#pragma once

#include "math.h"
#include <vector>

using std::size_t;



typedef uint8_t slot_t;

struct Position
{
    uint64_t x, y;
};

struct Config
{
    uint8_t order_x, order_y;
    uint32_t base_radius;
    uint32_t food_energy;
    uint8_t input_level;
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

    void reset(uint64_t r2);
    void update(uint64_t r2, Creature *cr);
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


enum class Slot : uint8_t
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
};

struct Genome
{
    // TODO

    Genome(const Genome &parent, const Genome *father);
};

struct Creature
{
    enum Flags : uint8_t
    {
        f_eating = 1 << 0,
        f_grass  = 1 << 2,  // 1 << Food::Grass
        f_meat   = 1 << 3,  // 1 << Food::Meat
    };


    struct Womb
    {
        uint32_t energy;
        bool active;
    };

    struct Claw
    {
        uint32_t radius;
        angle_t angle, delta;
        uint32_t damage;
        bool active;
    };

    struct Leg
    {
        uint32_t dist_x4;
        angle_t angle;
    };


    struct Stomach
    {
        uint32_t capacity;
        uint32_t mul;  // = ceil((255 << 24) / capacity)
    };

    struct Hide
    {
        uint32_t life, capacity, regen;
        uint32_t mul;  // = ceil((255 << 24) / capacity)
    };

    struct Eye
    {
        uint32_t radius;
        angle_t angle, delta;
        uint8_t flags;
        uint32_t count;
    };

    struct Radar
    {
        angle_t angle, delta;
        uint8_t flags;
        uint64_t min_r2;
    };


    struct Neiron
    {
        uint32_t act_cost;
        int32_t act_level;
        int32_t level;
    };

    struct Link
    {
        slot_t input, output;
        int8_t weight;
    };


    uint64_t id;
    Genome gen;

    Position pos;
    angle_t angle;
    uint32_t energy, max_energy, passive_cost;
    uint32_t passive_energy, damage;
    Detector father;
    uint8_t flags;

    std::vector<Womb> wombs;
    std::vector<Claw> claws;
    std::vector<Leg> legs;
    std::vector<angle_t> rotators;
    std::vector<uint8_t> signals;

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

    Creature(uint64_t id, const Position &pos, angle_t angle, uint32_t energy, const Creature &parent);
    static Creature *spawn(uint64_t id, const Position &pos, angle_t angle, uint32_t energy, const Creature &parent);

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
        ~Tile();
    };


    Config config;
    std::vector<Tile> tiles;
    size_t total_food_count;
    size_t spawn_per_tile;
    uint64_t next_id;


    World();
    size_t tile_index(Position &pos) const;
    void spawn_grass(Tile &tile, uint32_t x, uint32_t y);
    void spawn_meat(Tile &tile, Position pos, uint32_t energy);
    void process_tile_pair(Tile &tile1, Tile &tile2);
    void next_step();
};
