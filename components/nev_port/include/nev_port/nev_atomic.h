/* NEVOS L-1 — atomics.
 *
 * C11 stdatomic works on both the Xtensa GCC toolchain and the host, so this
 * header has no per-platform implementation. Counters that are written from
 * one context and read from another (bus statistics, blob refcounts) use these.
 */
#ifndef NEV_PORT_NEV_ATOMIC_H
#define NEV_PORT_NEV_ATOMIC_H

#include "nev_port/nev_types.h"
#include <stdatomic.h>

typedef _Atomic uint32_t nev_atomic_u32_t;

static inline uint32_t nev_atomic_load(const nev_atomic_u32_t *a) {
    return atomic_load_explicit(a, memory_order_relaxed);
}
static inline void nev_atomic_store(nev_atomic_u32_t *a, uint32_t v) {
    atomic_store_explicit(a, v, memory_order_relaxed);
}
static inline uint32_t nev_atomic_inc(nev_atomic_u32_t *a) {
    return atomic_fetch_add_explicit(a, 1, memory_order_relaxed) + 1;
}
static inline uint32_t nev_atomic_add(nev_atomic_u32_t *a, uint32_t v) {
    return atomic_fetch_add_explicit(a, v, memory_order_relaxed) + v;
}
static inline uint32_t nev_atomic_dec(nev_atomic_u32_t *a) {
    return atomic_fetch_sub_explicit(a, 1, memory_order_acq_rel) - 1;
}

#endif /* NEV_PORT_NEV_ATOMIC_H */
