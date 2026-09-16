/* SHA-1 (FIPS 180-4) and base64 (RFC 4648). See nev_sha1.h for why. */
#include "nev_sha1.h"

#include <string.h>

static uint32_t rotl(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t tmp = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void nev_sha1(const uint8_t *data, size_t len, uint8_t out[NEV_SHA1_DIGEST_LEN]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    size_t whole = len / 64;
    for (size_t i = 0; i < whole; i++) {
        sha1_block(h, data + i * 64);
    }

    /* The tail: remaining bytes, a 0x80 marker, zero padding, and the length in
     * bits as a big-endian 64-bit value. Two blocks are needed when the
     * remainder leaves no room for the length. */
    uint8_t tail[128];
    size_t rest = len - whole * 64;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data + whole * 64, rest);
    tail[rest] = 0x80;

    size_t tail_blocks = (rest + 1 + 8 > 64) ? 2 : 1;
    uint64_t bits = (uint64_t)len * 8u;
    size_t at = tail_blocks * 64 - 8;
    for (int i = 0; i < 8; i++) {
        tail[at + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    for (size_t i = 0; i < tail_blocks; i++) {
        sha1_block(h, tail + i * 64);
    }

    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
}

void nev_base64(const uint8_t *data, size_t len, char *out, size_t out_cap) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!out || out_cap == 0) return;

    size_t needed = 4 * ((len + 2) / 3) + 1;
    if (out_cap < needed) {
        out[0] = '\0';
        return;
    }

    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = kAlphabet[(v >> 6) & 0x3F];
        out[o++] = kAlphabet[v & 0x3F];
        i += 3;
    }
    size_t rest = len - i;
    if (rest == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rest == 2) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = kAlphabet[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
}
