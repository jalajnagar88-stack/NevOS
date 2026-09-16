/*
 * The CBOR codec.
 *
 * This is the one piece of NEVOS that parses bytes arriving from the network,
 * so it is tested adversarially: truncation, lying length headers, constructs
 * the subset deliberately excludes, and a fuzz pass. Under the sanitiser build
 * (./tools/build.sh asan) the fuzz test also proves it never reads out of
 * bounds.
 */
#include "nev_bridge/nev_cbor.h"
#include "unity.h"
#include <string.h>

static uint8_t buf[512];
static nev_cbor_w_t w;
static nev_cbor_r_t r;

void setUp(void) {
    memset(buf, 0, sizeof(buf));
    nev_cbor_w_init(&w, buf, sizeof(buf));
}
void tearDown(void) {
}

static void reread(void) {
    nev_cbor_r_init(&r, buf, w.len);
}

/* --------------------------------------------------- canonical encoding ---
 * Two encoders that disagree on the shortest form produce different bytes for
 * the same message, which would break the golden vectors both languages are
 * checked against. These are the worked examples from RFC 8949 appendix A. */

static void expect_bytes(const uint8_t *want, size_t n) {
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(n, (uint32_t)w.len, "encoded length differs");
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, n);
}

static void test_small_ints_use_the_head_byte(void) {
    nev_cbor_w_u64(&w, 0);
    nev_cbor_w_u64(&w, 10);
    nev_cbor_w_u64(&w, 23);
    const uint8_t want[] = {0x00, 0x0a, 0x17};
    expect_bytes(want, sizeof(want));
}

static void test_integers_use_the_shortest_width(void) {
    nev_cbor_w_u64(&w, 24);
    nev_cbor_w_u64(&w, 1000);
    nev_cbor_w_u64(&w, 1000000);
    const uint8_t want[] = {0x18, 0x18, 0x19, 0x03, 0xe8, 0x1a, 0x00, 0x0f, 0x42, 0x40};
    expect_bytes(want, sizeof(want));
}

static void test_negative_integers_match_the_spec(void) {
    nev_cbor_w_i64(&w, -1);
    nev_cbor_w_i64(&w, -500);
    const uint8_t want[] = {0x20, 0x39, 0x01, 0xf3};
    expect_bytes(want, sizeof(want));
}

static void test_text_and_bool_match_the_spec(void) {
    nev_cbor_w_text(&w, "IETF");
    nev_cbor_w_bool(&w, true);
    nev_cbor_w_bool(&w, false);
    const uint8_t want[] = {0x64, 'I', 'E', 'T', 'F', 0xf5, 0xf4};
    expect_bytes(want, sizeof(want));
}

/* ------------------------------------------------------------ round trips */

static void test_unsigned_round_trip(void) {
    const uint64_t vals[] = {
        0, 1, 23, 24, 255, 256, 65535, 65536, 0xFFFFFFFFu, 0x100000000ull, UINT64_MAX};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        nev_cbor_w_init(&w, buf, sizeof(buf));
        nev_cbor_w_u64(&w, vals[i]);
        reread();
        uint64_t got = 0;
        TEST_ASSERT_TRUE(nev_cbor_r_u64(&r, &got));
        TEST_ASSERT_EQUAL_UINT64(vals[i], got);
        TEST_ASSERT_TRUE(nev_cbor_r_done(&r));
    }
}

static void test_signed_round_trip(void) {
    const int64_t vals[] = {0,    -1,        1,         -24,       -25,      -256,
                            -257, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        nev_cbor_w_init(&w, buf, sizeof(buf));
        nev_cbor_w_i64(&w, vals[i]);
        reread();
        int64_t got = 0;
        TEST_ASSERT_TRUE(nev_cbor_r_i64(&r, &got));
        TEST_ASSERT_EQUAL_INT64(vals[i], got);
    }
}

static void test_float_round_trip(void) {
    const float vals[] = {0.0f, 1.0f, -1.5f, 3.14159f, 1e30f, -1e-30f};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        nev_cbor_w_init(&w, buf, sizeof(buf));
        nev_cbor_w_f32(&w, vals[i]);
        reread();
        float got = 0.0f;
        TEST_ASSERT_TRUE(nev_cbor_r_f32(&r, &got));
        TEST_ASSERT_EQUAL_FLOAT(vals[i], got);
    }
}

static void test_bytes_and_text_round_trip(void) {
    const uint8_t payload[] = {0, 1, 2, 250, 255};
    nev_cbor_w_bytes(&w, payload, sizeof(payload));
    nev_cbor_w_text(&w, "hello NEVOS");
    nev_cbor_w_text(&w, "");
    reread();

    const uint8_t *d = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(nev_cbor_r_bytes(&r, &d, &n));
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, d, n);

    char text[32];
    TEST_ASSERT_TRUE(nev_cbor_r_text(&r, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("hello NEVOS", text);
    TEST_ASSERT_TRUE(nev_cbor_r_text(&r, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("", text);
    TEST_ASSERT_TRUE(nev_cbor_r_done(&r));
}

static void test_array_round_trip(void) {
    nev_cbor_w_array(&w, 3);
    nev_cbor_w_u64(&w, 7);
    nev_cbor_w_text(&w, "x");
    nev_cbor_w_bool(&w, true);
    reread();

    size_t count = 0;
    TEST_ASSERT_TRUE(nev_cbor_r_array(&r, &count));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)count);
}

/* ------------------------------------------------------------- rejections */

static void test_truncated_input_is_refused(void) {
    nev_cbor_w_u64(&w, 1000000); /* a five-byte item */
    for (size_t cut = 1; cut < w.len; cut++) {
        nev_cbor_r_init(&r, buf, cut);
        uint64_t v;
        TEST_ASSERT_FALSE_MESSAGE(nev_cbor_r_u64(&r, &v), "accepted a truncated integer");
        TEST_ASSERT_TRUE(r.error);
    }
}

/* Indefinite-length items are how a decoder is made to loop on attacker input.
 * The subset excludes them, so they must be refused rather than mis-parsed. */
static void test_indefinite_length_is_refused(void) {
    const uint8_t indefinite_text[] = {0x7F, 0x61, 'a', 0xFF};
    nev_cbor_r_init(&r, indefinite_text, sizeof(indefinite_text));
    char out[8];
    TEST_ASSERT_FALSE(nev_cbor_r_text(&r, out, sizeof(out)));

    const uint8_t indefinite_array[] = {0x9F, 0x01, 0xFF};
    nev_cbor_r_init(&r, indefinite_array, sizeof(indefinite_array));
    size_t n;
    TEST_ASSERT_FALSE(nev_cbor_r_array(&r, &n));
}

static void test_reserved_additional_info_is_refused(void) {
    for (uint8_t ai = 28; ai <= 30; ai++) {
        const uint8_t item[] = {(uint8_t)(0x00 | ai)};
        nev_cbor_r_init(&r, item, sizeof(item));
        uint64_t v;
        TEST_ASSERT_FALSE(nev_cbor_r_u64(&r, &v));
    }
}

/* A header claiming four gigabytes is the classic way to walk a parser off the
 * end of its buffer. */
static void test_a_lying_length_header_is_refused(void) {
    const uint8_t huge_text[] = {0x7A, 0xFF, 0xFF, 0xFF, 0xFF, 'a'};
    nev_cbor_r_init(&r, huge_text, sizeof(huge_text));
    char out[8];
    TEST_ASSERT_FALSE(nev_cbor_r_text(&r, out, sizeof(out)));
    TEST_ASSERT_TRUE(r.error);

    const uint8_t huge_array[] = {0x9A, 0xFF, 0xFF, 0xFF, 0xFF};
    nev_cbor_r_init(&r, huge_array, sizeof(huge_array));
    size_t n;
    TEST_ASSERT_FALSE(nev_cbor_r_array(&r, &n));
}

/* Silently shortening a device name or a pairing token is worse than refusing
 * the message. */
static void test_a_string_too_long_for_the_destination_is_refused(void) {
    nev_cbor_w_text(&w, "0123456789");
    reread();
    char small[8];
    TEST_ASSERT_FALSE(nev_cbor_r_text(&r, small, sizeof(small)));
    TEST_ASSERT_TRUE(r.error);
}

static void test_a_value_wider_than_the_field_is_refused(void) {
    nev_cbor_w_u64(&w, 0x1FFFFFFFFull);
    reread();
    uint32_t narrow;
    TEST_ASSERT_FALSE_MESSAGE(nev_cbor_r_u32(&r, &narrow), "truncated a too-wide value");

    nev_cbor_w_init(&w, buf, sizeof(buf));
    nev_cbor_w_i64(&w, (int64_t)INT32_MAX + 1);
    reread();
    int32_t narrow_i;
    TEST_ASSERT_FALSE(nev_cbor_r_i32(&r, &narrow_i));
}

static void test_type_mismatches_are_refused_without_consuming(void) {
    nev_cbor_w_text(&w, "not a number");
    reread();
    uint64_t v;
    TEST_ASSERT_FALSE(nev_cbor_r_u64(&r, &v));
    /* The cursor must not have moved, so a caller can report where it failed. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)r.pos);
}

static void test_nested_arrays_are_refused_by_skip(void) {
    nev_cbor_w_array(&w, 1);
    nev_cbor_w_array(&w, 1);
    nev_cbor_w_u64(&w, 1);
    reread();
    TEST_ASSERT_FALSE_MESSAGE(nev_cbor_r_skip(&r), "skipped into a nested array");
}

static void test_skip_steps_over_every_supported_type(void) {
    const uint8_t payload[] = {9, 9, 9};
    nev_cbor_w_u64(&w, 1000);
    nev_cbor_w_i64(&w, -1000);
    nev_cbor_w_text(&w, "skip me");
    nev_cbor_w_bytes(&w, payload, sizeof(payload));
    nev_cbor_w_bool(&w, true);
    nev_cbor_w_f32(&w, 1.5f);
    nev_cbor_w_u64(&w, 42); /* the one we actually want */
    reread();

    for (int i = 0; i < 6; i++)
        TEST_ASSERT_TRUE_MESSAGE(nev_cbor_r_skip(&r), "skip failed");
    uint64_t v = 0;
    TEST_ASSERT_TRUE(nev_cbor_r_u64(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(42, v);
}

/* ---------------------------------------------------------------- writer */

static void test_writer_overflow_is_sticky_and_safe(void) {
    uint8_t tiny[4];
    nev_cbor_w_t tw;
    nev_cbor_w_init(&tw, tiny, sizeof(tiny));

    nev_cbor_w_text(&tw, "far too long for four bytes");
    TEST_ASSERT_TRUE(tw.overflow);
    TEST_ASSERT_TRUE_MESSAGE(tw.len <= sizeof(tiny), "wrote past the buffer");

    /* Once set it stays set, so a caller can write a whole message and check once. */
    nev_cbor_w_u64(&tw, 1);
    TEST_ASSERT_TRUE(tw.overflow);
}

static void test_a_null_buffer_is_survivable(void) {
    nev_cbor_w_t nw;
    nev_cbor_w_init(&nw, NULL, 0);
    nev_cbor_w_u64(&nw, 5);
    TEST_ASSERT_TRUE(nw.overflow);

    nev_cbor_r_t nr;
    nev_cbor_r_init(&nr, NULL, 0);
    uint64_t v;
    TEST_ASSERT_FALSE(nev_cbor_r_u64(&nr, &v));
}

/* ------------------------------------------------------------------- fuzz */

/*
 * Random bytes must never crash the decoder, and under the sanitiser build must
 * never read out of bounds. Whether any given blob parses is irrelevant; that it
 * fails safely is the whole point.
 */
static void test_fuzz_random_input_never_misbehaves(void) {
    uint32_t seed = 0xC0FFEEu;
    uint8_t noise[96];

    for (int iter = 0; iter < 20000; iter++) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;

        const size_t n = 1 + (seed % sizeof(noise));
        uint32_t s = seed;
        for (size_t i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            noise[i] = (uint8_t)(s >> 24);
        }

        nev_cbor_r_init(&r, noise, n);
        char text[24];
        uint64_t u;
        int64_t i64;
        float f;
        bool b;
        size_t count;
        const uint8_t *d;
        size_t dn;

        /* Every entry point, against the same arbitrary bytes. */
        (void)nev_cbor_r_u64(&r, &u);
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_i64(&r, &i64);
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_f32(&r, &f);
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_bool(&r, &b);
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_text(&r, text, sizeof(text));
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_bytes(&r, &d, &dn);
        nev_cbor_r_init(&r, noise, n);
        (void)nev_cbor_r_array(&r, &count);
        nev_cbor_r_init(&r, noise, n);
        for (int k = 0; k < 8 && nev_cbor_r_skip(&r); k++) {
        }

        TEST_ASSERT_TRUE_MESSAGE(r.pos <= r.len, "the cursor escaped the buffer");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_small_ints_use_the_head_byte);
    RUN_TEST(test_integers_use_the_shortest_width);
    RUN_TEST(test_negative_integers_match_the_spec);
    RUN_TEST(test_text_and_bool_match_the_spec);
    RUN_TEST(test_unsigned_round_trip);
    RUN_TEST(test_signed_round_trip);
    RUN_TEST(test_float_round_trip);
    RUN_TEST(test_bytes_and_text_round_trip);
    RUN_TEST(test_array_round_trip);
    RUN_TEST(test_truncated_input_is_refused);
    RUN_TEST(test_indefinite_length_is_refused);
    RUN_TEST(test_reserved_additional_info_is_refused);
    RUN_TEST(test_a_lying_length_header_is_refused);
    RUN_TEST(test_a_string_too_long_for_the_destination_is_refused);
    RUN_TEST(test_a_value_wider_than_the_field_is_refused);
    RUN_TEST(test_type_mismatches_are_refused_without_consuming);
    RUN_TEST(test_nested_arrays_are_refused_by_skip);
    RUN_TEST(test_skip_steps_over_every_supported_type);
    RUN_TEST(test_writer_overflow_is_sticky_and_safe);
    RUN_TEST(test_a_null_buffer_is_survivable);
    RUN_TEST(test_fuzz_random_input_never_misbehaves);
    return UNITY_END();
}
