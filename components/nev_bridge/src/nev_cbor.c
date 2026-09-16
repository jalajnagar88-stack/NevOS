#include "nev_bridge/nev_cbor.h"
#include <string.h>

#define MT_UINT      0
#define MT_NINT      1
#define MT_BYTES     2
#define MT_TEXT      3
#define MT_ARRAY     4
#define MT_SIMPLE    7

#define AI_U8        24
#define AI_U16       25
#define AI_U32       26
#define AI_U64       27

#define SIMPLE_FALSE 20
#define SIMPLE_TRUE  21

void nev_cbor_w_init(nev_cbor_w_t *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = (buf == NULL);
}

void nev_cbor_r_init(nev_cbor_r_t *r, const uint8_t *buf, size_t len) {
    r->buf = buf;
    r->len = len;
    r->pos = 0;
    r->error = (buf == NULL);
}

/* ------------------------------------------------------------------ writer */

static void put(nev_cbor_w_t *w, uint8_t byte) {
    if (w->overflow) return;
    if (w->len >= w->cap) {
        w->overflow = true;
        return;
    }
    w->buf[w->len++] = byte;
}

static void put_n(nev_cbor_w_t *w, const uint8_t *data, size_t len) {
    if (w->overflow) return;
    if (len > w->cap - w->len) {
        w->overflow = true;
        return;
    }
    if (len) memcpy(w->buf + w->len, data, len);
    w->len += len;
}

/* Head byte plus the shortest argument that holds `v` — canonical CBOR. Two
 * encoders that disagree on this produce different bytes for the same message,
 * which would break the golden vectors the two languages are checked against. */
static void put_head(nev_cbor_w_t *w, uint8_t major, uint64_t v) {
    const uint8_t m = (uint8_t)(major << 5);
    if (v < 24) {
        put(w, (uint8_t)(m | v));
    } else if (v <= 0xFF) {
        put(w, (uint8_t)(m | AI_U8));
        put(w, (uint8_t)v);
    } else if (v <= 0xFFFF) {
        put(w, (uint8_t)(m | AI_U16));
        put(w, (uint8_t)(v >> 8));
        put(w, (uint8_t)v);
    } else if (v <= 0xFFFFFFFFu) {
        put(w, (uint8_t)(m | AI_U32));
        for (int i = 3; i >= 0; i--)
            put(w, (uint8_t)(v >> (i * 8)));
    } else {
        put(w, (uint8_t)(m | AI_U64));
        for (int i = 7; i >= 0; i--)
            put(w, (uint8_t)(v >> (i * 8)));
    }
}

void nev_cbor_w_u64(nev_cbor_w_t *w, uint64_t v) {
    put_head(w, MT_UINT, v);
}

void nev_cbor_w_i64(nev_cbor_w_t *w, int64_t v) {
    if (v < 0) {
        /* CBOR stores -1-n, so -1 is encoded as 0. Computed on the unsigned
         * side because negating INT64_MIN is undefined. */
        put_head(w, MT_NINT, (uint64_t)(-(v + 1)));
    } else {
        put_head(w, MT_UINT, (uint64_t)v);
    }
}

void nev_cbor_w_bool(nev_cbor_w_t *w, bool v) {
    put(w, (uint8_t)((MT_SIMPLE << 5) | (v ? SIMPLE_TRUE : SIMPLE_FALSE)));
}

void nev_cbor_w_f32(nev_cbor_w_t *w, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits)); /* not a cast: type-punning through a
                                      * pointer is undefined behaviour */
    put(w, (uint8_t)((MT_SIMPLE << 5) | AI_U32));
    for (int i = 3; i >= 0; i--)
        put(w, (uint8_t)(bits >> (i * 8)));
}

void nev_cbor_w_bytes(nev_cbor_w_t *w, const uint8_t *data, size_t len) {
    put_head(w, MT_BYTES, len);
    put_n(w, data, len);
}

void nev_cbor_w_text(nev_cbor_w_t *w, const char *s) {
    const size_t n = s ? strlen(s) : 0;
    put_head(w, MT_TEXT, n);
    put_n(w, (const uint8_t *)s, n);
}

void nev_cbor_w_array(nev_cbor_w_t *w, size_t count) {
    put_head(w, MT_ARRAY, count);
}

/* ------------------------------------------------------------------ reader */

static bool avail(const nev_cbor_r_t *r, size_t n) {
    return !r->error && r->len - r->pos >= n;
}

static bool read_head(nev_cbor_r_t *r, uint8_t *major, uint64_t *arg) {
    if (!avail(r, 1)) {
        r->error = true;
        return false;
    }
    const uint8_t b = r->buf[r->pos++];
    *major = (uint8_t)(b >> 5);
    const uint8_t ai = (uint8_t)(b & 0x1F);

    if (ai < 24) {
        *arg = ai;
        return true;
    }

    size_t n;
    switch (ai) {
        case AI_U8:
            n = 1;
            break;
        case AI_U16:
            n = 2;
            break;
        case AI_U32:
            n = 4;
            break;
        case AI_U64:
            n = 8;
            break;
        default:
            /* 28-30 are reserved and 31 is indefinite length. Both are refused:
             * indefinite-length items are how a decoder is made to loop on
             * attacker-controlled input. */
            r->error = true;
            return false;
    }
    if (!avail(r, n)) {
        r->error = true;
        return false;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)
        v = (v << 8) | r->buf[r->pos++];
    *arg = v;
    return true;
}

bool nev_cbor_r_u64(nev_cbor_r_t *r, uint64_t *out) {
    uint8_t major;
    uint64_t arg;
    const size_t start = r->pos;
    if (!read_head(r, &major, &arg) || major != MT_UINT) {
        r->pos = start;
        r->error = true;
        return false;
    }
    if (out) *out = arg;
    return true;
}

bool nev_cbor_r_u32(nev_cbor_r_t *r, uint32_t *out) {
    uint64_t v;
    if (!nev_cbor_r_u64(r, &v)) return false;
    if (v > 0xFFFFFFFFu) { /* a wider value than the field can hold is a
                            * protocol error, not something to truncate */
        r->error = true;
        return false;
    }
    if (out) *out = (uint32_t)v;
    return true;
}

bool nev_cbor_r_i64(nev_cbor_r_t *r, int64_t *out) {
    uint8_t major;
    uint64_t arg;
    const size_t start = r->pos;
    if (!read_head(r, &major, &arg)) return false;

    if (major == MT_UINT) {
        if (arg > (uint64_t)INT64_MAX) {
            r->pos = start;
            r->error = true;
            return false;
        }
        if (out) *out = (int64_t)arg;
        return true;
    }
    if (major == MT_NINT) {
        if (arg > (uint64_t)INT64_MAX) {
            r->pos = start;
            r->error = true;
            return false;
        }
        if (out) *out = -(int64_t)arg - 1;
        return true;
    }
    r->pos = start;
    r->error = true;
    return false;
}

bool nev_cbor_r_i32(nev_cbor_r_t *r, int32_t *out) {
    int64_t v;
    if (!nev_cbor_r_i64(r, &v)) return false;
    if (v < INT32_MIN || v > INT32_MAX) {
        r->error = true;
        return false;
    }
    if (out) *out = (int32_t)v;
    return true;
}

bool nev_cbor_r_bool(nev_cbor_r_t *r, bool *out) {
    if (!avail(r, 1)) {
        r->error = true;
        return false;
    }
    const uint8_t b = r->buf[r->pos];
    if (b == ((MT_SIMPLE << 5) | SIMPLE_TRUE)) {
        r->pos++;
        if (out) *out = true;
        return true;
    }
    if (b == ((MT_SIMPLE << 5) | SIMPLE_FALSE)) {
        r->pos++;
        if (out) *out = false;
        return true;
    }
    r->error = true;
    return false;
}

bool nev_cbor_r_f32(nev_cbor_r_t *r, float *out) {
    if (!avail(r, 5) || r->buf[r->pos] != ((MT_SIMPLE << 5) | AI_U32)) {
        r->error = true;
        return false;
    }
    r->pos++;
    uint32_t bits = 0;
    for (int i = 0; i < 4; i++)
        bits = (bits << 8) | r->buf[r->pos++];
    if (out) memcpy(out, &bits, sizeof(*out));
    return true;
}

static bool read_string(nev_cbor_r_t *r, uint8_t want_major, const uint8_t **data, size_t *len) {
    uint8_t major;
    uint64_t arg;
    const size_t start = r->pos;
    if (!read_head(r, &major, &arg) || major != want_major) {
        r->pos = start;
        r->error = true;
        return false;
    }
    /* Check the declared length against what is actually present before
     * trusting it: a header claiming four gigabytes is the classic way to make
     * a parser read past its buffer. */
    if (arg > (uint64_t)(r->len - r->pos)) {
        r->pos = start;
        r->error = true;
        return false;
    }
    *data = r->buf + r->pos;
    *len = (size_t)arg;
    r->pos += (size_t)arg;
    return true;
}

bool nev_cbor_r_bytes(nev_cbor_r_t *r, const uint8_t **data, size_t *len) {
    const uint8_t *d;
    size_t n;
    if (!read_string(r, MT_BYTES, &d, &n)) return false;
    if (data) *data = d;
    if (len) *len = n;
    return true;
}

bool nev_cbor_r_text(nev_cbor_r_t *r, char *out, size_t out_cap) {
    const uint8_t *d;
    size_t n;
    const size_t start = r->pos;
    if (!read_string(r, MT_TEXT, &d, &n)) return false;

    if (!out || out_cap == 0) return true; /* caller only wanted it consumed */
    if (n >= out_cap) {
        /* Refused rather than truncated: a silently shortened device name or
         * pairing token is worse than a rejected message. */
        r->pos = start;
        r->error = true;
        return false;
    }
    memcpy(out, d, n);
    out[n] = '\0';
    return true;
}

bool nev_cbor_r_array(nev_cbor_r_t *r, size_t *count) {
    uint8_t major;
    uint64_t arg;
    const size_t start = r->pos;
    if (!read_head(r, &major, &arg) || major != MT_ARRAY) {
        r->pos = start;
        r->error = true;
        return false;
    }
    /* An array header cannot promise more elements than there are bytes left,
     * since the shortest possible element is one byte. */
    if (arg > (uint64_t)(r->len - r->pos)) {
        r->pos = start;
        r->error = true;
        return false;
    }
    if (count) *count = (size_t)arg;
    return true;
}

bool nev_cbor_r_skip(nev_cbor_r_t *r) {
    uint8_t major;
    uint64_t arg;
    if (!read_head(r, &major, &arg)) return false;

    switch (major) {
        case MT_UINT:
        case MT_NINT:
            return true;

        case MT_BYTES:
        case MT_TEXT:
            if (arg > (uint64_t)(r->len - r->pos)) {
                r->error = true;
                return false;
            }
            r->pos += (size_t)arg;
            return true;

        case MT_ARRAY:
            /* One level only: the protocol never nests, so a nested array is
             * either a bug or an attempt to make this recurse. */
            for (uint64_t i = 0; i < arg; i++) {
                uint8_t m;
                uint64_t a;
                const size_t here = r->pos;
                if (!read_head(r, &m, &a)) return false;
                if (m == MT_ARRAY) {
                    r->pos = here;
                    r->error = true;
                    return false;
                }
                if ((m == MT_BYTES || m == MT_TEXT)) {
                    if (a > (uint64_t)(r->len - r->pos)) {
                        r->error = true;
                        return false;
                    }
                    r->pos += (size_t)a;
                }
            }
            return true;

        case MT_SIMPLE:
            return true; /* the argument was already consumed by read_head */

        default:
            r->error = true;
            return false;
    }
}
