/*
 * Simulator-only board extensions: input injection and framebuffer capture.
 *
 * This header exists so that headless verification and future replay tests can
 * drive the board without a window. It is not compiled into device builds and
 * nothing in components/ above L0 or in apps/ may include it.
 */
#ifndef NEV_BOARD_BOARD_SIM_H
#define NEV_BOARD_BOARD_SIM_H

#include "nev_board/board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_SIM_DISPLAY_SDL2 = 0, /* a real window */
    NEV_SIM_DISPLAY_HEADLESS, /* framebuffer only; what CI runs */
} nev_sim_display_kind_t;

nev_sim_display_kind_t nev_board_sim_display_kind(void);

/* Write the current framebuffer as a binary PPM (P6). */
nev_err_t nev_board_sim_save_ppm(const char *path);

/* Injected state is returned by the matching nev_board_* read on the next poll. */
void nev_board_sim_inject_touch(int16_t x, int16_t y, nev_touch_action_t action);
void nev_board_sim_inject_buttons(uint8_t mask);
void nev_board_sim_inject_imu(const nev_imu_sample_t *sample);
void nev_board_sim_request_quit(void);

/* Non-zero once anything has been flushed; lets a headless run assert it drew. */
uint32_t nev_board_sim_flush_count(void);
uint32_t nev_board_sim_pixels_written(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BOARD_BOARD_SIM_H */
