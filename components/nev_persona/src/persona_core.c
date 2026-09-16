#include "nev_persona/persona_core.h"
#include <string.h>

/*
 * How long it takes to arrive at each mood. Not one number: dropping off to
 * sleep should take most of a second, while a celebration that eases in over
 * 700 ms has already missed the moment it was celebrating.
 */
static const uint16_t kEnterMs[NEV_MOOD_COUNT] = {
    [NEV_MOOD_IDLE] = 420,      [NEV_MOOD_CURIOUS] = 260,  [NEV_MOOD_HAPPY] = 280,
    [NEV_MOOD_FOCUSED] = 380,   [NEV_MOOD_SLEEPY] = 900,   [NEV_MOOD_CELEBRATING] = 200,
    [NEV_MOOD_CONCERNED] = 320, [NEV_MOOD_THINKING] = 300,
};

/*
 * How much idle motion each mood tolerates. Sleepy is still — a sleeping face
 * that keeps glancing around is not asleep — and focused is nearly so, because
 * concentration reads as an absence of motion.
 */
static const float kLiveliness[NEV_MOOD_COUNT] = {
    [NEV_MOOD_IDLE] = 1.0f,      [NEV_MOOD_CURIOUS] = 1.0f,  [NEV_MOOD_HAPPY] = 0.8f,
    [NEV_MOOD_FOCUSED] = 0.25f,  [NEV_MOOD_SLEEPY] = 0.0f,   [NEV_MOOD_CELEBRATING] = 0.9f,
    [NEV_MOOD_CONCERNED] = 0.6f, [NEV_MOOD_THINKING] = 0.9f,
};

static uint32_t rng_next(nev_persona_core_t *c) {
    /* xorshift32: deterministic from the seed, which is what makes idle
     * behaviour reproducible in a test. */
    uint32_t x = c->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return x;
}

static uint32_t rng_between(nev_persona_core_t *c, uint32_t lo, uint32_t hi) {
    return lo + (rng_next(c) % (hi - lo + 1u));
}

static float rng_signed(nev_persona_core_t *c, float magnitude) {
    return ((float)(rng_next(c) % 2001u) / 1000.0f - 1.0f) * magnitude;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Cubic ease-in-out: no sudden start, no sudden stop. */
static float ease_in_out(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return 4.0f * t * t * t;
    const float f = -2.0f * t + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

/* A cheap smooth hump for the head bob; no sinf, and periodic by construction. */
static float hump(float phase) {
    phase -= (float)(int)phase; /* fractional part, 0..1 */
    const float t = phase * 2.0f;
    return t < 1.0f ? ease_in_out(t) * 2.0f - 1.0f : (1.0f - ease_in_out(t - 1.0f)) * 2.0f - 1.0f;
}

static void schedule_blink(nev_persona_core_t *c, uint32_t now_ms) {
    c->next_blink_ms = now_ms + rng_between(c, NEV_BLINK_MIN_MS, NEV_BLINK_MAX_MS);
}

static void schedule_gaze(nev_persona_core_t *c, uint32_t now_ms) {
    c->next_gaze_ms = now_ms + rng_between(c, NEV_GAZE_MIN_MS, NEV_GAZE_MAX_MS);
    c->gaze_target_x = rng_signed(c, 0.30f);
    c->gaze_target_y = rng_signed(c, 0.18f);
}

void nev_persona_core_init(nev_persona_core_t *c, uint32_t now_ms, uint32_t seed) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->rng = seed ? seed : 0x9E3779B9u; /* xorshift is dead at zero */
    c->mood = NEV_MOOD_IDLE;
    c->rest_mood = NEV_MOOD_IDLE;
    c->intensity = 255;

    c->from = *nev_face_preset(NEV_MOOD_IDLE);
    c->to = c->from;
    c->current = c->from;
    c->tween_start_ms = now_ms;
    c->tween_ms = 1; /* already arrived */
    c->bob_origin_ms = now_ms;

    schedule_blink(c, now_ms);
    schedule_gaze(c, now_ms);
}

void nev_persona_core_wake(nev_persona_core_t *c, uint32_t ms, uint32_t now_ms) {
    if (!c || ms == 0) return;

    /*
     * Shut means eye_open at zero and lids level — not the sleepy preset, which
     * is a mood with a downward cast to everything and would read as the device
     * waking up unhappy about it.
     */
    c->from = *nev_face_preset(NEV_MOOD_IDLE);
    c->from.eye_open = 0.0f;
    c->from.brow_raise = 0.0f;
    c->to = *nev_face_preset(c->rest_mood);
    c->current = c->from;

    c->tween_start_ms = now_ms;
    c->tween_ms = ms;

    /* No blink for a moment: opening the eyes and immediately shutting them
     * again reads as a flicker rather than as waking up. */
    c->next_blink_ms = now_ms + ms + NEV_BLINK_MIN_MS;
}

void nev_persona_core_set_mood(nev_persona_core_t *c, nev_mood_t mood, uint8_t intensity,
                               uint32_t hold_ms, uint32_t now_ms) {
    if (!c || mood < 0 || mood >= NEV_MOOD_COUNT) return;

    /*
     * Start the new transition from wherever the face actually is, not from the
     * previous mood's preset. Retargeting mid-tween is the common case — a tap
     * during a mood change — and starting from the preset would snap.
     */
    c->from = c->current;

    /* Intensity scales the mood away from idle, so one mood covers a nudge and
     * a full reaction without needing two table rows. */
    const nev_face_params_t *target = nev_face_preset(mood);
    if (intensity >= 255) {
        c->to = *target;
    } else {
        nev_face_lerp(&c->to, nev_face_preset(NEV_MOOD_IDLE), target, (float)intensity / 255.0f);
    }

    c->tween_start_ms = now_ms;
    c->tween_ms = kEnterMs[mood] ? kEnterMs[mood] : 300u;
    c->intensity = intensity;

    if (c->mood != mood) c->transitions++;
    c->mood = mood;

    if (hold_ms > 0) {
        c->hold_until_ms = now_ms + hold_ms;
    } else {
        c->hold_until_ms = 0;
        c->rest_mood = mood;
    }
}

const nev_face_params_t *nev_persona_core_tick(nev_persona_core_t *c, uint32_t now_ms) {
    if (!c) return nev_face_preset(NEV_MOOD_IDLE);

    /* A held mood expires back to the resting one. Compared as a signed
     * difference so a 32-bit millisecond wrap does not strand the face. */
    if (c->hold_until_ms != 0 && (int32_t)(now_ms - c->hold_until_ms) >= 0) {
        const nev_mood_t back = c->rest_mood;
        c->hold_until_ms = 0;
        nev_persona_core_set_mood(c, back, 255, 0, now_ms);
    }

    /* --- the transition --- */
    const uint32_t elapsed = now_ms - c->tween_start_ms;
    const float raw = c->tween_ms ? (float)elapsed / (float)c->tween_ms : 1.0f;
    nev_face_lerp(&c->current, &c->from, &c->to, ease_in_out(clampf(raw, 0.0f, 1.0f)));

    const float live = kLiveliness[c->mood];

    /* --- blink --- */
    if (!c->blinking && (int32_t)(now_ms - c->next_blink_ms) >= 0) {
        c->blinking = true;
        c->blink_start_ms = now_ms;
        c->blinks++;
    }
    if (c->blinking) {
        const uint32_t t = now_ms - c->blink_start_ms;
        float shut = 0.0f;
        if (t < NEV_BLINK_CLOSE_MS) {
            shut = (float)t / (float)NEV_BLINK_CLOSE_MS;
        } else if (t < NEV_BLINK_CLOSE_MS + NEV_BLINK_OPEN_MS) {
            shut = 1.0f - (float)(t - NEV_BLINK_CLOSE_MS) / (float)NEV_BLINK_OPEN_MS;
        } else {
            c->blinking = false;
            schedule_blink(c, now_ms);
        }
        /* Multiplicative, so a blink during a squint narrows what is already
         * narrow instead of overriding the expression. */
        c->current.eye_open *= (1.0f - clampf(shut, 0.0f, 1.0f));
    }

    /* --- idle drift --- */
    if (live > 0.0f) {
        if ((int32_t)(now_ms - c->next_gaze_ms) >= 0) schedule_gaze(c, now_ms);

        /* Exponential approach: the eyes glide rather than step. */
        c->gaze_x += (c->gaze_target_x - c->gaze_x) * 0.02f;
        c->gaze_y += (c->gaze_target_y - c->gaze_y) * 0.02f;

        c->current.pupil_x = clampf(c->current.pupil_x + c->gaze_x * live, -1.0f, 1.0f);
        c->current.pupil_y = clampf(c->current.pupil_y + c->gaze_y * live, -1.0f, 1.0f);

        /* A slow breath-like bob so the device never looks frozen. */
        const float phase = (float)(now_ms - c->bob_origin_ms) / 5500.0f;
        c->current.head_y = clampf(c->current.head_y + hump(phase) * 0.035f * live, -1.0f, 1.0f);
    }

    return &c->current;
}

nev_mood_t nev_persona_core_mood(const nev_persona_core_t *c) {
    return c ? c->mood : NEV_MOOD_IDLE;
}

bool nev_persona_core_is_blinking(const nev_persona_core_t *c) {
    return c && c->blinking;
}

bool nev_persona_core_is_transitioning(const nev_persona_core_t *c, uint32_t now_ms) {
    if (!c) return false;
    return (now_ms - c->tween_start_ms) < c->tween_ms;
}
