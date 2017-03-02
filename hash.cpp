// hash.cpp : BLAKE2b hash calculation
//

#include "hash.h"
#include <cstring>
#include <cstdio>
#include <ctime>



const uint64_t blake2b_vec[8] =
{
  0x6A09E667F3BCC908ull, 0xBB67AE8584CAA73Bull,
  0x3C6EF372FE94F82Bull, 0xA54FF53A5F1D36F1ull,
  0x510E527FADE682D1ull, 0x9B05688C2B3E6C1Full,
  0x1F83D9ABFB41BD6Bull, 0x5BE0CD19137E2179ull
};

void Hash::init()
{
    for(int i = 0; i < 8; i++)h[i] = blake2b_vec[i];
    h[0] ^= 0x01010040ul;  t[0] = t[1] = f[0] = f[1] = 0;
}

const uint8_t blake2b_sigma[160] =
{
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3,
    11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4,
     7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8,
     9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13,
     2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9,
    12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11,
    13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10,
     6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5,
    10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0,
};

void blake2b_step(int i, const uint64_t m[16], uint64_t v[16], int a, int b, int c, int d)
{
    v[a] = v[a] + v[b] + m[blake2b_sigma[i + 0]];
    v[d] = rot64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = rot64(v[b] ^ v[c], 24);

    v[a] = v[a] + v[b] + m[blake2b_sigma[i + 1]];
    v[d] = rot64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rot64(v[b] ^ v[c], 63);
}

void blake2b_round(int r, const uint64_t m[16], uint64_t v[16])
{
    int i = 16 * r;
    blake2b_step(i, m, v,  0,  4,  8, 12);  i += 2;
    blake2b_step(i, m, v,  1,  5,  9, 13);  i += 2;
    blake2b_step(i, m, v,  2,  6, 10, 14);  i += 2;
    blake2b_step(i, m, v,  3,  7, 11, 15);  i += 2;
    blake2b_step(i, m, v,  0,  5, 10, 15);  i += 2;
    blake2b_step(i, m, v,  1,  6, 11, 12);  i += 2;
    blake2b_step(i, m, v,  2,  7,  8, 13);  i += 2;
    blake2b_step(i, m, v,  3,  4,  9, 14);
}

void Hash::compress_block(uint64_t m[16])
{
    for(int i = 0; i < 16; i++)m[i] = to_le64(m[i]);

    uint64_t v[16];
    for(int i = 0; i < 8; i++)v[i] = h[i];
    for(int i = 0; i < 8; i++)v[i + 8] = blake2b_vec[i];
    v[12] ^= t[0];  v[13] ^= t[1];  v[14] ^= f[0];  v[15] ^= f[1];
    for(int i = 0; i < 12; i++)blake2b_round(i % 10, m, v);
    for(int i = 0; i < 8; i++)h[i] ^= v[i] ^ v[i + 8];
}

void Hash::process_block(void *buf)
{
    t[0] += block_size;  // ignore more than 2^64 bytes
    compress_block(static_cast<uint64_t *>(buf));
}

void Hash::process_last(void *buf, unsigned size)
{
    t[0] += size;  f[0] = -1;
    std::memset(static_cast<char *>(buf) + size, 0, block_size - size);
    compress_block(static_cast<uint64_t *>(buf));

    for(int i = 0; i < 8; i++)h[i] = to_le64(h[i]);
}



#if 1

void hash_test()
{
    Hash hash;  hash.init();
    char buf[Hash::block_size];
    std::memcpy(buf, "abc", 3);
    hash.process_last(buf, 3);

    std::printf("Result:");
    const uint8_t *res = static_cast<const uint8_t *>(hash.result());
    for(unsigned i = 0; i < Hash::result_size; i++)
        std::printf(" %02X", (unsigned)res[i]);
    std::printf("\n");
}

#else

#include <blake2.h>

bool check_hash(char *str, size_t len)
{
    uint8_t cmp[Hash::result_size];
    if(blake2b(cmp, str, nullptr, sizeof(cmp), len, 0))return false;

    Hash hash;  hash.init();  size_t pos = 0;
    for(; pos + Hash::block_size < len; pos += Hash::block_size)hash.process_block(str + pos);
    hash.process_last(str + pos, len - pos);

    return !std::memcmp(cmp, hash.result(), sizeof(cmp));
}

void hash_test()
{
    constexpr unsigned n = 16 * Hash::block_size;
    uint32_t buf[n];

    Random rand(clock(), 0);
    for(unsigned i = 0; i < n; i++)buf[i] = rand.uint32();

    for(size_t i = 0; i < sizeof(buf); i++)
        if(!check_hash(reinterpret_cast<char *>(buf), i))
            std::printf(">>> CHECK FAILED <<<\n");
}

#endif
