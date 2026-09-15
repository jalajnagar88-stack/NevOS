/* The mood vocabulary and the tween. No LVGL, no display, no hardware. */
#include "nev_persona/face_params.h"
#include "unity.h"
#include <math.h>
#include <string.h>

void setUp(void) {
}
void tearDown(void) {
}

#define FIELD_COUNT (sizeof(nev_face_params_t) / sizeof(float))

static void test_every_mood_has_a_name_and_round_trips(void) {
    for (int i = 0; i < NEV_MOOD_COUNT; i++) {
        const char *name = nev_mood_name((nev_mood_t)i);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_NOT_EQUAL_MESSAGE('?', name[0], "mood missing from the name table");

        nev_mood_t back;
        TEST_ASSERT_TRUE_MESSAGE(nev_mood_from_name(name, &back), name);
        TEST_ASSERT_EQUAL_INT(i, back);
    }
    nev_mood_t unused;
    TEST_ASSERT_FALSE(nev_mood_from_name("elated", &unused));
    TEST_ASSERT_FALSE(nev_mood_from_name(NULL, &unused));
}

/*
 * The preset table is written positionally, so a reordered struct field would
 * shift every value one column across. The static assert in face_presets.c
 * catches a changed field count; this pins actual values so a same-size
 * reorder is caught too.
 */
static void test_preset_table_columns_are_not_shifted(void) {
    const nev_face_params_t *sleepy = nev_face_preset(NEV_MOOD_SLEEPY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.22f, sleepy->eye_open);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.20f, sleepy->glow);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, sleepy->head_y);

    const nev_face_params_t *concerned = nev_face_preset(NEV_MOOD_CONCERNED);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, concerned->brow_angle);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.60f, concerned->mouth_curve);
}

/* Out-of-range must clamp to a face, never index out of the table. */
static void test_invalid_mood_falls_back_to_idle(void) {
    TEST_ASSERT_EQUAL_PTR(nev_face_preset(NEV_MOOD_IDLE), nev_face_preset((nev_mood_t)-1));
    TEST_ASSERT_EQUAL_PTR(nev_face_preset(NEV_MOOD_IDLE), nev_face_preset((nev_mood_t)999));
}

static void test_presets_stay_in_their_declared_ranges(void) {
    for (int i = 0; i < NEV_MOOD_COUNT; i++) {
        const nev_face_params_t *p = nev_face_preset((nev_mood_t)i);
        const char *n = nev_mood_name((nev_mood_t)i);

        TEST_ASSERT_TRUE_MESSAGE(p->eye_open >= 0.0f && p->eye_open <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(p->mouth_open >= 0.0f && p->mouth_open <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(p->blush >= 0.0f && p->blush <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(p->glow >= 0.0f && p->glow <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(p->eye_slant) <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(p->brow_angle) <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(p->mouth_curve) <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(p->head_tilt) <= 1.0f, n);
        TEST_ASSERT_TRUE_MESSAGE(p->eye_scale >= 0.5f && p->eye_scale <= 1.5f, n);
        TEST_ASSERT_TRUE_MESSAGE(p->mouth_scale >= 0.5f && p->mouth_scale <= 1.5f, n);
    }
}

/* Two moods that render identically would be a bug in the table, not a design. */
static void test_no_two_moods_are_the_same_face(void) {
    for (int i = 0; i < NEV_MOOD_COUNT; i++) {
        for (int j = i + 1; j < NEV_MOOD_COUNT; j++) {
            if (memcmp(nev_face_preset((nev_mood_t)i), nev_face_preset((nev_mood_t)j),
                       sizeof(nev_face_params_t)) == 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "%s and %s are identical", nev_mood_name((nev_mood_t)i),
                         nev_mood_name((nev_mood_t)j));
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_lerp_endpoints_are_exact(void) {
    const nev_face_params_t *a = nev_face_preset(NEV_MOOD_SLEEPY);
    const nev_face_params_t *b = nev_face_preset(NEV_MOOD_CELEBRATING);
    nev_face_params_t out;

    nev_face_lerp(&out, a, b, 0.0f);
    TEST_ASSERT_EQUAL_MEMORY(a, &out, sizeof(out));
    nev_face_lerp(&out, a, b, 1.0f);
    TEST_ASSERT_EQUAL_MEMORY(b, &out, sizeof(out));
}

static void test_lerp_midpoint_is_the_mean_of_every_field(void) {
    const nev_face_params_t *a = nev_face_preset(NEV_MOOD_CONCERNED);
    const nev_face_params_t *b = nev_face_preset(NEV_MOOD_HAPPY);
    nev_face_params_t out;
    nev_face_lerp(&out, a, b, 0.5f);

    const float *pa = (const float *)a;
    const float *pb = (const float *)b;
    const float *po = (const float *)&out;
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, (pa[i] + pb[i]) * 0.5f, po[i]);
    }
}

/* A tween driven by a clock can overshoot; it must not produce a broken face. */
static void test_lerp_clamps_out_of_range_t(void) {
    const nev_face_params_t *a = nev_face_preset(NEV_MOOD_IDLE);
    const nev_face_params_t *b = nev_face_preset(NEV_MOOD_FOCUSED);
    nev_face_params_t out;

    nev_face_lerp(&out, a, b, -3.0f);
    TEST_ASSERT_EQUAL_MEMORY(a, &out, sizeof(out));
    nev_face_lerp(&out, a, b, 42.0f);
    TEST_ASSERT_EQUAL_MEMORY(b, &out, sizeof(out));
}

/* Interpolating in place is how a running transition retargets mid-flight. */
static void test_lerp_is_safe_when_output_aliases_an_input(void) {
    nev_face_params_t cur = *nev_face_preset(NEV_MOOD_IDLE);
    const nev_face_params_t target = *nev_face_preset(NEV_MOOD_CELEBRATING);

    for (int i = 0; i < 64; i++)
        nev_face_lerp(&cur, &cur, &target, 0.25f);

    const float *pc = (const float *)&cur;
    const float *pt = (const float *)&target;
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, pt[i], pc[i], "did not converge on the target");
    }
}

static void test_null_arguments_are_ignored(void) {
    nev_face_params_t out = *nev_face_preset(NEV_MOOD_IDLE);
    nev_face_params_t copy = out;
    nev_face_lerp(&out, NULL, NULL, 0.5f);
    TEST_ASSERT_EQUAL_MEMORY(&copy, &out, sizeof(out));
    nev_face_lerp(NULL, &copy, &copy, 0.5f);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_mood_has_a_name_and_round_trips);
    RUN_TEST(test_preset_table_columns_are_not_shifted);
    RUN_TEST(test_invalid_mood_falls_back_to_idle);
    RUN_TEST(test_presets_stay_in_their_declared_ranges);
    RUN_TEST(test_no_two_moods_are_the_same_face);
    RUN_TEST(test_lerp_endpoints_are_exact);
    RUN_TEST(test_lerp_midpoint_is_the_mean_of_every_field);
    RUN_TEST(test_lerp_clamps_out_of_range_t);
    RUN_TEST(test_lerp_is_safe_when_output_aliases_an_input);
    RUN_TEST(test_null_arguments_are_ignored);
    return UNITY_END();
}
