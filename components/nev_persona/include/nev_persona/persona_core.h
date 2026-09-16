/*
 * NEVOS L3 — the mood machine.
 *
 * No LVGL, no event bus, and no clock of its own: every entry point takes
 * `now_ms`. A personality whose behaviour depends on wall-clock time and on a
 * random number generator is exactly the kind of thing that is untestable by
 * accident, so both are injected and the whole of it runs deterministically in
 * a unit test.
 */
#ifndef NEV_PERSONA_PERSONA_CORE_H
#define NEV_PERSONA_PERSONA_CORE_H

#include "nev_persona/face_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Randomised within this band so blinking never looks metronomic. */
#define NEV_BLINK_MIN_MS   2200u
#define NEV_BLINK_MAX_MS   6200u
#define NEV_BLINK_CLOSE_MS 70u
#define NEV_BLINK_OPEN_MS  110u

/* Idle gaze drifts to a new resting point on this cadence. */
#define NEV_GAZE_MIN_MS    3000u
#define NEV_GAZE_MAX_MS    7000u

/* Exposed so it can be allocated statically; treat the fields as private. */
typedef struct {
    nev_mood_t mood;      /* what it is expressing now                  */
    nev_mood_t rest_mood; /* what it returns to when a held mood ends   */
    uint8_t intensity;

    nev_face_params_t from;    /* transition start                      */
    nev_face_params_t to;      /* transition target                     */
    nev_face_params_t current; /* what the renderer should draw         */

    uint32_t tween_start_ms;
    uint32_t tween_ms;
    uint32_t hold_until_ms; /* 0 when the mood is a resting state       */

    uint32_t next_blink_ms;
    uint32_t blink_start_ms;
    bool blinking;

    uint32_t next_gaze_ms;
    float gaze_target_x, gaze_target_y;
    float gaze_x, gaze_y;
    uint32_t bob_origin_ms;

    uint32_t rng;
    uint32_t transitions; /* how many mood changes; for tests and tracing */
    uint32_t blinks;
} nev_persona_core_t;

void nev_persona_core_init(nev_persona_core_t *c, uint32_t now_ms, uint32_t seed);

/*
 * Retarget. Safe to call mid-transition: the tween restarts from wherever the
 * face currently is, so a mood change never snaps.
 *
 * intensity scales the mood away from idle — 255 is the full preset, 128 is
 * halfway there — which is what lets one event type produce a nudge and another
 * produce a reaction without needing two moods.
 */
/*
 * Starts the face shut, so the first thing it does is open its eyes.
 *
 * Called once at boot. It is not a mood — there is no "asleep" in the emotional
 * vocabulary and adding one would put a row in the preset table that no app can
 * ever ask for — it is the same tween machinery started from a closed face.
 *
 * `ms` is how long the eyes take to open. Slow enough to be noticed, short
 * enough that it is not in the way of a device someone just switched on.
 */
void nev_persona_core_wake(nev_persona_core_t *c, uint32_t ms, uint32_t now_ms);

void nev_persona_core_set_mood(nev_persona_core_t *c, nev_mood_t mood, uint8_t intensity,
                               uint32_t hold_ms, uint32_t now_ms);

/* Advances the tween, blink and idle drift. Returns what to draw. */
const nev_face_params_t *nev_persona_core_tick(nev_persona_core_t *c, uint32_t now_ms);

nev_mood_t nev_persona_core_mood(const nev_persona_core_t *c);
bool nev_persona_core_is_blinking(const nev_persona_core_t *c);
bool nev_persona_core_is_transitioning(const nev_persona_core_t *c, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PERSONA_PERSONA_CORE_H */
