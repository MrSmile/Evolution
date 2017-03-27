// math.h : mathematical functions
//

#pragma once

#include <cstdint>
#include <climits>

using std::int8_t;   using std::uint8_t;
using std::int16_t;  using std::uint16_t;
using std::int32_t;  using std::uint32_t;
using std::int64_t;  using std::uint64_t;
using std::size_t;


inline uint32_t rot32(uint32_t val, int n)
{
    return val >> n | val << (-n & 31);
}

inline uint64_t rot64(uint64_t val, int n)
{
    return val >> n | val << (-n & 63);
}


constexpr double pi = 3.14159265358979323846264338327950288;


constexpr int tile_order = 30;
constexpr uint32_t tile_size = 1ul << tile_order;
constexpr uint64_t max_r2 = 1ull << (2 * tile_order);
constexpr uint64_t sqrt_scale = (max_r2 - 1) / (255u * 255u) + 1;
constexpr uint32_t tile_mask = tile_size - 1;
constexpr int radius_bits = 8;

typedef uint8_t angle_t;
constexpr int angle_bits = 8;
constexpr angle_t flip_angle = 128;
constexpr angle_t angle_90 = 64;


#ifdef BUILTIN

inline int ilog2(unsigned int val)
{
    return __builtin_clz(val) ^ (sizeof(val) * CHAR_BIT - 1);
}

inline int ilog2(unsigned long val)
{
    return __builtin_clzl(val) ^ (sizeof(val) * CHAR_BIT - 1);
}

inline int ilog2(unsigned long long val)
{
    return __builtin_clzll(val) ^ (sizeof(val) * CHAR_BIT - 1);
}

#else

template<typename T, int n> int ilog2_(T val)
{
    int res = 0;
    for(int ord = n / 2; ord; ord /= 2)
        if(val >= (T(1) << ord))
        {
            res += ord;  val >>= ord;
        }
    return res;
}

inline int ilog2(uint32_t val)
{
    return ilog2_<uint32_t, 32>(val);
}

inline int ilog2(uint64_t val)
{
    return ilog2_<uint64_t, 32>(val);
}

#endif

inline uint32_t mul_high(uint32_t a, uint32_t b)
{
    return uint64_t(a) * b >> 32;
}


int32_t r_sin(uint32_t r_x4, angle_t angle);
angle_t calc_angle(int32_t dx, int32_t dy);
uint8_t calc_radius(uint64_t r2);



class InStream;
class OutStream;

class Random
{
    uint64_t cur, inc;

public:
    Random()
    {
    }

    Random(uint64_t seed, uint64_t seq);

    bool load(InStream &stream);
    void save(OutStream &stream) const;

    uint32_t uint32();
    uint32_t uniform(uint32_t lim);
    uint32_t poisson(uint32_t exp_prob);
    uint32_t geometric(uint32_t prob);
};
