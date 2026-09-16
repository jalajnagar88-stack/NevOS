/* NEVOS L1 — SHA-256, streaming. */
#ifndef NEV_KERNEL_NEV_SHA256_H
#define NEV_KERNEL_NEV_SHA256_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Written rather than taken from mbedtls, for the same reason as the SHA-1 in
 * nev_bridge: mbedtls is an ESP-IDF component and this has to run on the host,
 * where the OTA logic is tested.
 *
 * Streaming, because the thing being hashed is a firmware image arriving over a
 * socket in 4 KB pieces and the device cannot hold it. Feed it as it comes.
 *
 * Unlike that SHA-1, this one is load-bearing: it decides whether a firmware
 * image is the one the daemon offered. Take that seriously — the test vectors
 * are FIPS 180-4's own.
 */
#define NEV_SHA256_DIGEST_LEN 32

typedef struct {
    uint32_t h[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_len;
} nev_sha256_t;

void nev_sha256_init(nev_sha256_t *s);
void nev_sha256_update(nev_sha256_t *s, const uint8_t *data, size_t len);
void nev_sha256_final(nev_sha256_t *s, uint8_t out[NEV_SHA256_DIGEST_LEN]);

/* One-shot, for anything that already has the whole thing in memory. */
void nev_sha256(const uint8_t *data, size_t len, uint8_t out[NEV_SHA256_DIGEST_LEN]);

/* Constant-time compare, so a digest check does not leak where it differed. */
bool nev_sha256_equal(const uint8_t a[NEV_SHA256_DIGEST_LEN],
                      const uint8_t b[NEV_SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_SHA256_H */
