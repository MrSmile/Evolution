// hash.h : BLAKE2b hash calculation
//

#pragma once

#include "math.h"



inline uint16_t bswap16(uint16_t val)
{
    return val >> 16 | val << 16;
}

inline uint32_t bswap32(uint32_t val)
{
    return (val >> 24 | val <<  8) & 0x00FF00FFul |
           (val >>  8 | val << 24) & 0xFF00FF00ul;
}

inline uint64_t bswap64(uint64_t val)
{
    return (val >> 56 | val <<  8) & 0x000000FF000000FFull |
           (val >> 40 | val << 24) & 0x0000FF000000FF00ull |
           (val >> 24 | val << 40) & 0x00FF000000FF0000ull |
           (val >>  8 | val << 56) & 0xFF000000FF000000ull;
}

inline uint16_t to_le16(uint16_t val)
{
#ifdef BIG_ENDIAN
    return bswap16(val);
#else
    return val;
#endif
}

inline uint32_t to_le32(uint32_t val)
{
#ifdef BIG_ENDIAN
    return bswap32(val);
#else
    return val;
#endif
}

inline uint64_t to_le64(uint64_t val)
{
#ifdef BIG_ENDIAN
    return bswap64(val);
#else
    return val;
#endif
}


class Hash
{
    uint64_t h[8], t[2], f[2];

    void compress_block(uint64_t m[16]);

public:
    static constexpr unsigned block_size = 128;
    static constexpr unsigned result_size = sizeof(h);

    void init();
    void process_block(void *buf);
    void process_last(void *buf, unsigned size);

    const void *result() const
    {
        return h;
    }
};
