/*
 * Reflex — the rules, with no LVGL.
 *
 * A reaction trainer. The target appears after an unpredictable delay; the
 * score is how fast you answer it.
 *
 * Two details decide whether this measures reaction time or just measures
 * mashing: the delay must be unpredictable, and tapping before the target
 * appears must cost something. Both are here.
 */
#ifndef REFLEX_CORE_H
#define REFLEX_CORE_H

#include "nev_port/nev_types.h"

#define RX_ROUNDS           5

/* Unpredictable enough that the delay cannot be learned or anticipated. */
#define RX_DELAY_MIN_MS     900u
#define RX_DELAY_MAX_MS     2600u

/* Anything faster than this is a guess that happened to land, not a reaction:
 * human simple visual reaction time does not go below about 100 ms. */
#define RX_HUMAN_FLOOR_MS   100u

/* Score per round: faster is worth more, and slower than this is worth nothing. */
#define RX_SCORE_CEILING_MS 600u

typedef enum {
    RX_WAITING = 0, /* counting down to the target   */
    RX_ARMED,       /* target visible, timing        */
    RX_FEEDBACK,    /* showing the round's result    */
    RX_FINISHED,
} rx_state_t;

typedef enum {
    RX_TAP_IGNORED = 0,
    RX_TAP_TOO_SOON,
    RX_TAP_COUNTED,
} rx_tap_t;

typedef struct {
    rx_state_t state;
    uint32_t timer_ms; /* remaining delay, or elapsed since arming */
    uint32_t hold_ms;  /* feedback dwell                            */
    uint8_t round;     /* 0-based, counts completed rounds          */
    uint16_t result[RX_ROUNDS];
    bool voided[RX_ROUNDS];
    uint16_t last_ms;
    bool last_too_soon;
} reflex_core_t;

void reflex_core_reset(reflex_core_t *c, uint32_t (*rand_fn)(void *, uint32_t), void *ctx);

/* Advances the clock. Returns true on the frame the target appears. */
bool reflex_core_step(reflex_core_t *c, uint32_t dt_ms, uint32_t (*rand_fn)(void *, uint32_t),
                      void *ctx);

rx_tap_t reflex_core_tap(reflex_core_t *c);

/* Mean of the counted rounds, or 0 if none counted. */
uint16_t reflex_core_average(const reflex_core_t *c);
uint16_t reflex_core_best(const reflex_core_t *c);
uint32_t reflex_core_score(const reflex_core_t *c);

#endif /* REFLEX_CORE_H */
