/*
 * Push-to-talk.
 *
 * Every case here is a way somebody holds a button wrong, and each one is
 * awkward to stage on hardware and instant here: the brush, the tap, the
 * nervous half-second, and the one where the device ends up under a book.
 */
#include "nev_services/talk_core.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define ARM_MS 120u
#define MIN_MS 350u
#define MAX_MS 30000u

static nev_ptt_t make(void) {
    const nev_ptt_cfg_t cfg = {.arm_ms = ARM_MS, .min_speech_ms = MIN_MS, .max_ms = MAX_MS};
    nev_ptt_t p;
    nev_ptt_init(&p, &cfg);
    return p;
}

/* Presses at `at` and runs the clock until the microphone opens. */
static void press_and_arm(nev_ptt_t *p, uint32_t at) {
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(p, at));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_START, nev_ptt_tick(p, at + ARM_MS));
    TEST_ASSERT_TRUE(nev_ptt_is_talking(p));
}

static void test_the_defaults_are_the_documented_ones(void) {
    nev_ptt_cfg_t cfg;
    nev_ptt_defaults(&cfg);
    TEST_ASSERT_EQUAL_UINT32(120u, cfg.arm_ms);
    TEST_ASSERT_EQUAL_UINT32(350u, cfg.min_speech_ms);
    TEST_ASSERT_EQUAL_UINT32(30000u, cfg.max_ms);
}

static void test_a_hold_opens_the_microphone_and_sends_on_release(void) {
    nev_ptt_t p = make();
    press_and_arm(&p, 1000);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_SEND, nev_ptt_release(&p, 1000 + ARM_MS + MIN_MS));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_IDLE, nev_ptt_state(&p));
}

static void test_a_brush_never_opens_the_microphone(void) {
    /*
     * The important half of this is the action, not the state: a sleeve that
     * touches the button for 40 ms must produce no START, so there is never a
     * capture to apologise for and nothing is sent anywhere.
     */
    nev_ptt_t p = make();
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(&p, 0));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_tick(&p, 40));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_release(&p, 40));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_IDLE, nev_ptt_state(&p));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DISCARD_NONE, nev_ptt_last_discard(&p));
}

static void test_the_arming_window_is_not_counted_as_speech(void) {
    /*
     * A hold of arm_ms + 200 contains 200 ms of audio, not 320. Counting the
     * arming window would let a capture with almost nothing in it through,
     * which is the exact thing min_speech_ms exists to stop.
     */
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_DISCARD, nev_ptt_release(&p, ARM_MS + 200));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DISCARD_TOO_SHORT, nev_ptt_last_discard(&p));
}

static void test_a_capture_with_no_words_in_it_is_discarded_not_sent(void) {
    nev_ptt_t p = make();
    press_and_arm(&p, 5000);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_DISCARD, nev_ptt_release(&p, 5000 + ARM_MS + MIN_MS - 1));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DISCARD_TOO_SHORT, nev_ptt_last_discard(&p));
    TEST_ASSERT_NOT_EQUAL(0, nev_ptt_discard_reason(nev_ptt_last_discard(&p))[0]);
}

static void test_exactly_the_minimum_is_long_enough(void) {
    /* A boundary written down, so tightening min_speech_ms later is a decision
     * rather than an accident. */
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_SEND, nev_ptt_release(&p, ARM_MS + MIN_MS));
}

static void test_a_stuck_button_stops_itself_and_throws_the_audio_away(void) {
    /*
     * The device under a book. It must stop, and it must not send half a minute
     * of a room to somebody's computer on the way out.
     */
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_tick(&p, ARM_MS + MAX_MS - 1));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_DISCARD, nev_ptt_tick(&p, ARM_MS + MAX_MS));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DISCARD_TOO_LONG, nev_ptt_last_discard(&p));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_IDLE, nev_ptt_state(&p));
}

static void test_a_release_after_the_ceiling_does_nothing_more(void) {
    /* The button is still down when the ceiling fires. The release that
     * eventually arrives must not produce a second STOP for a capture that is
     * already closed — which would close the *next* one. */
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_DISCARD, nev_ptt_tick(&p, ARM_MS + MAX_MS));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_release(&p, ARM_MS + MAX_MS + 5000));
}

static void test_a_chattering_button_does_not_restart_the_capture(void) {
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(&p, ARM_MS + 10));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(&p, ARM_MS + 20));
    /* Still the original capture, so its age is measured from the first arm. */
    TEST_ASSERT_EQUAL_UINT32(500u, nev_ptt_talk_ms(&p, ARM_MS + 500));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_SEND, nev_ptt_release(&p, ARM_MS + 500));
}

static void test_cancelling_a_live_capture_discards_it(void) {
    /* The app closed or the link dropped. Whatever was being said was not
     * finished, and finishing it for the user is the wrong guess. */
    nev_ptt_t p = make();
    press_and_arm(&p, 0);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_DISCARD, nev_ptt_cancel(&p));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_IDLE, nev_ptt_state(&p));
    /* No reason shown: nothing the user did was wrong. */
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DISCARD_NONE, nev_ptt_last_discard(&p));
}

static void test_cancelling_while_arming_has_nothing_to_stop(void) {
    nev_ptt_t p = make();
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(&p, 0));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_cancel(&p));
}

static void test_talk_ms_is_zero_when_nothing_is_open(void) {
    nev_ptt_t p = make();
    TEST_ASSERT_EQUAL_UINT32(0u, nev_ptt_talk_ms(&p, 9999));
    nev_ptt_press(&p, 0);
    TEST_ASSERT_EQUAL_UINT32(0u, nev_ptt_talk_ms(&p, 50)); /* arming is not talking */
}

static void test_a_zero_arm_window_opens_on_the_press(void) {
    /* What a button with hardware debouncing would ask for, and what the other
     * tests would need a tick for. */
    const nev_ptt_cfg_t cfg = {.arm_ms = 0, .min_speech_ms = MIN_MS, .max_ms = MAX_MS};
    nev_ptt_t p;
    nev_ptt_init(&p, &cfg);
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_START, nev_ptt_press(&p, 700));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_SEND, nev_ptt_release(&p, 700 + MIN_MS));
}

static void test_the_clock_wrapping_does_not_open_or_close_anything(void) {
    /* now_ms is a 32-bit millisecond counter and wraps after 49 days. The
     * subtraction is unsigned on purpose; this is the test that says so. */
    const uint32_t near_wrap = 0xFFFFFF00u;
    nev_ptt_t p = make();
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_NOTHING, nev_ptt_press(&p, near_wrap));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_START, nev_ptt_tick(&p, near_wrap + ARM_MS));
    TEST_ASSERT_EQUAL_INT(NEV_PTT_DO_SEND, nev_ptt_release(&p, near_wrap + ARM_MS + MIN_MS));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_defaults_are_the_documented_ones);
    RUN_TEST(test_a_hold_opens_the_microphone_and_sends_on_release);
    RUN_TEST(test_a_brush_never_opens_the_microphone);
    RUN_TEST(test_the_arming_window_is_not_counted_as_speech);
    RUN_TEST(test_a_capture_with_no_words_in_it_is_discarded_not_sent);
    RUN_TEST(test_exactly_the_minimum_is_long_enough);
    RUN_TEST(test_a_stuck_button_stops_itself_and_throws_the_audio_away);
    RUN_TEST(test_a_release_after_the_ceiling_does_nothing_more);
    RUN_TEST(test_a_chattering_button_does_not_restart_the_capture);
    RUN_TEST(test_cancelling_a_live_capture_discards_it);
    RUN_TEST(test_cancelling_while_arming_has_nothing_to_stop);
    RUN_TEST(test_talk_ms_is_zero_when_nothing_is_open);
    RUN_TEST(test_a_zero_arm_window_opens_on_the_press);
    RUN_TEST(test_the_clock_wrapping_does_not_open_or_close_anything);
    return UNITY_END();
}
