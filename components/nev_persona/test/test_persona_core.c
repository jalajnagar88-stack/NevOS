/*
 * The mood machine, driven by a fake clock.
 *
 * The whole point of keeping persona_core free of LVGL, the bus and any clock
 * of its own is that NEVOS's personality — transitions, blinking, idle drift —
 * can be asserted on exactly, with no display, no sleeps and no flakiness.
 */
#include "nev_persona/persona_core.h"
#include "unity.h"
#include <math.h>
#include <string.h>

static nev_persona_core_t g;

void setUp(void) {
    nev_persona_core_init(&g, 0, 12345u);
}
void tearDown(void) {
}

/* Advance the fake clock in frame-sized steps, as the render task would. */
static const nev_face_params_t *run_for(nev_persona_core_t *c, uint32_t *now_ms, uint32_t span_ms) {
    const nev_face_params_t *p = NULL;
    const uint32_t end = *now_ms + span_ms;
    while (*now_ms < end) {
        *now_ms += 33;
        p = nev_persona_core_tick(c, *now_ms);
    }
    return p;
}

static void test_starts_idle_and_settled(void) {
    TEST_ASSERT_EQUAL_INT(NEV_MOOD_IDLE, nev_persona_core_mood(&g));
    uint32_t now = 0;
    const nev_face_params_t *p = nev_persona_core_tick(&g, now);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, nev_face_preset(NEV_MOOD_IDLE)->eye_open, p->eye_open);
}

static void test_mood_change_arrives_at_the_preset(void) {
    uint32_t now = 0;
    nev_persona_core_set_mood(&g, NEV_MOOD_CELEBRATING, 255, 0, now);
    TEST_ASSERT_EQUAL_INT(NEV_MOOD_CELEBRATING, nev_persona_core_mood(&g));

    const nev_face_params_t *p = run_for(&g, &now, 1500);
    const nev_face_params_t *want = nev_face_preset(NEV_MOOD_CELEBRATING);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, want->mouth_curve, p->mouth_curve);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, want->eye_scale, p->eye_scale);
}

/* Never instant: the brief requires tweened transitions, so assert the face is
 * genuinely between the two states partway through. */
static void test_transition_is_tweened_not_instant(void) {
    uint32_t now = 0;
    const nev_face_params_t idle = *nev_face_preset(NEV_MOOD_IDLE);
    const nev_face_params_t *sleepy = nev_face_preset(NEV_MOOD_SLEEPY);

    nev_persona_core_set_mood(&g, NEV_MOOD_SLEEPY, 255, 0, now);
    const nev_face_params_t *p = run_for(&g, &now, 300); /* sleepy takes 900 ms */

    TEST_ASSERT_TRUE_MESSAGE(nev_persona_core_is_transitioning(&g, now), "should still be moving");
    TEST_ASSERT_TRUE_MESSAGE(p->eye_open < idle.eye_open - 0.01f, "has not started closing");
    TEST_ASSERT_TRUE_MESSAGE(p->eye_open > sleepy->eye_open + 0.01f, "arrived instantly");
}

/* The common case: a tap lands while a mood change is already running. */
static void test_retarget_midway_does_not_snap(void) {
    uint32_t now = 0;
    nev_persona_core_set_mood(&g, NEV_MOOD_SLEEPY, 255, 0, now);
    const nev_face_params_t before = *run_for(&g, &now, 400);

    nev_persona_core_set_mood(&g, NEV_MOOD_CELEBRATING, 255, 0, now);
    const nev_face_params_t *after = nev_persona_core_tick(&g, now);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.02f, before.eye_open, after->eye_open,
                                     "face jumped when the mood was retargeted");
}

static void test_held_mood_returns_to_the_resting_one(void) {
    uint32_t now = 0;
    nev_persona_core_set_mood(&g, NEV_MOOD_FOCUSED, 255, 0, now); /* new resting state */
    run_for(&g, &now, 600);

    nev_persona_core_set_mood(&g, NEV_MOOD_HAPPY, 255, 1000, now); /* temporary */
    TEST_ASSERT_EQUAL_INT(NEV_MOOD_HAPPY, nev_persona_core_mood(&g));

    run_for(&g, &now, 600);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_MOOD_HAPPY, nev_persona_core_mood(&g), "expired early");

    run_for(&g, &now, 900);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_MOOD_FOCUSED, nev_persona_core_mood(&g),
                                  "did not return to the resting mood");
}

static void test_intensity_scales_the_mood_toward_idle(void) {
    uint32_t now = 0;
    const nev_face_params_t *idle = nev_face_preset(NEV_MOOD_IDLE);
    const nev_face_params_t *full = nev_face_preset(NEV_MOOD_CELEBRATING);

    nev_persona_core_set_mood(&g, NEV_MOOD_CELEBRATING, 128, 0, now);
    const nev_face_params_t *p = run_for(&g, &now, 1200);

    const float halfway =
        idle->mouth_curve + (full->mouth_curve - idle->mouth_curve) * (128.0f / 255.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, halfway, p->mouth_curve);
}

/* --------------------------------------------------------------------- blink */

static void test_it_blinks_and_the_eyes_actually_close(void) {
    uint32_t now = 0;
    float min_open = 1.0f;
    uint32_t saw_blinking = 0;

    while (now < 20000) {
        now += 16; /* finer than a frame so a 70 ms close is not stepped over */
        const nev_face_params_t *p = nev_persona_core_tick(&g, now);
        if (nev_persona_core_is_blinking(&g)) saw_blinking++;
        if (p->eye_open < min_open) min_open = p->eye_open;
    }

    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(2, g.blinks, "should blink several times in 20 s");
    TEST_ASSERT_GREATER_THAN_UINT32(0, saw_blinking);
    TEST_ASSERT_TRUE_MESSAGE(min_open < 0.1f, "blink never actually shut the eyes");
}

/* A metronome does not read as alive. */
static void test_blink_intervals_are_not_uniform(void) {
    uint32_t now = 0;
    uint32_t last = 0;
    uint32_t gaps[6];
    uint32_t n = 0;
    uint32_t seen = 0;

    while (now < 60000 && n < 6) {
        now += 16;
        nev_persona_core_tick(&g, now);
        if (g.blinks > seen) {
            seen = g.blinks;
            if (last) gaps[n++] = now - last;
            last = now;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(4, n, "not enough blinks to judge");

    bool varied = false;
    for (uint32_t i = 1; i < n; i++) {
        if (gaps[i] > gaps[0] + 200 || gaps[i] + 200 < gaps[0]) varied = true;
        TEST_ASSERT_TRUE_MESSAGE(gaps[i] >= NEV_BLINK_MIN_MS - 100, "blinked too soon");
        TEST_ASSERT_TRUE_MESSAGE(gaps[i] <= NEV_BLINK_MAX_MS + 400, "blinked too late");
    }
    TEST_ASSERT_TRUE_MESSAGE(varied, "blink interval is metronomic");
}

/* ---------------------------------------------------------------- idle drift */

static void test_idle_face_keeps_moving(void) {
    uint32_t now = 0;
    float min_y = 9.0f, max_y = -9.0f;

    for (int i = 0; i < 900; i++) {
        now += 33;
        const nev_face_params_t *p = nev_persona_core_tick(&g, now);
        if (p->head_y < min_y) min_y = p->head_y;
        if (p->head_y > max_y) max_y = p->head_y;
    }
    TEST_ASSERT_TRUE_MESSAGE(max_y - min_y > 0.01f, "idle face is frozen");
}

/* A sleeping face that keeps glancing around is not asleep. */
static void test_sleepy_face_is_still(void) {
    uint32_t now = 0;
    nev_persona_core_set_mood(&g, NEV_MOOD_SLEEPY, 255, 0, now);
    run_for(&g, &now, 1500);

    const nev_face_params_t settled = *nev_persona_core_tick(&g, now);
    float drift = 0.0f;
    for (int i = 0; i < 300; i++) {
        now += 33;
        const nev_face_params_t *p = nev_persona_core_tick(&g, now);
        if (nev_persona_core_is_blinking(&g)) continue; /* blinking is allowed */
        drift = fmaxf(drift, fabsf(p->head_y - settled.head_y));
        drift = fmaxf(drift, fabsf(p->pupil_x - settled.pupil_x));
    }
    TEST_ASSERT_TRUE_MESSAGE(drift < 0.01f, "sleepy face is fidgeting");
}

/* ------------------------------------------------------------- determinism */

static void test_same_seed_gives_the_same_behaviour(void) {
    nev_persona_core_t a, b;
    nev_persona_core_init(&a, 0, 999u);
    nev_persona_core_init(&b, 0, 999u);

    for (uint32_t t = 0; t < 30000; t += 33) {
        const nev_face_params_t pa = *nev_persona_core_tick(&a, t);
        const nev_face_params_t pb = *nev_persona_core_tick(&b, t);
        TEST_ASSERT_EQUAL_MEMORY(&pa, &pb, sizeof(pa));
    }
    TEST_ASSERT_EQUAL_UINT32(a.blinks, b.blinks);
}

static void test_different_seeds_diverge(void) {
    nev_persona_core_t a, b;
    nev_persona_core_init(&a, 0, 111u);
    nev_persona_core_init(&b, 0, 222u);
    for (uint32_t t = 0; t < 30000; t += 33) {
        nev_persona_core_tick(&a, t);
        nev_persona_core_tick(&b, t);
    }
    TEST_ASSERT_TRUE_MESSAGE(a.next_blink_ms != b.next_blink_ms, "seeds produced identical timing");
}

/*
 * A device left running for 49 days wraps its 32-bit millisecond clock. The
 * face must not freeze or get stuck in a held mood when it does.
 */
static void test_survives_the_millisecond_wrap(void) {
    nev_persona_core_t c;
    uint32_t now = 0xFFFFF000u;
    nev_persona_core_init(&c, now, 7u);

    nev_persona_core_set_mood(&c, NEV_MOOD_HAPPY, 255, 2000, now);
    for (int i = 0; i < 400; i++) { /* ~13 s, straight through the wrap */
        now += 33;
        nev_persona_core_tick(&c, now);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_MOOD_IDLE, nev_persona_core_mood(&c),
                                  "held mood never expired across the wrap");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, c.blinks, "stopped blinking across the wrap");
}

static void test_invalid_mood_is_ignored(void) {
    nev_persona_core_set_mood(&g, (nev_mood_t)77, 255, 0, 0);
    TEST_ASSERT_EQUAL_INT(NEV_MOOD_IDLE, nev_persona_core_mood(&g));
    nev_persona_core_set_mood(NULL, NEV_MOOD_HAPPY, 255, 0, 0);
    TEST_ASSERT_NOT_NULL(nev_persona_core_tick(NULL, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle_and_settled);
    RUN_TEST(test_mood_change_arrives_at_the_preset);
    RUN_TEST(test_transition_is_tweened_not_instant);
    RUN_TEST(test_retarget_midway_does_not_snap);
    RUN_TEST(test_held_mood_returns_to_the_resting_one);
    RUN_TEST(test_intensity_scales_the_mood_toward_idle);
    RUN_TEST(test_it_blinks_and_the_eyes_actually_close);
    RUN_TEST(test_blink_intervals_are_not_uniform);
    RUN_TEST(test_idle_face_keeps_moving);
    RUN_TEST(test_sleepy_face_is_still);
    RUN_TEST(test_same_seed_gives_the_same_behaviour);
    RUN_TEST(test_different_seeds_diverge);
    RUN_TEST(test_survives_the_millisecond_wrap);
    RUN_TEST(test_invalid_mood_is_ignored);
    return UNITY_END();
}
