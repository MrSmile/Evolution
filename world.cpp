// world.cpp : world mechanics implementation
//

#include "world.h"
#include <algorithm>
#include <cassert>
#include <cmath>



// Config struct

void Config::calc_derived()
{
    mask_x = (uint32_t(1) << order_x) - 1;
    mask_y = (uint32_t(1) << order_y) - 1;
    full_mask_x = (uint64_t(1) << (order_x + tile_order)) - 1;
    full_mask_y = (uint64_t(1) << (order_x + tile_order)) - 1;;
    base_r2 = uint64_t(base_radius) * base_radius;
    repression_r2 = uint64_t(repression_range) * repression_range;
}



// Detector struct

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

Food::Food(const Config &config, Type type, const Position &pos) : type(type), pos(pos)
{
    eater.reset(config.base_r2);
}

Food::Food(const Config &config, const Food &food) : Food(config, food.type > Sprout ? food.type : Grass, food.pos)
{
}



// Genome struct

Genome::Genome(const Genome &parent, const Genome *father)
{
}



// Creature struct

Creature::Creature(uint64_t id, const Position &pos, angle_t angle, uint32_t energy, const Creature &parent) :
    id(id), gen(parent.gen, parent.father.target ? &parent.father.target->gen : nullptr), pos(pos), angle(angle)
{
    // TODO
}

Creature *Creature::spawn(uint64_t id, const Position &pos, angle_t angle, uint32_t energy, const Creature &parent)
{
    return new Creature(id, pos, angle, energy, parent);  // TODO: handle energy
}


void Creature::pre_process(const Config &config)
{
    father.reset(config.base_r2);
    for(auto &eye : eyes)eye.count = 0;
    for(auto &radar : radars)radar.min_r2 = max_r2;
    damage = 0;
}

void Creature::process_detectors(Creature *cr, uint64_t r2, angle_t dir)
{
    father.update(r2, cr);
    angle_t test = dir - angle;
    for(auto &eye : eyes)if(eye.flags & cr->flags)
    {
        if(angle_t(test - eye.angle) > eye.delta)continue;
        if(r2 >= uint64_t(eye.radius) * eye.radius)continue;
        eye.count++;
    }
    if(!r2)return;  // invalid angle
    for(auto &radar : radars)if(radar.flags & cr->flags)
    {
        if(angle_t(test - radar.angle) > radar.delta)continue;
        radar.min_r2 = std::min(radar.min_r2, r2);
    }
    test = angle_t(dir - cr->angle) ^ flip_angle;
    for(const auto &claw : cr->claws)if(claw.active)
    {
        if(angle_t(test - claw.angle) > claw.delta)continue;
        if(r2 >= uint64_t(claw.radius) * claw.radius)continue;
        damage += claw.damage;  // TODO: overflow
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

void Creature::process_food(Food *food, size_t n)
{
    if(!(flags & f_eating))return;
    for(size_t i = 0; i < n; i++)if(food[i].type > Food::Sprout)
    {
        int32_t dx = pos.x - food[i].pos.x;
        int32_t dy = pos.y - food[i].pos.y;
        uint64_t r2 = int64_t(dx) * dx + int64_t(dy) * dy;
        food[i].eater.update(r2, this);
    }

    // TODO: eyes
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
    flags = 0;
    for(auto &neiron : neirons)neiron.level = 0;
    for(const auto &link : links)
        neirons[link.output].level += link.weight * int16_t(input[link.input]);

    uint32_t cost = passive_cost;
    for(size_t i = 0; i < neirons.size(); i++)
        if(neirons[i].level > neirons[i].act_level)
        {
            input[i] = config.input_level;
            cost += neirons[i].act_cost;
        }
        else input[i] = 0;

    for(size_t i = hides.size() - 1; i != size_t(-1); i--)
    {
        hides[i].life = std::min(hides[i].capacity, hides[i].life + hides[i].regen);
        uint32_t hit = std::min(hides[i].life, damage);
        hides[i].life -= hit;  damage -= hit;
    }

    energy = std::min(energy, max_energy);
    if(damage || energy < cost)return passive_energy + energy;
    energy -= cost;

    uint8_t *cur = input.data();
    for(auto &womb : wombs)womb.active = *cur++;
    for(auto &claw : claws)claw.active = *cur++;
    for(auto &leg : legs)if(*cur++)
    {
        pos.x += r_sin(leg.dist_x4, angle + leg.angle + angle_90);
        pos.y += r_sin(leg.dist_x4, angle + leg.angle);
    }
    for(auto &rot : rotators)if(*cur++)angle += rot;
    for(auto &sig : signals)if(*cur++)flags |= sig;
    return 0;
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

World::Tile::~Tile()
{
    for(Creature *ptr = first; ptr;)
    {
        Creature *cr = ptr;  ptr = ptr->next;  delete cr;
    }
}


World::World() : total_food_count(0), spawn_per_tile(4), next_id(0)
{
    config.order_x = config.order_y = 8;  // 256 x 256
    config.base_radius = tile_size / 128;
    config.food_energy = 1024;
    config.input_level = 16;
    config.exp_sprout_per_tile = 0x0FFFFFFF;
    config.exp_sprout_per_grass = 0x0FFFFFFF;
    config.repression_range = tile_size / 128;
    config.sprout_dist_x4 = 5 * config.repression_range;
    config.meat_dist_x4 = tile_size / 128;
    config.calc_derived();

    uint64_t seed = 1234;
    uint32_t exp_grass_gen = uint32_t(-1) >> 6;

    size_t size = size_t(1) << (config.order_x + config.order_y);
    std::swap(config.exp_sprout_per_tile, exp_grass_gen);  tiles.reserve(size);
    for(size_t i = 0; i < size; i++)
    {
        tiles.emplace_back(seed, i);
        spawn_grass(tiles[i], i & config.mask_x, i >> config.order_x);
    }
    std::swap(config.exp_sprout_per_tile, exp_grass_gen);
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
    for(Creature *cr1 = tile1.first; cr1; cr1 = cr1->next)
        for(Creature *cr2 = tile2.first; cr2; cr2 = cr2->next)
            if(cr1 != cr2)Creature::process_detectors(cr1, cr2);

    for(Creature *cr = tile1.first; cr; cr = cr->next)
        cr->process_food(tile2.foods.data(), tile2.foods.size());

    for(Creature *cr = tile2.first; cr; cr = cr->next)
        cr->process_food(tile1.foods.data(), tile1.foods.size());

    // TODO: process sprouts
}

void World::next_step()
{
    std::vector<Tile> old;  old.reserve(tiles.size());  old.swap(tiles);

    total_food_count = 0;
    for(size_t i = 0; i < old.size(); i++)
        tiles.emplace_back(config, old[i], total_food_count, spawn_per_tile);

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
            *tiles[index].last = cr;  tiles[index].last = &cr->next;
            for(const auto &womb : cr->wombs)if(womb.active)
            {
                Creature *child = Creature::spawn(next_id++, prev_pos, prev_angle, womb.energy, *cr);
                if(child)
                {
                    *tiles[i].last = child;  tiles[i].last = &child->next;
                }
                else spawn_meat(tiles[i], prev_pos, womb.energy);
            }
        }
    }
    old.clear();

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
}
