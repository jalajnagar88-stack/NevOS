/*
 * Headless display backend: no window, no SDL2, no display server.
 *
 * This is what CI runs and what a development container can verify: the render
 * path still executes end to end, and nev_board_sim_save_ppm captures the
 * result for inspection. A compile-only check would not catch a flush that
 * writes nothing.
 */
#include "sim_backend.h"
#include "nev_port/nev_log.h"

#define TAG "headless"

static const uint16_t *s_fb;

nev_err_t sim_backend_init(const uint16_t *fb, int width, int height) {
    s_fb = fb;
    NEV_LOGI(TAG, "headless display %dx%d — no window will open", width, height);
    return NEV_OK;
}

void sim_backend_deinit(void) {
    s_fb = NULL;
}
void sim_backend_present(void) { /* nothing to present to */
}
void sim_backend_poll(void) { /* input arrives via nev_board_sim_inject_* */
}

nev_sim_display_kind_t sim_backend_kind(void) {
    return NEV_SIM_DISPLAY_HEADLESS;
}
