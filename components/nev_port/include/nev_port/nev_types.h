/* NEVOS L-1 — common types and error codes. Platform-free. */
#ifndef NEV_PORT_NEV_TYPES_H
#define NEV_PORT_NEV_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_OK = 0,
    NEV_ERR_INVALID_ARG = -1,
    NEV_ERR_NO_MEM = -2,
    NEV_ERR_NO_SPACE = -3,
    NEV_ERR_TIMEOUT = -4,
    NEV_ERR_NOT_FOUND = -5,
    NEV_ERR_INVALID_STATE = -6,
    NEV_ERR_DROPPED = -7,
    NEV_ERR_UNSUPPORTED = -8,
} nev_err_t;

const char *nev_err_str(nev_err_t err);

#define NEV_WAIT_FOREVER ((uint32_t)0xFFFFFFFFu)
#define NEV_NO_WAIT      ((uint32_t)0u)

#define NEV_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define NEV_UNUSED(x)    ((void)(x))

#define NEV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NEV_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Propagate a non-OK result to the caller. */
#define NEV_TRY(expr)                                                                              \
    do {                                                                                           \
        nev_err_t nev_try_rc_ = (expr);                                                            \
        if (nev_try_rc_ != NEV_OK) return nev_try_rc_;                                             \
    } while (0)

typedef struct {
    int16_t x1, y1, x2, y2; /* inclusive */
} nev_rect_t;

static inline int32_t nev_rect_w(const nev_rect_t *r) { return (int32_t)r->x2 - r->x1 + 1; }
static inline int32_t nev_rect_h(const nev_rect_t *r) { return (int32_t)r->y2 - r->y1 + 1; }

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_TYPES_H */
