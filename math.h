// math.h : mathematical functions
//

#pragma once

#include <cstdint>

using std::int8_t;   using std::uint8_t;
using std::int16_t;  using std::uint16_t;
using std::int32_t;  using std::uint32_t;
using std::int64_t;  using std::uint64_t;


constexpr double pi = 3.14159265358979323846264338327950288;


constexpr int tile_order = 30;
constexpr uint32_t tile_size = 1ul << tile_order;
constexpr uint64_t max_r2 = 1ull << (2 * tile_order);
constexpr uint64_t sqrt_scale = (max_r2 - 1) / (255u * 255u) + 1;
constexpr uint32_t tile_mask = tile_size - 1;

typedef uint8_t angle_t;
constexpr angle_t flip_angle = 128;
constexpr angle_t angle_90 = 64;

int32_t r_sin(uint32_t r_x4, angle_t angle);
angle_t calc_angle(int32_t dx, int32_t dy);
uint8_t calc_radius(uint64_t r2);



class Random
{
    uint64_t cur, inc;

public:
    Random(uint64_t seed, uint64_t seq);

    uint32_t uint32();
    uint32_t poisson(uint32_t exp_prob);
};
