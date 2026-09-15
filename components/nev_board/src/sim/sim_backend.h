/* Internal contract between board_sim.c and whichever display backend is built. */
#ifndef NEV_SIM_BACKEND_H
#define NEV_SIM_BACKEND_H

#include "nev_board/board_sim.h"

nev_err_t sim_backend_init(const uint16_t *fb, int width, int height);
void sim_backend_deinit(void);
void sim_backend_present(void); /* push the framebuffer to the window */
void sim_backend_poll(void);    /* pump input; injects via board_sim */
nev_sim_display_kind_t sim_backend_kind(void);

#endif /* NEV_SIM_BACKEND_H */
