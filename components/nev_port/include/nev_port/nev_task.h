/* NEVOS L-1 — tasks.
 *
 * Priorities and core pinning are named here rather than passed as raw numbers,
 * so the task topology in ARCHITECTURE.md §4 and the code cannot drift.
 */
#ifndef NEV_PORT_NEV_TASK_H
#define NEV_PORT_NEV_TASK_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*nev_task_fn_t)(void *arg);

typedef struct {
    void *impl;
} nev_task_t;

/* Ascending urgency. Mapped onto the platform's own range by the port. */
typedef enum {
    NEV_PRIO_IDLE = 0,
    NEV_PRIO_LOW = 2,     /* nev_sys */
    NEV_PRIO_NORMAL = 3,  /* nev_work */
    NEV_PRIO_NET = 4,     /* nev_net */
    NEV_PRIO_UI = 5,      /* nev_ui  — render + apps */
    NEV_PRIO_IO = 7,      /* nev_input, nev_audio_out */
    NEV_PRIO_REALTIME = 8 /* nev_audio_in */
} nev_prio_t;

#define NEV_CORE_ANY (-1)
#define NEV_CORE_IO  0 /* radios, audio, filesystem */
#define NEV_CORE_UI  1 /* kept quiet for rendering */

typedef struct {
    const char   *name;
    nev_task_fn_t fn;
    void         *arg;
    size_t        stack_bytes;
    nev_prio_t    priority;
    int8_t        core;
} nev_task_cfg_t;

nev_err_t nev_task_create(nev_task_t *out, const nev_task_cfg_t *cfg);
void      nev_task_yield(void);
size_t    nev_task_stack_high_water(const nev_task_t *t); /* bytes still unused */
const char *nev_task_self_name(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_TASK_H */
