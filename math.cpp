// math.h : mathematical functions
//

#include "stream.h"
#include <algorithm>
#include <climits>
#include <ctime>
#include <cmath>



const uint32_t cos_table[] =
{
    0x80000000, 0x7FF62182, 0x7FD8878E, 0x7FA736B4, 0x7F62368F, 0x7F0991C4, 0x7E9D55FC, 0x7E1D93EA,
    0x7D8A5F40, 0x7CE3CEB2, 0x7C29FBEE, 0x7B5D039E, 0x7A7D055B, 0x798A23B1, 0x78848414, 0x776C4EDB,
    0x7641AF3D, 0x7504D345, 0x73B5EBD1, 0x72552C85, 0x70E2CBC6, 0x6F5F02B2, 0x6DCA0D14, 0x6C242960,
    0x6A6D98A4, 0x68A69E81, 0x66CF8120, 0x64E88926, 0x62F201AC, 0x60EC3830, 0x5ED77C8A, 0x5CB420E0,
    0x5A82799A, 0x5842DD54, 0x55F5A4D2, 0x539B2AF0, 0x5133CC94, 0x4EBFE8A5, 0x4C3FDFF4, 0x49B41533,
    0x471CECE7, 0x447ACD50, 0x41CE1E65, 0x3F1749B8, 0x3C56BA70, 0x398CDD32, 0x36BA2014, 0x33DEF287,
    0x30FBC54D, 0x2E110A62, 0x2B1F34EB, 0x2826B928, 0x25280C5E, 0x2223A4C5, 0x1F19F97B, 0x1C0B826A,
    0x18F8B83C, 0x15E21445, 0x12C8106F, 0x0FAB272B, 0x0C8BD35E, 0x096A9049, 0x0647D97C, 0x03242ABF,
    0x00000000
};

int32_t r_sin(uint32_t r_x4, angle_t angle)
{
    int index = (angle & (flip_angle - 1)) - angle_90;
    uint32_t mul = cos_table[index < 0 ? -index : index];
    int32_t res = (uint64_t(r_x4) * mul + (int64_t(1) << 32)) >> 33;
    return angle & flip_angle ? -res : res;
}

void generate_table()
{
    for(unsigned i = 0; i <= angle_90; i++)
    {
        if(!(i % 8))std::printf("\n   ");
        long long val = std::llround((1l << 31) * std::cos(i * (pi / flip_angle)));
        std::printf(" 0x%08llX%s", val, i < 64 ? "," : "\n");
    }
}


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


angle_t calc_angle(int32_t dx, int32_t dy)
{
#if 1

    int32_t flag_x = dx >> 31;
    int32_t flag_y = dy >> 31;
    dx = (dx ^ flag_x) - flag_x;
    dy = (dy ^ flag_y) - flag_y;

    int32_t delta = dx - dy;
    int32_t flag = delta >> 31;
    delta &= flag;

    uint32_t x = dx - delta;
    uint32_t y = dy + delta;
    flag_x &= flip_angle - 1;  flag &= angle_90 - 1;
    angle_t mask = flag_x ^ flag_y ^ flag;

#else

    angle_t mask = 0;
    uint32_t x = dx, y = dy;
    if(x & (uint32_t(1) << 31))
    {
        x = -x;  mask ^= flip_angle - 1;
    }
    if(y & (uint32_t(1) << 31))
    {
        y = -y;  mask = ~mask;
    }
    if(y > x)
    {
        std::swap(x, y);  mask ^= angle_90 - 1;
    }

#endif

    if(!x)return 0;
    int shift = 31 - ilog2(x);
    x <<= shift;  y <<= shift;

    uint32_t t = 0x7FFFFFFF - x;
    t = mul_high(0x0F5C28F6 - mul_high(t, x), mul_high(t, y));
    t = mul_high(0x99922D0E - mul_high(0x8B2A3D71, t), t);
    angle_t res = (t + 0x007FFFFF) >> 24;

    uint64_t xs = uint64_t(x) * cos_table[angle_90 - res];
    uint64_t yc = uint64_t(y) * cos_table[res];
    if(xs > yc)res--;  res ^= mask;
    return xs == yc ? res + (mask & 1) : res;
}

int64_t test_sin(angle_t angle)
{
    int index = (angle & (flip_angle - 1)) - angle_90;
    int64_t res = cos_table[index < 0 ? -index : index];
    return angle & flip_angle ? -res : res;
}

bool check_point(int32_t x, int32_t y)
{
    if(!x && !y)return true;
    angle_t angle = calc_angle(x, y), angle1 = angle + 1;
    int64_t check  = x * test_sin(angle)  - y * test_sin(angle  + angle_90);
    int64_t check1 = x * test_sin(angle1) - y * test_sin(angle1 + angle_90);
    if(check > 0 || check1 <= 0)return false;

    return true;  // fast route

    if(calc_angle(-y, +x) != angle_t(angle + 1 * angle_90))return false;
    if(calc_angle(-x, -y) != angle_t(angle + 2 * angle_90))return false;
    if(calc_angle(+y, -x) != angle_t(angle + 3 * angle_90))return false;

    if(check)angle++;
    if(calc_angle(+x, -y) != angle_t(0 * angle_90 - angle))return false;
    if(calc_angle(+y, +x) != angle_t(1 * angle_90 - angle))return false;
    if(calc_angle(-x, +y) != angle_t(2 * angle_90 - angle))return false;
    if(calc_angle(-y, -x) != angle_t(3 * angle_90 - angle))return false;
    return true;
}

void angle_test()
{
#if 1

    clock_t start = clock();  Random rand(start, 0);
    for(uint64_t n = 0;; n++)
    {
        if(!(n % 100000000))std::printf("Completed up to %llu, %.6g s/10^9\n",
            (unsigned long long)n, (1e9 / CLOCKS_PER_SEC) * (clock() - start) / (n + 1));

        int32_t x = rand.uint32(), y = rand.uint32();
        if(uint32_t(x) == (1ul << 31))continue;
        if(uint32_t(y) == (1ul << 31))continue;
        if(check_point(x, y))continue;

        std::printf("Point (%ld, %ld) failed!\n", long(x), long(y));  return;
    }

#else

    constexpr int r = 1024;
    for(int y = 0; y <= r; y++)for(int x = 0; x <= r; x++)
    {
        if(check_point(x, y))continue;
        std::printf("Point (%ld, %ld) failed!\n", long(x), long(y));
        return;
    }

    clock_t start = clock();  constexpr int rad = 1;
    for(uint32_t len = 0; len < (3ul << 30); len += 2 * rad)
    {
        if(!(len % 100000))std::printf("Completed up to %lu, %.6g s/10^6\n",
            (unsigned long)len, (1e6 / CLOCKS_PER_SEC) * (clock() - start) / (len + 1));

        double mul = len / double(1 << 31);
        for(int angle = 0; angle <= angle_t(-1); angle++)
        {
            int64_t x = llround(mul * test_sin(angle + angle_90));
            int64_t y = llround(mul * test_sin(angle));
            for(int dx = -rad; dx <= rad; dx++)for(int dy = -rad; dy <= rad; dy++)
            {
                int64_t xx = x + dx, yy = y + dy;
                if(std::abs(xx) > 0x7FFFFFFF)continue;
                if(std::abs(yy) > 0x7FFFFFFF)continue;
                if(check_point(xx, yy))continue;

                std::printf("Point (%ld, %ld) failed!\n", long(xx), long(yy));
                return;
            }
        }
    }

#endif
}


uint8_t calc_radius(uint64_t r2)
{
#if 1

    if(r2 < sqrt_scale)return 0;
    int shift = 15 - (ilog2(uint32_t(r2 >> 32)) >> 1);
    uint32_t t = r2 << 2 * shift >> 32;

    t = 0x1C180155 + mul_high(t, 0xA60393F5 - mul_high(t, 0x6492003C - mul_high(t, 0x220E6BB0)));
    uint32_t res = std::min<uint32_t>(255, ((t >> (shift + tile_order - 10)) + 1) >> 1);
    return res * res * sqrt_scale > r2 ? res - 1 : res;

#else

    uint8_t res = 0;
    for(uint8_t step = 128; step; step /= 2)
        if((res + step) * (res + step) * sqrt_scale <= r2)res += step;
    return res;

#endif
}

bool check_radius(uint64_t r2)
{
    uint8_t res = calc_radius(r2), res1 = res + 1;
    if(sqrt_scale * res * res > r2)return false;
    if(res1 && sqrt_scale * res1 * res1 <= r2)return false;
    return true;
}

void radius_test()
{
#if 1

    clock_t start = clock();  Random rand(start, 0);
    for(uint64_t n = 0;; n++)
    {
        if(!(n % 100000000))std::printf("Completed up to %llu, %.6g s/10^9\n",
            (unsigned long long)n, (1e9 / CLOCKS_PER_SEC) * (clock() - start) / (n + 1));

        uint64_t val = uint64_t(rand.uint32()) << 32 | rand.uint32();
        if(check_radius(val))continue;

        std::printf("Radius %llu failed!\n", (unsigned long long)val);  return;
    }

#else

    constexpr int rad = 3;
    for(int r = 0; r <= 256; r++)
    {
        uint64_t r2 = sqrt_scale * r * r;
        for(int dr2 = -rad; dr2 <= rad; dr2++)
        {
            uint64_t val = r2 + dr2;  if(check_radius(val))continue;
            std::printf("Radius %llu failed!\n", (unsigned long long)val);
            return;
        }
    }

#endif
}



// Random class

Random::Random(uint64_t seed, uint64_t seq) : cur(0), inc(2 * seq + 1)
{
    uint32();  cur += seed;  uint32();
}


bool Random::load(InStream &stream)
{
    stream >> cur >> inc;  return stream && (inc & 1);
}

void Random::save(OutStream &stream) const
{
    stream << cur << inc;
}


uint32_t Random::uint32()  // algorithm: M.E. O'Neill / pcg-random.org
{
    uint64_t old = cur;
    cur = old * 6364136223846793005ull + inc;
    return rot32((old ^ old >> 18) >> 27, old >> 59);
}

uint32_t Random::uniform(uint32_t lim)
{
    for(;;)
    {
        uint32_t res = uint32(), val = res / lim * lim;
        if(val <= uint32_t(-lim))return res - val;
    }
}

uint32_t Random::poisson(uint32_t exp_prob)
{
    uint32_t val = uint32(), res = 0;
    while(val > exp_prob)
    {
        val = mul_high(val, uint32());  res++;
    }
    return res;
}

uint32_t Random::geometric(uint32_t prob)
{
    uint32_t val = uint32();
    if(val >= prob)return 0;

    uint32_t pow[37];  int ord = 0;
    for(;;)
    {
        pow[ord] = prob;
        uint32_t next = mul_high(prob, prob);
        if(val >= next)break;  prob = next;  ord++;
    }

    uint32_t res = 1;
    while(ord)
    {
        uint32_t next = mul_high(prob, pow[--ord]);  res *= 2;
        if(val >= next)continue;  prob = next;  res++;
    }
    return res;
}


void random_test()
{
    constexpr double lambda = 5;
    uint32_t exp_prob = std::lround(std::exp(-lambda) * std::pow(2.0, 32));

    constexpr int m = 32;
    uint64_t freq[m] = {};
    double mean = 0, disp = 0;

    Random rand(clock(), 0);
    constexpr uint64_t n = 100000000;
    for(uint64_t i = 0; i < n; i++)
    {
        uint32_t val = rand.poisson(exp_prob);
        if(val < m)freq[val]++;  mean += val;  disp += val * val;
    }

    constexpr double scale = 1.0 / n;  mean *= scale;  disp *= scale;
    for(int i = 0; i < m; i++)std::printf("%.6g\n", freq[i] * scale);
    std::printf("Mean: %.6g, Dispersion: %.6g\n", mean, disp - mean * mean);
}
