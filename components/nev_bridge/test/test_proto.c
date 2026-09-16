/*
 * The generated bridge codec.
 *
 * The important test here is the golden one: every message is decoded from
 * bytes produced by a third, independent implementation (the Python reference
 * in tools/schema/gen.py), and re-encoded to check it comes back byte-identical.
 * That is what makes "the two sides cannot drift" a checked claim rather than
 * an aspiration — the Rust suite runs the same vectors.
 */
#include "nev_bridge/nev_proto.h"
#include "nev_bridge/nev_cbor.h"
#include "golden_data.h"
#include "unity.h"
#include <string.h>

static uint8_t buf[NEV_PROTO_MAX_FRAME];

void setUp(void) {
    memset(buf, 0, sizeof(buf));
}
void tearDown(void) {
}

/* ------------------------------------------------------------- golden ---- */

/*
 * Encoding must be byte-identical to the reference. Canonical CBOR has exactly
 * one encoding per value, so a difference here means one of the three
 * implementations has drifted.
 */
#define GOLDEN_ROUND_TRIP(name)                                                                    \
    do {                                                                                           \
        const nev_msg_##name##_t want = golden_sample_##name();                                    \
        size_t n = 0;                                                                              \
        TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_OK,                                                      \
                                      nev_proto_encode_##name(&want, buf, sizeof(buf), &n),        \
                                      #name " failed to encode");                                  \
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(sizeof(kGolden_##name), (uint32_t)n,                      \
                                         #name " encoded to a different length");                  \
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(kGolden_##name, buf, n,                               \
                                             #name " encoded to different bytes");                 \
                                                                                                   \
        nev_msg_##name##_t got;                                                                    \
        TEST_ASSERT_EQUAL_INT_MESSAGE(                                                             \
            NEV_OK, nev_proto_decode_##name(kGolden_##name, sizeof(kGolden_##name), &got),         \
            #name " failed to decode the reference bytes");                                        \
    } while (0)

static void test_golden_vectors_encode_byte_for_byte(void) {
    GOLDEN_ROUND_TRIP(hello);
    GOLDEN_ROUND_TRIP(hello_ack);
    GOLDEN_ROUND_TRIP(pair);
    GOLDEN_ROUND_TRIP(pair_result);
    GOLDEN_ROUND_TRIP(ping);
    GOLDEN_ROUND_TRIP(pong);
    GOLDEN_ROUND_TRIP(audio_chunk);
    GOLDEN_ROUND_TRIP(transcript_partial);
    GOLDEN_ROUND_TRIP(transcript_final);
    GOLDEN_ROUND_TRIP(agent_request);
    GOLDEN_ROUND_TRIP(agent_token);
    GOLDEN_ROUND_TRIP(agent_done);
    GOLDEN_ROUND_TRIP(mood_hint);
    GOLDEN_ROUND_TRIP(notification);
    GOLDEN_ROUND_TRIP(ota_available);
}

/* Field values, not just lengths: a shifted field would still round-trip. */
static void test_decoded_fields_match_the_reference(void) {
    nev_msg_hello_t hello;
    TEST_ASSERT_EQUAL_INT(NEV_OK,
                          nev_proto_decode_hello(kGolden_hello, sizeof(kGolden_hello), &hello));
    const nev_msg_hello_t want_hello = golden_sample_hello();
    TEST_ASSERT_EQUAL_UINT16(want_hello.protocol, hello.protocol);
    TEST_ASSERT_EQUAL_STRING(want_hello.device_id, hello.device_id);
    TEST_ASSERT_EQUAL_STRING(want_hello.token, hello.token);

    nev_msg_audio_chunk_t chunk;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_decode_audio_chunk(
                                      kGolden_audio_chunk, sizeof(kGolden_audio_chunk), &chunk));
    const nev_msg_audio_chunk_t want_chunk = golden_sample_audio_chunk();
    TEST_ASSERT_EQUAL_UINT32(want_chunk.seq, chunk.seq);
    TEST_ASSERT_EQUAL_UINT32(want_chunk.session, chunk.session);
    TEST_ASSERT_EQUAL_INT(want_chunk.final, chunk.final);
    TEST_ASSERT_EQUAL_UINT32(want_chunk.pcm_len, (uint32_t)chunk.pcm_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_chunk.pcm, chunk.pcm, chunk.pcm_len);

    nev_msg_transcript_final_t tf;
    TEST_ASSERT_EQUAL_INT(NEV_OK,
                          nev_proto_decode_transcript_final(kGolden_transcript_final,
                                                            sizeof(kGolden_transcript_final), &tf));
    TEST_ASSERT_EQUAL_FLOAT(golden_sample_transcript_final().confidence, tf.confidence);
}

/* ----------------------------------------------------------- dispatching -- */

static void test_peek_id_reads_the_id_without_decoding(void) {
    uint16_t id = 0;
    TEST_ASSERT_EQUAL_INT(NEV_OK,
                          nev_proto_peek_id(kGolden_mood_hint, sizeof(kGolden_mood_hint), &id));
    TEST_ASSERT_EQUAL_UINT16(NEV_MSG_MOOD_HINT, id);
    TEST_ASSERT_EQUAL_STRING("mood_hint", nev_proto_name(id));
}

/* Decoding a payload as the wrong type must fail rather than reinterpret it. */
static void test_decoding_as_the_wrong_message_is_refused(void) {
    nev_msg_ping_t ping;
    TEST_ASSERT_EQUAL_INT(NEV_ERR_INVALID_ARG,
                          nev_proto_decode_ping(kGolden_pong, sizeof(kGolden_pong), &ping));
}

/* ------------------------------------------------------- compatibility ---- */

/*
 * A newer peer appends a field. An older decoder must ignore it, not fail —
 * otherwise every schema addition is a flag day for every deployed device.
 */
static void test_trailing_fields_from_a_newer_peer_are_ignored(void) {
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, sizeof(buf));
    nev_cbor_w_array(&w, 3); /* ping has one field; this claims two */
    nev_cbor_w_u64(&w, NEV_MSG_PING);
    nev_cbor_w_u64(&w, 4242);
    nev_cbor_w_text(&w, "a field this build has never heard of");
    TEST_ASSERT_FALSE(w.overflow);

    nev_msg_ping_t ping;
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_OK, nev_proto_decode_ping(buf, w.len, &ping),
                                  "refused a message from a newer peer");
    TEST_ASSERT_EQUAL_UINT32(4242, ping.nonce);
}

/*
 * An older peer omits a field a newer build knows about. It must decode with
 * that field at its zero value rather than failing.
 */
static void test_missing_trailing_fields_default_to_zero(void) {
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, sizeof(buf));
    nev_cbor_w_array(&w, 3); /* hello has four fields; send only the first two */
    nev_cbor_w_u64(&w, NEV_MSG_HELLO);
    nev_cbor_w_u64(&w, 1);
    nev_cbor_w_text(&w, "abc123");
    TEST_ASSERT_FALSE(w.overflow);

    nev_msg_hello_t hello;
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_OK, nev_proto_decode_hello(buf, w.len, &hello),
                                  "refused a message from an older peer");
    TEST_ASSERT_EQUAL_UINT16(1, hello.protocol);
    TEST_ASSERT_EQUAL_STRING("abc123", hello.device_id);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", hello.firmware, "absent field was not zeroed");
    TEST_ASSERT_EQUAL_STRING("", hello.token);
}

/* ------------------------------------------------------------- bounds ----- */

static void test_an_oversized_string_is_refused(void) {
    char big[128];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';

    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, sizeof(buf));
    nev_cbor_w_array(&w, 2);
    nev_cbor_w_u64(&w, NEV_MSG_PAIR);
    nev_cbor_w_text(&w, big); /* pair.code is capped at 12 */

    nev_msg_pair_t pair;
    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_ERR_INVALID_ARG, nev_proto_decode_pair(buf, w.len, &pair),
                                  "a string past its schema bound was accepted");
}

static void test_encoding_into_a_small_buffer_reports_no_space(void) {
    const nev_msg_notification_t msg = golden_sample_notification();
    uint8_t tiny[8];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(NEV_ERR_NO_SPACE,
                          nev_proto_encode_notification(&msg, tiny, sizeof(tiny), &n));
}

static void test_oversized_byte_fields_are_refused_on_encode(void) {
    nev_msg_audio_chunk_t msg = golden_sample_audio_chunk();
    msg.pcm_len = 99999; /* past the schema's 4096 */
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(NEV_ERR_NO_SPACE,
                          nev_proto_encode_audio_chunk(&msg, buf, sizeof(buf), &n));
}

/* ------------------------------------------------------------- framing ---- */

static void test_frames_wrap_and_split(void) {
    size_t framed = 0;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_frame_wrap(kGolden_ping, sizeof(kGolden_ping), buf,
                                                       sizeof(buf), &framed));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kGolden_ping) + NEV_PROTO_FRAME_HEADER, (uint32_t)framed);

    const uint8_t *payload = NULL;
    size_t payload_len = 0, frame_len = 0;
    TEST_ASSERT_EQUAL_INT(NEV_OK,
                          nev_proto_frame_split(buf, framed, &payload, &payload_len, &frame_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kGolden_ping), (uint32_t)payload_len);
    TEST_ASSERT_EQUAL_UINT32(framed, (uint32_t)frame_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kGolden_ping, payload, payload_len);
}

/* A stream delivers frames in arbitrary pieces; a partial one is "wait", not an error. */
static void test_a_partial_frame_asks_for_more(void) {
    size_t framed = 0;
    nev_proto_frame_wrap(kGolden_hello, sizeof(kGolden_hello), buf, sizeof(buf), &framed);

    for (size_t have = 0; have < framed; have++) {
        const uint8_t *p;
        size_t pl, fl;
        TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_ERR_TIMEOUT,
                                      nev_proto_frame_split(buf, have, &p, &pl, &fl),
                                      "a partial frame was treated as complete");
    }
}

/* A length prefix is the first thing a peer controls, and the device cannot
 * allocate its way out of a lie. */
static void test_an_absurd_length_prefix_is_refused(void) {
    const uint8_t liar[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    const uint8_t *p;
    size_t pl, fl;
    TEST_ASSERT_EQUAL_INT(NEV_ERR_NO_SPACE,
                          nev_proto_frame_split(liar, sizeof(liar), &p, &pl, &fl));
}

static void test_two_frames_in_one_buffer_split_correctly(void) {
    size_t a = 0, b = 0;
    nev_proto_frame_wrap(kGolden_ping, sizeof(kGolden_ping), buf, sizeof(buf), &a);
    nev_proto_frame_wrap(kGolden_pong, sizeof(kGolden_pong), buf + a, sizeof(buf) - a, &b);

    const uint8_t *p;
    size_t pl, fl;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_frame_split(buf, a + b, &p, &pl, &fl));
    TEST_ASSERT_EQUAL_UINT32(a, (uint32_t)fl);

    uint16_t id = 0;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_peek_id(p, pl, &id));
    TEST_ASSERT_EQUAL_UINT16(NEV_MSG_PING, id);

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_frame_split(buf + fl, b, &p, &pl, &fl));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_proto_peek_id(p, pl, &id));
    TEST_ASSERT_EQUAL_UINT16(NEV_MSG_PONG, id);
}

/* --------------------------------------------------------------- fuzz ----- */

/* Every decoder, against arbitrary bytes. Under the sanitiser build this also
 * proves none of them reads outside its buffer. */
static void test_fuzz_no_decoder_misbehaves(void) {
    uint32_t seed = 0x5EED1234u;
    uint8_t noise[128];

    for (int iter = 0; iter < 8000; iter++) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const size_t n = 1 + (seed % sizeof(noise));
        uint32_t s = seed;
        for (size_t i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            noise[i] = (uint8_t)(s >> 24);
        }
        /* Sometimes hand it a valid header, so the field decoders are reached
         * rather than every blob being rejected at the array. */
        if ((iter & 3) == 0 && n > 3) {
            noise[0] = 0x84;
            noise[1] = 0x01;
        }

        nev_msg_hello_t h;
        nev_msg_hello_ack_t ha;
        nev_msg_pair_result_t pr;
        nev_msg_audio_chunk_t ac;
        nev_msg_transcript_final_t tf;
        nev_msg_agent_token_t at;
        nev_msg_mood_hint_t mh;
        nev_msg_ota_available_t oa;
        uint16_t id;
        const uint8_t *p;
        size_t pl, fl;

        (void)nev_proto_decode_hello(noise, n, &h);
        (void)nev_proto_decode_hello_ack(noise, n, &ha);
        (void)nev_proto_decode_pair_result(noise, n, &pr);
        (void)nev_proto_decode_audio_chunk(noise, n, &ac);
        (void)nev_proto_decode_transcript_final(noise, n, &tf);
        (void)nev_proto_decode_agent_token(noise, n, &at);
        (void)nev_proto_decode_mood_hint(noise, n, &mh);
        (void)nev_proto_decode_ota_available(noise, n, &oa);
        (void)nev_proto_peek_id(noise, n, &id);
        (void)nev_proto_frame_split(noise, n, &p, &pl, &fl);
    }
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_vectors_encode_byte_for_byte);
    RUN_TEST(test_decoded_fields_match_the_reference);
    RUN_TEST(test_peek_id_reads_the_id_without_decoding);
    RUN_TEST(test_decoding_as_the_wrong_message_is_refused);
    RUN_TEST(test_trailing_fields_from_a_newer_peer_are_ignored);
    RUN_TEST(test_missing_trailing_fields_default_to_zero);
    RUN_TEST(test_an_oversized_string_is_refused);
    RUN_TEST(test_encoding_into_a_small_buffer_reports_no_space);
    RUN_TEST(test_oversized_byte_fields_are_refused_on_encode);
    RUN_TEST(test_frames_wrap_and_split);
    RUN_TEST(test_a_partial_frame_asks_for_more);
    RUN_TEST(test_an_absurd_length_prefix_is_refused);
    RUN_TEST(test_two_frames_in_one_buffer_split_correctly);
    RUN_TEST(test_fuzz_no_decoder_misbehaves);
    return UNITY_END();
}
