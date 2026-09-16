#include "reflex_core.h"
#include <string.h>

#define FEEDBACK_MS 900u

static void begin_wait(reflex_core_t *c, uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    c->state = RX_WAITING;
    const uint32_t span = RX_DELAY_MAX_MS - RX_DELAY_MIN_MS;
    c->timer_ms = RX_DELAY_MIN_MS + (rand_fn ? rand_fn(ctx, span) : span / 2);
}

void reflex_core_reset(reflex_core_t *c, uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    begin_wait(c, rand_fn, ctx);
}

bool reflex_core_step(reflex_core_t *c, uint32_t dt_ms, uint32_t (*rand_fn)(void *, uint32_t),
                      void *ctx) {
    if (!c) return false;

    switch (c->state) {
        case RX_WAITING:
            if (dt_ms >= c->timer_ms) {
                c->state = RX_ARMED;
                c->timer_ms = 0;
                return true; /* the target is now visible */
            }
            c->timer_ms -= dt_ms;
            return false;

        case RX_ARMED:
            c->timer_ms += dt_ms;
            /* No timeout: a round that is never answered simply stays open.
             * Failing someone for looking away is not measuring anything. */
            return false;

        case RX_FEEDBACK:
            if (dt_ms >= c->hold_ms) {
                if (c->round >= RX_ROUNDS) {
                    c->state = RX_FINISHED;
                } else {
                    begin_wait(c, rand_fn, ctx);
                }
            } else {
                c->hold_ms -= dt_ms;
            }
            return false;

        case RX_FINISHED:
            return false;
    }
    return false;
}

static void finish_round(reflex_core_t *c, uint16_t ms, bool voided) {
    if (c->round < RX_ROUNDS) {
        c->result[c->round] = ms;
        c->voided[c->round] = voided;
        c->round++;
    }
    c->last_ms = ms;
    c->last_too_soon = voided;
    c->state = RX_FEEDBACK;
    c->hold_ms = FEEDBACK_MS;
}

rx_tap_t reflex_core_tap(reflex_core_t *c) {
    if (!c) return RX_TAP_IGNORED;

    switch (c->state) {
        case RX_WAITING:
            /* Tapping before the target appears voids the round. Without a cost,
             * the optimal strategy is to mash, and the game measures nothing. */
            finish_round(c, 0, true);
            return RX_TAP_TOO_SOON;

        case RX_ARMED: {
            uint32_t ms = c->timer_ms;
            /*
             * Below the human floor the player did not react, they guessed and
             * got lucky. Voiding it keeps the high-score table meaningful.
             */
            if (ms < RX_HUMAN_FLOOR_MS) {
                finish_round(c, (uint16_t)ms, true);
                return RX_TAP_TOO_SOON;
            }
            if (ms > 0xFFFFu) ms = 0xFFFFu;
            finish_round(c, (uint16_t)ms, false);
            return RX_TAP_COUNTED;
        }

        case RX_FEEDBACK:
        case RX_FINISHED:
            return RX_TAP_IGNORED;
    }
    return RX_TAP_IGNORED;
}

uint16_t reflex_core_average(const reflex_core_t *c) {
    if (!c) return 0;
    uint32_t sum = 0, n = 0;
    for (uint8_t i = 0; i < c->round && i < RX_ROUNDS; i++) {
        if (c->voided[i]) continue;
        sum += c->result[i];
        n++;
    }
    return n ? (uint16_t)(sum / n) : 0;
}

uint16_t reflex_core_best(const reflex_core_t *c) {
    if (!c) return 0;
    uint16_t best = 0;
    for (uint8_t i = 0; i < c->round && i < RX_ROUNDS; i++) {
        if (c->voided[i]) continue;
        if (best == 0 || c->result[i] < best) best = c->result[i];
    }
    return best;
}

uint32_t reflex_core_score(const reflex_core_t *c) {
    if (!c) return 0;
    /*
     * Faster is worth more, so the shared high-score table — which ranks by
     * larger-is-better — works without a special case. A voided round scores
     * nothing, which is the whole point of voiding it.
     */
    uint32_t score = 0;
    for (uint8_t i = 0; i < c->round && i < RX_ROUNDS; i++) {
        if (c->voided[i]) continue;
        if (c->result[i] < RX_SCORE_CEILING_MS) score += RX_SCORE_CEILING_MS - c->result[i];
    }
    return score;
}
