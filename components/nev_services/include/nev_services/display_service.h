/*
 * NEVOS L2 — display service.
 *
 * Owns LVGL: initialisation, the draw-buffer strategy, the flush path down to
 * L0, and frame-time instrumentation. Nothing above this layer calls LVGL
 * setup functions or touches a framebuffer.
 *
 * Threading: LVGL 9 is not thread-safe and NEVOS does not make it so. Every
 * lv_* call, including those made by apps, happens on the single render task.
 * See ARCHITECTURE.md §4 for why that is a choice rather than an omission.
 */
#ifndef NEV_SERVICES_DISPLAY_SERVICE_H
#define NEV_SERVICES_DISPLAY_SERVICE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frames;
    uint32_t overruns; /* frames that exceeded the budget          */
    uint32_t last_us;  /* wall time of the most recent frame       */
    uint32_t worst_us; /* worst frame since init — the BUDGET.md number */
    uint32_t avg_us;   /* rolling mean                             */
    uint16_t fps_q4;   /* measured fps in 1/16ths                  */
} display_stats_t;

nev_err_t display_service_init(void);
void display_service_deinit(void);

/*
 * Render one frame and pace to the target rate. Runs LVGL's timers, measures
 * the frame, publishes DISPLAY.FRAME_STATS once a second, and sleeps out the
 * remainder of the budget. Returns false when the board has been asked to quit.
 */
bool display_service_frame(void);

void display_service_stats(display_stats_t *out);

/* Budget in microseconds for one frame, from the board's target frame rate. */
uint32_t display_service_frame_budget_us(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_DISPLAY_SERVICE_H */
