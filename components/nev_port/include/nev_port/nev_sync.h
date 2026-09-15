/* NEVOS L-1 — mutex and counting semaphore.
 *
 * Both are handle-carrying structs rather than opaque pointers so callers can
 * embed them in statically allocated objects (the bus rings do exactly this).
 * The underlying object is created once at init; no allocation on the hot path.
 */
#ifndef NEV_PORT_NEV_SYNC_H
#define NEV_PORT_NEV_SYNC_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *impl;
} nev_mutex_t;

typedef struct {
    void *impl;
} nev_sem_t;

#define NEV_MUTEX_INIT                                                                             \
    { NULL }
#define NEV_SEM_INIT                                                                               \
    { NULL }

nev_err_t nev_mutex_init(nev_mutex_t *m);
void nev_mutex_deinit(nev_mutex_t *m);
void nev_mutex_lock(nev_mutex_t *m);
void nev_mutex_unlock(nev_mutex_t *m);

/* Counting semaphore, starts empty, saturates at max_count. */
nev_err_t nev_sem_init(nev_sem_t *s, uint32_t max_count);
void nev_sem_deinit(nev_sem_t *s);
void nev_sem_give(nev_sem_t *s);
bool nev_sem_take(nev_sem_t *s, uint32_t timeout_ms); /* false on timeout */

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_SYNC_H */
