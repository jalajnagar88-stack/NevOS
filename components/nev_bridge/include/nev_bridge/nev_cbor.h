/*
 * NEVOS L6 — a minimal CBOR writer and reader (RFC 8949).
 *
 * Deliberately a subset: unsigned and negative integers, byte and text strings,
 * definite-length arrays, booleans and 32-bit floats. That is everything the
 * NEVOS wire protocol uses, and nothing else is accepted.
 *
 * Refusing indefinite-length items, maps, tags and 64-bit floats is a security
 * property, not laziness. This decoder runs on data arriving from the network
 * on a device with 512 KB of RAM: every construct it does not implement is a
 * construct that cannot be used to make it allocate, recurse or loop.
 *
 * Every read is bounds-checked against the buffer end, and every write against
 * the buffer capacity. Neither ever moves a cursor it has not validated.
 */
#ifndef NEV_BRIDGE_NEV_CBOR_H
#define NEV_BRIDGE_NEV_CBOR_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nesting is not supported beyond one array, so there is no recursion here at
 * all — but the guard is explicit so that stays true if it is ever extended. */
#define NEV_CBOR_MAX_DEPTH 1

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool overflow; /* sticky: one check at the end covers every write */
} nev_cbor_w_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    bool error; /* sticky, for the same reason */
} nev_cbor_r_t;

void nev_cbor_w_init(nev_cbor_w_t *w, uint8_t *buf, size_t cap);
void nev_cbor_r_init(nev_cbor_r_t *r, const uint8_t *buf, size_t len);

/* Writers. All are no-ops once `overflow` is set, so a caller may write a whole
 * message and check once. */
void nev_cbor_w_u64(nev_cbor_w_t *w, uint64_t v);
void nev_cbor_w_i64(nev_cbor_w_t *w, int64_t v);
void nev_cbor_w_bool(nev_cbor_w_t *w, bool v);
void nev_cbor_w_f32(nev_cbor_w_t *w, float v);
void nev_cbor_w_bytes(nev_cbor_w_t *w, const uint8_t *data, size_t len);
void nev_cbor_w_text(nev_cbor_w_t *w, const char *s);
void nev_cbor_w_array(nev_cbor_w_t *w, size_t count);

/* Readers. All return false and set `error` on a type mismatch, a truncated
 * item, or a value that does not fit the requested width. */
bool nev_cbor_r_u64(nev_cbor_r_t *r, uint64_t *out);
bool nev_cbor_r_u32(nev_cbor_r_t *r, uint32_t *out);
bool nev_cbor_r_i64(nev_cbor_r_t *r, int64_t *out);
bool nev_cbor_r_i32(nev_cbor_r_t *r, int32_t *out);
bool nev_cbor_r_bool(nev_cbor_r_t *r, bool *out);
bool nev_cbor_r_f32(nev_cbor_r_t *r, float *out);

/* Borrowed: the pointer is into the caller's buffer and is valid only as long
 * as it is. Nothing is copied and nothing is allocated. */
bool nev_cbor_r_bytes(nev_cbor_r_t *r, const uint8_t **data, size_t *len);

/* Copies into `out` and always NUL-terminates. A string longer than the
 * destination is a decode error, not a truncation: silently shortening a device
 * name or a pairing token is worse than refusing the message. */
bool nev_cbor_r_text(nev_cbor_r_t *r, char *out, size_t out_cap);

bool nev_cbor_r_array(nev_cbor_r_t *r, size_t *count);

/* Steps over the next item whatever it is. Used to ignore fields appended by a
 * newer peer; see docs/protocol.md on forward compatibility. */
bool nev_cbor_r_skip(nev_cbor_r_t *r);

static inline bool nev_cbor_r_done(const nev_cbor_r_t *r) {
    return !r->error && r->pos >= r->len;
}

#ifdef __cplusplus
}
#endif
#endif /* NEV_BRIDGE_NEV_CBOR_H */
