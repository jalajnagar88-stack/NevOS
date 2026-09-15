/*
 * NEVOS L3 — the persona.
 *
 * Split in two on purpose:
 *
 *   persona_core  the mood machine, the tween, blink timing and idle drift.
 *                 Pure logic, no LVGL, no bus, and it takes the current time as
 *                 an argument rather than reading a clock. That is what lets
 *                 the whole of NEVOS's personality be unit-tested deterministically
 *                 with a fake clock and no display.
 *
 *   persona       binds the core to the event bus and to a face renderer.
 *
 * Only persona.h knows about either LVGL or the bus; persona_core.h knows about
 * neither.
 */
#ifndef NEV_PERSONA_PERSONA_H
#define NEV_PERSONA_PERSONA_H

#include "nev_persona/face.h"
#include "nev_persona/persona_core.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the face under `parent` and subscribes to the bus. */
nev_err_t nev_persona_init(lv_obj_t *parent);
void nev_persona_deinit(void);

/*
 * Called once per frame from the render task. Drains the persona's bus queue,
 * advances the mood machine, and applies the result to the face. Bounded work:
 * no allocation, no blocking, no more than a fixed number of events per call.
 */
void nev_persona_tick(uint32_t now_ms);

/*
 * The one imperative entry point, as promised in ARCHITECTURE.md §L3.
 *
 * Callable from L4 and L5 — an app driving the face is a downward call and
 * therefore legal. It is NOT callable from another L3 module: nev_bridge drives
 * the face by publishing BRIDGE.MOOD_HINT, which persona subscribes to, so the
 * no-sideways-calls rule holds.
 *
 * hold_ms of 0 makes the mood the new resting state; non-zero returns to the
 * previous resting mood afterwards.
 */
void nev_persona_set_mood(nev_mood_t mood, uint8_t intensity, uint32_t hold_ms);

nev_mood_t nev_persona_mood(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PERSONA_PERSONA_H */
