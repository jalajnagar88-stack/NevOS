/*
 * The sounds, as numbers.
 *
 * The point of synthesising these rather than shipping WAV files is that every
 * property worth having becomes testable: that a cue starts and ends at
 * silence, that volume zero is silent, that nothing clips. An ear cannot check
 * the third one until it is too loud, and by then it is in someone's house.
 */
#include <stdlib.h>

#include "nev_services/sound_core.h"
#include "unity.h"

#define RATE 16000u

void setUp(void) {
}
void tearDown(void) {
}

/* Renders a whole cue. Returns the sample count. */
static size_t render_all(nev_sound_core_t *s, int16_t *buf, size_t cap) {
    size_t total = 0;
    for (;;) {
        const size_t n =
            nev_sound_core_render(s, buf + total, (cap - total) < 64 ? (cap - total) : 64);
        if (n == 0) break;
        total += n;
        if (total >= cap) break;
    }
    return total;
}

static void test_every_cue_is_the_length_it_claims(void) {
    for (int c = 0; c < NEV_CUE_COUNT; c++) {
        nev_sound_core_t s;
        nev_sound_core_init(&s, RATE);
        nev_sound_core_start(&s, (nev_sound_cue_t)c, 100);

        static int16_t buf[32768];
        const size_t n = render_all(&s, buf, sizeof(buf) / sizeof(buf[0]));
        const size_t want = nev_sound_cue_ms((nev_sound_cue_t)c) * RATE / 1000u;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)want, (uint32_t)n,
                                         nev_sound_cue_name((nev_sound_cue_t)c));
        TEST_ASSERT_FALSE(nev_sound_core_active(&s));
    }
}

static void test_no_cue_begins_or_ends_with_a_click(void) {
    /* A tone that starts at full amplitude starts with a step, and a step
     * through a small speaker is louder than the note it belongs to. */
    for (int c = 0; c < NEV_CUE_COUNT; c++) {
        nev_sound_core_t s;
        nev_sound_core_init(&s, RATE);
        nev_sound_core_start(&s, (nev_sound_cue_t)c, 100);

        static int16_t buf[32768];
        const size_t n = render_all(&s, buf, sizeof(buf) / sizeof(buf[0]));
        TEST_ASSERT_TRUE(n > 2);
        TEST_ASSERT_INT_WITHIN_MESSAGE(64, 0, buf[0], "cue starts with a step");
        TEST_ASSERT_INT_WITHIN_MESSAGE(64, 0, buf[n - 1], "cue ends with a step");
    }
}

static void test_nothing_clips(void) {
    /* Two gains multiply here — the cue's and the user's — and an over-range
     * sample wrapping turns a loud note into a burst of noise. */
    for (int c = 0; c < NEV_CUE_COUNT; c++) {
        nev_sound_core_t s;
        nev_sound_core_init(&s, RATE);
        nev_sound_core_start(&s, (nev_sound_cue_t)c, 100);

        static int16_t buf[32768];
        const size_t n = render_all(&s, buf, sizeof(buf) / sizeof(buf[0]));
        int16_t peak = 0;
        for (size_t i = 0; i < n; i++) {
            const int16_t v = buf[i] < 0 ? (int16_t)-buf[i] : buf[i];
            if (v > peak) peak = v;
        }
        TEST_ASSERT_TRUE_MESSAGE(peak < 32767, "a cue reached full scale");
        TEST_ASSERT_TRUE_MESSAGE(peak > 1000, "a cue is inaudibly quiet");
    }
}

static void test_volume_zero_is_actually_silent(void) {
    /* Muted must mean muted, not quiet. */
    nev_sound_core_t s;
    nev_sound_core_init(&s, RATE);
    nev_sound_core_start(&s, NEV_CUE_ALERT, 0);

    int16_t buf[256];
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)nev_sound_core_render(&s, buf, 256));
    TEST_ASSERT_FALSE(nev_sound_core_active(&s));
}

static void test_volume_scales_the_peak(void) {
    static int16_t loud[32768];
    static int16_t soft[32768];

    nev_sound_core_t a, b;
    nev_sound_core_init(&a, RATE);
    nev_sound_core_start(&a, NEV_CUE_SCORE, 100);
    const size_t na = render_all(&a, loud, 32768);

    nev_sound_core_init(&b, RATE);
    nev_sound_core_start(&b, NEV_CUE_SCORE, 25);
    const size_t nb = render_all(&b, soft, 32768);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)na, (uint32_t)nb);

    int32_t peak_a = 0, peak_b = 0;
    for (size_t i = 0; i < na; i++) {
        if (abs(loud[i]) > peak_a) peak_a = abs(loud[i]);
        if (abs(soft[i]) > peak_b) peak_b = abs(soft[i]);
    }
    TEST_ASSERT_TRUE_MESSAGE(peak_b * 3 < peak_a, "quarter volume was not much quieter");
}

static void test_a_buffer_boundary_does_not_break_the_waveform(void) {
    /*
     * The phase carries across calls. Without that, every buffer would restart
     * the sine from zero and the cue would be a series of clicks at whatever
     * rate the audio driver happens to ask for samples — which is exactly the
     * kind of bug that sounds fine on one machine and terrible on another.
     */
    static int16_t whole[32768];
    static int16_t split[32768];

    nev_sound_core_t a;
    nev_sound_core_init(&a, RATE);
    nev_sound_core_start(&a, NEV_CUE_LAUNCH, 100);
    size_t na = 0;
    for (;;) {
        const size_t n = nev_sound_core_render(&a, whole + na, 32768 - na);
        if (n == 0) break;
        na += n;
    }

    nev_sound_core_t b;
    nev_sound_core_init(&b, RATE);
    nev_sound_core_start(&b, NEV_CUE_LAUNCH, 100);
    size_t nb = 0;
    while (nb < 32768) {
        /* Deliberately awkward sizes. */
        const size_t n = nev_sound_core_render(&b, split + nb, (nb % 7) + 1);
        if (n == 0) break;
        nb += n;
    }

    TEST_ASSERT_EQUAL_UINT32((uint32_t)na, (uint32_t)nb);
    for (size_t i = 0; i < na; i++) {
        TEST_ASSERT_INT_WITHIN_MESSAGE(2, whole[i], split[i], "chunking changed the waveform");
    }
}

static void test_a_new_cue_replaces_the_old_one(void) {
    /* Two sounds at once on a speaker this size is mud. The newest wins. */
    nev_sound_core_t s;
    nev_sound_core_init(&s, RATE);
    nev_sound_core_start(&s, NEV_CUE_ALERT, 100);

    int16_t buf[64];
    nev_sound_core_render(&s, buf, 64);
    nev_sound_core_start(&s, NEV_CUE_TAP, 100);

    static int16_t rest[32768];
    const size_t n = render_all(&s, rest, 32768);
    TEST_ASSERT_EQUAL_UINT32(NEV_CUE_TAP * 0 + nev_sound_cue_ms(NEV_CUE_TAP) * RATE / 1000u,
                             (uint32_t)n);
}

static void test_an_unknown_cue_does_nothing(void) {
    nev_sound_core_t s;
    nev_sound_core_init(&s, RATE);
    nev_sound_core_start(&s, (nev_sound_cue_t)99, 100);
    TEST_ASSERT_FALSE(nev_sound_core_active(&s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_cue_is_the_length_it_claims);
    RUN_TEST(test_no_cue_begins_or_ends_with_a_click);
    RUN_TEST(test_nothing_clips);
    RUN_TEST(test_volume_zero_is_actually_silent);
    RUN_TEST(test_volume_scales_the_peak);
    RUN_TEST(test_a_buffer_boundary_does_not_break_the_waveform);
    RUN_TEST(test_a_new_cue_replaces_the_old_one);
    RUN_TEST(test_an_unknown_cue_does_nothing);
    return UNITY_END();
}
