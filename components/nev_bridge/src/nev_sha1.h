/* SHA-1 and base64, for the WebSocket handshake and nothing else. */
#ifndef NEV_BRIDGE_NEV_SHA1_H
#define NEV_BRIDGE_NEV_SHA1_H

#include "nev_port/nev_types.h"

/*
 * Hand-written rather than mbedtls, because mbedtls is an ESP-IDF component and
 * this has to build on the host too — and the whole point of the bridge living
 * behind nev_port is that it compiles and is tested off-device.
 *
 * SHA-1 is used here for exactly one thing: the Sec-WebSocket-Accept value,
 * which RFC 6455 specifies as a fixed handshake ritual. Its brokenness as a
 * hash is irrelevant to that use — there is no secret involved and no signature
 * being verified. Nothing else in NEVOS may use this for anything.
 */
#define NEV_SHA1_DIGEST_LEN 20

void nev_sha1(const uint8_t *data, size_t len, uint8_t out[NEV_SHA1_DIGEST_LEN]);

/* Writes base64 of `len` bytes plus a NUL. `out` needs 4*((len+2)/3)+1 bytes. */
void nev_base64(const uint8_t *data, size_t len, char *out, size_t out_cap);

#endif /* NEV_BRIDGE_NEV_SHA1_H */
