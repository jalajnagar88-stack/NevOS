/*
 * NEVOS L4 — the shell.
 *
 * Owns the screen: a persistent status bar, the home grid, navigation between
 * them, and the lifecycle of the one foreground app. Apps never touch the
 * screen or the status bar; they get a root container and stay inside it.
 *
 * See docs/app-lifecycle.md.
 */
#ifndef NEV_APPKIT_SHELL_H
#define NEV_APPKIT_SHELL_H

#include "nev_appkit/app.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How many apps may hold their UI at once: one foreground plus two suspended.
 * Beyond that the least recently used is closed. Small on purpose — on a device
 * with 512 KB of internal memory, "keep everything warm" is how you run out.
 */
#ifndef NEV_SHELL_MAX_LIVE
#define NEV_SHELL_MAX_LIVE 3
#endif

nev_err_t nev_shell_init(lv_obj_t *screen);
void nev_shell_deinit(void);

/* Once per frame from the render task: drains the shell's bus queue, updates
 * the status bar, and forwards events and ticks to the foreground app. */
void nev_shell_tick(uint32_t now_ms);

nev_err_t nev_shell_launch(const char *app_id);
void nev_shell_go_home(void);

/* NULL when the home grid is showing. */
const char *nev_shell_foreground_id(void);
size_t nev_shell_live_count(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_APPKIT_SHELL_H */
