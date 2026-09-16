/* Reflex's rules, with no display and a controlled clock. */
#include "reflex_core.h"
#include "unity.h"

static reflex_core_t c;

static uint32_t mid_rand(void *ctx, uint32_t upper) {
    (void)ctx;
    return upper / 2;
}

void setUp(void) {
    reflex_core_reset(&c, mid_rand, NULL);
}
void tearDown(void) {
}

static void arm(void) {
    while (c.state == RX_WAITING)
        reflex_core_step(&c, 10, mid_rand, NULL);
}
static void wait_out_feedback(void) {
    while (c.state == RX_FEEDBACK)
        reflex_core_step(&c, 100, mid_rand, NULL);
}

static void test_starts_waiting_with_a_delay_in_range(void) {
    TEST_ASSERT_EQUAL_INT(RX_WAITING, c.state);
    TEST_ASSERT_TRUE(c.timer_ms >= RX_DELAY_MIN_MS);
    TEST_ASSERT_TRUE(c.timer_ms <= RX_DELAY_MAX_MS);
}

/* A learnable delay measures anticipation, not reaction. */
static uint32_t counter_rand(void *ctx, uint32_t upper) {
    uint32_t *n = ctx;
    *n += 617u; /* coprime-ish stride, so successive calls differ */
    return *n % upper;
}

static void test_the_delay_varies_between_rounds(void) {
    uint32_t seed = 0;
    uint32_t seen[4];

    for (int i = 0; i < 4; i++) {
        reflex_core_t r;
        reflex_core_reset(&r, counter_rand, &seed);
        seen[i] = r.timer_ms;
        TEST_ASSERT_TRUE(r.timer_ms >= RX_DELAY_MIN_MS);
        TEST_ASSERT_TRUE(r.timer_ms <= RX_DELAY_MAX_MS);
    }
    TEST_ASSERT_TRUE_MESSAGE(seen[0] != seen[1] && seen[1] != seen[2],
                             "the delay is the same every round");
}

static void test_the_target_arms_after_the_delay(void) {
    TEST_ASSERT_FALSE(reflex_core_step(&c, 10, mid_rand, NULL));
    bool armed = false;
    for (int i = 0; i < 500 && !armed; i++)
        armed = reflex_core_step(&c, 10, mid_rand, NULL);
    TEST_ASSERT_TRUE_MESSAGE(armed, "the target never appeared");
    TEST_ASSERT_EQUAL_INT(RX_ARMED, c.state);
}

static void test_a_counted_tap_records_the_elapsed_time(void) {
    arm();
    reflex_core_step(&c, 250, mid_rand, NULL);
    TEST_ASSERT_EQUAL_INT(RX_TAP_COUNTED, reflex_core_tap(&c));
    TEST_ASSERT_EQUAL_UINT16(250, c.last_ms);
    TEST_ASSERT_FALSE(c.last_too_soon);
    TEST_ASSERT_EQUAL_UINT8(1, c.round);
}

/* Without a cost for tapping early, the optimal strategy is to mash and the
 * game measures nothing. */
static void test_tapping_before_the_target_voids_the_round(void) {
    TEST_ASSERT_EQUAL_INT(RX_WAITING, c.state);
    TEST_ASSERT_EQUAL_INT(RX_TAP_TOO_SOON, reflex_core_tap(&c));
    TEST_ASSERT_TRUE(c.voided[0]);
    TEST_ASSERT_EQUAL_UINT8(1, c.round);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, reflex_core_score(&c), "a voided round scored");
}

/* Faster than human reaction is a guess that landed, not a reaction. */
static void test_an_impossibly_fast_tap_is_voided(void) {
    arm();
    reflex_core_step(&c, RX_HUMAN_FLOOR_MS - 20, mid_rand, NULL);
    TEST_ASSERT_EQUAL_INT(RX_TAP_TOO_SOON, reflex_core_tap(&c));
    TEST_ASSERT_TRUE(c.voided[0]);
}

static void test_taps_during_feedback_are_ignored(void) {
    arm();
    reflex_core_step(&c, 300, mid_rand, NULL);
    reflex_core_tap(&c);
    TEST_ASSERT_EQUAL_INT(RX_FEEDBACK, c.state);
    TEST_ASSERT_EQUAL_INT(RX_TAP_IGNORED, reflex_core_tap(&c));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, c.round, "a stray tap consumed a round");
}

static void test_an_unanswered_round_stays_open(void) {
    arm();
    for (int i = 0; i < 200; i++)
        reflex_core_step(&c, 100, mid_rand, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RX_ARMED, c.state, "the round timed out on its own");
    TEST_ASSERT_EQUAL_INT(RX_TAP_COUNTED, reflex_core_tap(&c));
}

static void test_the_session_ends_after_every_round(void) {
    for (int r = 0; r < RX_ROUNDS; r++) {
        arm();
        reflex_core_step(&c, 200 + (uint32_t)r * 10, mid_rand, NULL);
        TEST_ASSERT_EQUAL_INT(RX_TAP_COUNTED, reflex_core_tap(&c));
        wait_out_feedback();
    }
    TEST_ASSERT_EQUAL_INT(RX_FINISHED, c.state);
    TEST_ASSERT_EQUAL_UINT8(RX_ROUNDS, c.round);
    TEST_ASSERT_EQUAL_INT(RX_TAP_IGNORED, reflex_core_tap(&c));
}

static void test_average_and_best_ignore_voided_rounds(void) {
    c.round = 3;
    c.result[0] = 200;
    c.voided[0] = false;
    c.result[1] = 0;
    c.voided[1] = true; /* voided: must not drag the mean to 0 */
    c.result[2] = 400;
    c.voided[2] = false;

    TEST_ASSERT_EQUAL_UINT16(300, reflex_core_average(&c));
    TEST_ASSERT_EQUAL_UINT16(200, reflex_core_best(&c));
}

/* Faster must score higher, so the shared larger-is-better table works. */
static void test_faster_scores_higher(void) {
    reflex_core_t fast = {0}, slow = {0};
    fast.round = 1;
    fast.result[0] = 180;
    slow.round = 1;
    slow.result[0] = 420;
    TEST_ASSERT_TRUE(reflex_core_score(&fast) > reflex_core_score(&slow));

    reflex_core_t sluggish = {0};
    sluggish.round = 1;
    sluggish.result[0] = RX_SCORE_CEILING_MS + 100;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, reflex_core_score(&sluggish), "a slow round scored");
}

static void test_null_is_survivable(void) {
    reflex_core_reset(NULL, mid_rand, NULL);
    TEST_ASSERT_FALSE(reflex_core_step(NULL, 10, mid_rand, NULL));
    TEST_ASSERT_EQUAL_INT(RX_TAP_IGNORED, reflex_core_tap(NULL));
    TEST_ASSERT_EQUAL_UINT16(0, reflex_core_average(NULL));
    TEST_ASSERT_EQUAL_UINT16(0, reflex_core_best(NULL));
    TEST_ASSERT_EQUAL_UINT32(0, reflex_core_score(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_waiting_with_a_delay_in_range);
    RUN_TEST(test_the_delay_varies_between_rounds);
    RUN_TEST(test_the_target_arms_after_the_delay);
    RUN_TEST(test_a_counted_tap_records_the_elapsed_time);
    RUN_TEST(test_tapping_before_the_target_voids_the_round);
    RUN_TEST(test_an_impossibly_fast_tap_is_voided);
    RUN_TEST(test_taps_during_feedback_are_ignored);
    RUN_TEST(test_an_unanswered_round_stays_open);
    RUN_TEST(test_the_session_ends_after_every_round);
    RUN_TEST(test_average_and_best_ignore_voided_rounds);
    RUN_TEST(test_faster_scores_higher);
    RUN_TEST(test_null_is_survivable);
    return UNITY_END();
}
