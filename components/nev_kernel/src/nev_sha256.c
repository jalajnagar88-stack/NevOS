/* SHA-256 (FIPS 180-4). See nev_sha256.h for why this is written rather than used. */
#include "nev_kernel/nev_sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t rotr(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
}

static void compress(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; i++) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void nev_sha256_init(nev_sha256_t *s) {
    if (!s) return;
    s->h[0] = 0x6a09e667u;
    s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u;
    s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu;
    s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu;
    s->h[7] = 0x5be0cd19u;
    s->total_bytes = 0;
    s->block_len = 0;
}

void nev_sha256_update(nev_sha256_t *s, const uint8_t *data, size_t len) {
    if (!s || (!data && len)) return;
    s->total_bytes += len;

    while (len > 0) {
        const size_t room = 64 - s->block_len;
        const size_t take = len < room ? len : room;
        memcpy(s->block + s->block_len, data, take);
        s->block_len += take;
        data += take;
        len -= take;
        if (s->block_len == 64) {
            compress(s->h, s->block);
            s->block_len = 0;
        }
    }
}

void nev_sha256_final(nev_sha256_t *s, uint8_t out[NEV_SHA256_DIGEST_LEN]) {
    if (!s || !out) return;

    const uint64_t bits = s->total_bytes * 8u;
    const uint8_t pad = 0x80;
    nev_sha256_update(s, &pad, 1);
    /* The length must land in the last 8 bytes of a block, so pad with zeros
     * until there is exactly room for it — possibly into a second block. */
    const uint8_t zero = 0;
    while (s->block_len != 56) {
        nev_sha256_update(s, &zero, 1);
    }
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) {
        len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    /* total_bytes is now wrong, but it has already been captured above. */
    nev_sha256_update(s, len_be, 8);

    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

void nev_sha256(const uint8_t *data, size_t len, uint8_t out[NEV_SHA256_DIGEST_LEN]) {
    nev_sha256_t s;
    nev_sha256_init(&s);
    nev_sha256_update(&s, data, len);
    nev_sha256_final(&s, out);
}

bool nev_sha256_equal(const uint8_t a[NEV_SHA256_DIGEST_LEN],
                      const uint8_t b[NEV_SHA256_DIGEST_LEN]) {
    if (!a || !b) return false;
    uint8_t diff = 0;
    for (int i = 0; i < NEV_SHA256_DIGEST_LEN; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}
