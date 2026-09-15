#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "nev_port/nev_task.h"
#include "nev_port/nev_assert.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_t th;
    char name[24];
    nev_task_fn_t fn;
    void *arg;
} host_task_t;

static __thread const char *s_self_name = "main";

static void *trampoline(void *p) {
    host_task_t *t = p;
    s_self_name = t->name;
    /* Thread naming is spelled differently on each platform, and is worth
     * having: it is what makes a backtrace readable. */
#if defined(__APPLE__)
    pthread_setname_np(t->name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), t->name);
#endif
    t->fn(t->arg);
    return NULL;
}

nev_err_t nev_task_create(nev_task_t *out, const nev_task_cfg_t *cfg) {
    NEV_REQUIRE(out && cfg && cfg->fn && cfg->name, NEV_ERR_INVALID_ARG);

    host_task_t *t = calloc(1, sizeof(*t));
    if (!t) return NEV_ERR_NO_MEM;
    snprintf(t->name, sizeof(t->name), "%s", cfg->name);
    t->fn = cfg->fn;
    t->arg = cfg->arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (cfg->stack_bytes >= 16384) pthread_attr_setstacksize(&attr, cfg->stack_bytes);
    int rc = pthread_create(&t->th, &attr, trampoline, t);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        free(t);
        return NEV_ERR_NO_MEM;
    }
    /*
     * Priority and core pinning are deliberately ignored on the host: the OS
     * scheduler is not FreeRTOS and pretending otherwise would let timing bugs
     * hide behind a simulation that does not match the device. Timing-sensitive
     * behavior is verified on hardware at M5.
     */
    out->impl = t;
    return NEV_OK;
}

void nev_task_yield(void) {
    sched_yield();
}

size_t nev_task_stack_high_water(const nev_task_t *t) {
    NEV_UNUSED(t);
    return 0; /* not measurable on pthreads; real numbers come from the device */
}

const char *nev_task_self_name(void) {
    return s_self_name;
}
