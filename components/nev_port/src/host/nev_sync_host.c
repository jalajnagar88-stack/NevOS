#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "nev_port/nev_sync.h"
#include "nev_port/nev_assert.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "nev_port/nev_time.h"

typedef struct {
    pthread_mutex_t m;
} host_mutex_t;

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    uint32_t count;
    uint32_t max_count;
} host_sem_t;

nev_err_t nev_mutex_init(nev_mutex_t *m) {
    NEV_REQUIRE(m != NULL, NEV_ERR_INVALID_ARG);
    host_mutex_t *h = calloc(1, sizeof(*h));
    if (!h) return NEV_ERR_NO_MEM;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&h->m, &attr);
    pthread_mutexattr_destroy(&attr);
    m->impl = h;
    return NEV_OK;
}

void nev_mutex_deinit(nev_mutex_t *m) {
    if (!m || !m->impl) return;
    host_mutex_t *h = m->impl;
    pthread_mutex_destroy(&h->m);
    free(h);
    m->impl = NULL;
}

void nev_mutex_lock(nev_mutex_t *m) {
    NEV_ASSERT(m && m->impl);
    pthread_mutex_lock(&((host_mutex_t *)m->impl)->m);
}

void nev_mutex_unlock(nev_mutex_t *m) {
    NEV_ASSERT(m && m->impl);
    pthread_mutex_unlock(&((host_mutex_t *)m->impl)->m);
}

nev_err_t nev_sem_init(nev_sem_t *s, uint32_t max_count) {
    NEV_REQUIRE(s != NULL && max_count > 0, NEV_ERR_INVALID_ARG);
    host_sem_t *h = calloc(1, sizeof(*h));
    if (!h) return NEV_ERR_NO_MEM;
    pthread_mutex_init(&h->m, NULL);

    /*
     * Timeouts must be immune to wall-clock steps. glibc gets a condvar bound to
     * CLOCK_MONOTONIC; macOS has no pthread_condattr_setclock, so nev_sem_take
     * uses pthread_cond_timedwait_relative_np there instead.
     */
#if defined(__APPLE__)
    pthread_cond_init(&h->cv, NULL);
#else
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&h->cv, &attr);
    pthread_condattr_destroy(&attr);
#endif

    h->max_count = max_count;
    s->impl = h;
    return NEV_OK;
}

void nev_sem_deinit(nev_sem_t *s) {
    if (!s || !s->impl) return;
    host_sem_t *h = s->impl;
    pthread_cond_destroy(&h->cv);
    pthread_mutex_destroy(&h->m);
    free(h);
    s->impl = NULL;
}

void nev_sem_give(nev_sem_t *s) {
    NEV_ASSERT(s && s->impl);
    host_sem_t *h = s->impl;
    pthread_mutex_lock(&h->m);
    if (h->count < h->max_count) h->count++;
    pthread_cond_signal(&h->cv);
    pthread_mutex_unlock(&h->m);
}

bool nev_sem_take(nev_sem_t *s, uint32_t timeout_ms) {
    NEV_ASSERT(s && s->impl);
    host_sem_t *h = s->impl;
    bool got = false;

    pthread_mutex_lock(&h->m);
    if (timeout_ms == NEV_NO_WAIT) {
        got = h->count > 0;
    } else if (timeout_ms == NEV_WAIT_FOREVER) {
        while (h->count == 0)
            pthread_cond_wait(&h->cv, &h->m);
        got = true;
    } else {
        /* One absolute deadline, re-derived each pass, so spurious wakeups
         * cannot extend the caller's timeout. */
        const uint64_t deadline_us = nev_now_us() + (uint64_t)timeout_ms * 1000u;
        while (h->count == 0) {
            uint64_t now_us = nev_now_us();
            if (now_us >= deadline_us) break;
            uint64_t remain_us = deadline_us - now_us;
#if defined(__APPLE__)
            struct timespec rel = {.tv_sec = (time_t)(remain_us / 1000000u),
                                   .tv_nsec = (long)((remain_us % 1000000u) * 1000u)};
            if (pthread_cond_timedwait_relative_np(&h->cv, &h->m, &rel) == ETIMEDOUT) break;
#else
            struct timespec abs;
            clock_gettime(CLOCK_MONOTONIC, &abs);
            abs.tv_sec += (time_t)(remain_us / 1000000u);
            abs.tv_nsec += (long)((remain_us % 1000000u) * 1000u);
            if (abs.tv_nsec >= 1000000000L) {
                abs.tv_sec += 1;
                abs.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&h->cv, &h->m, &abs) == ETIMEDOUT) break;
#endif
        }
        got = h->count > 0;
    }
    if (got) h->count--;
    pthread_mutex_unlock(&h->m);
    return got;
}
