/*
 * NEVOS L1 — the blob pool.
 *
 * Sixteen bytes of event payload does not hold a 20 ms audio frame or a
 * streamed sentence. Copying those through the bus would break the fixed-size
 * property; heap allocation on the hot path would break the no-allocation
 * property. So: fixed-size buffers, preallocated at boot, passed by handle with
 * a reference count.
 *
 * Ownership protocol (docs/event-bus.md §7) — the whole of it:
 *   1. publisher calls nev_blob_alloc          -> refcount 1
 *   2. bus retains once per accepted delivery  -> refcount 1 + N
 *   3. publisher releases right after publish  -> refcount N
 *   4. each subscriber releases when done      -> refcount 0
 *   5. buffer returns to its free list
 *
 * A subscriber must release on every path, including early returns.
 */
#ifndef NEV_KERNEL_NEV_BLOB_H
#define NEV_KERNEL_NEV_BLOB_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t nev_blob_t;
#define NEV_BLOB_NONE         ((nev_blob_t)0xFFFFu)

/* Size classes. Provisional counts; tuned with measurements in BUDGET.md. */
#define NEV_BLOB_SMALL_SIZE   512u
#define NEV_BLOB_SMALL_COUNT  32u
#define NEV_BLOB_MEDIUM_SIZE  4096u
#define NEV_BLOB_MEDIUM_COUNT 16u
#define NEV_BLOB_LARGE_SIZE   65536u
#define NEV_BLOB_LARGE_COUNT  4u

#define NEV_BLOB_MAX_LEN      NEV_BLOB_LARGE_SIZE

typedef struct {
    uint16_t in_use[3];
    uint16_t capacity[3];
    uint16_t high_water[3];
    uint32_t alloc_failures;
} nev_blob_stats_t;

/* Allocates the backing buffers once, from PSRAM. Call during boot. */
nev_err_t nev_blob_pool_init(void);
void nev_blob_pool_deinit(void);

/*
 * Smallest class that fits len. Returns NEV_BLOB_NONE when the class is
 * exhausted or len exceeds NEV_BLOB_MAX_LEN — the caller must then drop the
 * frame, never block. Refcount starts at 1. *out receives the writable buffer.
 */
nev_blob_t nev_blob_alloc(size_t len, uint8_t **out);

uint8_t *nev_blob_data(nev_blob_t handle);
size_t nev_blob_len(nev_blob_t handle);

void nev_blob_retain(nev_blob_t handle);
void nev_blob_release(nev_blob_t handle);

/* Debug and test surface. */
uint32_t nev_blob_refcount(nev_blob_t handle);
void nev_blob_stats(nev_blob_stats_t *out);
bool nev_blob_all_free(void); /* leak assertion for test teardown */

/*
 * Warn about any blob held longer than max_age_ms. A missing release is the one
 * bug this design is vulnerable to; nev_sys calls this once a second in debug
 * builds so the bug is found the day it is written rather than in the field.
 */
void nev_blob_check_stale(uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_BLOB_H */
