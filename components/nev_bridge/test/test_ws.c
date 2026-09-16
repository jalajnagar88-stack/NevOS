/*
 * The WebSocket client.
 *
 * The frame codec and the handshake ritual are pure functions, so they are
 * tested here against the RFC's own vectors and against every awkward input a
 * peer can send. The socket half is tested in test_bridge_live.c, which talks
 * to a real daemon.
 */
#include <string.h>

#include "nev_bridge/nev_ws.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/* --------------------------------------------------------------- handshake */

static void test_accept_matches_the_rfc_example(void) {
    /* RFC 6455 §1.3, verbatim. If this passes, a compliant server will accept
     * our handshake, which is not something a home-grown test can establish. */
    char accept[32];
    nev_ws_accept_for_key("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept));
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
}

static void test_accept_differs_per_key(void) {
    char a[32], b[32];
    nev_ws_accept_for_key("AAAAAAAAAAAAAAAAAAAAAA==", a, sizeof(a));
    nev_ws_accept_for_key("AAAAAAAAAAAAAAAAAAAAAB==", b, sizeof(b));
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

/* ------------------------------------------------------------ frame writing */

static void test_a_short_frame_is_masked_and_flagged(void) {
    const uint8_t mask[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
    uint8_t out[32];

    size_t n = nev_ws_frame_write(out, sizeof(out), 0x2, payload, sizeof(payload), mask);
    TEST_ASSERT_EQUAL_UINT(2 + 4 + 3, n);
    TEST_ASSERT_EQUAL_HEX8(0x82, out[0]); /* FIN + binary */
    TEST_ASSERT_EQUAL_HEX8(0x83, out[1]); /* MASK + length 3 */
    /* A client frame that is not masked is rejected outright by a compliant
     * server, so the mask bit and the XOR are both load-bearing. */
    TEST_ASSERT_EQUAL_HEX8(0xAA ^ 0x01, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0xBB ^ 0x02, out[7]);
    TEST_ASSERT_EQUAL_HEX8(0xCC ^ 0x03, out[8]);
}

static void test_a_medium_frame_uses_the_16_bit_length(void) {
    const uint8_t mask[4] = {0, 0, 0, 0};
    uint8_t payload[200];
    memset(payload, 0x5A, sizeof(payload));
    uint8_t out[256];

    size_t n = nev_ws_frame_write(out, sizeof(out), 0x2, payload, sizeof(payload), mask);
    TEST_ASSERT_EQUAL_UINT(2 + 2 + 4 + 200, n);
    TEST_ASSERT_EQUAL_HEX8(0x80 | 126, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(200, out[3]);
}

static void test_a_frame_that_does_not_fit_is_refused(void) {
    /* Better than a truncated frame, which would desynchronise the stream and
     * take the connection down several messages later. */
    const uint8_t mask[4] = {0, 0, 0, 0};
    uint8_t payload[100];
    memset(payload, 0, sizeof(payload));
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT(
        0, nev_ws_frame_write(out, sizeof(out), 0x2, payload, sizeof(payload), mask));
}

/* ------------------------------------------------------------ frame reading */

/* Builds an unmasked server frame, as the daemon sends. */
static size_t server_frame(uint8_t *out, uint8_t opcode, const uint8_t *payload, size_t len) {
    size_t o = 0;
    out[o++] = (uint8_t)(0x80 | opcode);
    if (len <= 125) {
        out[o++] = (uint8_t)len;
    } else {
        out[o++] = 126;
        out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)len;
    }
    memcpy(out + o, payload, len);
    return o + len;
}

static void test_a_server_frame_parses(void) {
    uint8_t buf[64];
    const uint8_t payload[5] = {1, 2, 3, 4, 5};
    size_t total = server_frame(buf, 0x2, payload, sizeof(payload));

    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_TRUE(nev_ws_frame_read(buf, total, &opcode, &at, &len, &consumed, &need_more));
    TEST_ASSERT_FALSE(need_more);
    TEST_ASSERT_EQUAL_HEX8(0x2, opcode);
    TEST_ASSERT_EQUAL_UINT(5, len);
    TEST_ASSERT_EQUAL_UINT(total, consumed);
    TEST_ASSERT_EQUAL_MEMORY(payload, buf + at, len);
}

static void test_a_frame_split_anywhere_asks_for_more(void) {
    /* TCP does not deliver on frame boundaries, and this is the failure that
     * only shows up under load. So: try every split point. */
    uint8_t buf[300];
    uint8_t payload[200];
    memset(payload, 0x11, sizeof(payload));
    size_t total = server_frame(buf, 0x2, payload, sizeof(payload));

    for (size_t split = 0; split < total; split++) {
        uint8_t opcode = 0;
        size_t at = 0, len = 0, consumed = 0;
        bool need_more = false;
        bool ok = nev_ws_frame_read(buf, split, &opcode, &at, &len, &consumed, &need_more);
        TEST_ASSERT_FALSE_MESSAGE(ok, "an incomplete frame must not parse");
        TEST_ASSERT_TRUE_MESSAGE(need_more, "an incomplete frame must ask for more");
    }
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = false;
    TEST_ASSERT_TRUE(nev_ws_frame_read(buf, total, &opcode, &at, &len, &consumed, &need_more));
}

static void test_a_masked_server_frame_is_refused(void) {
    /* RFC 6455 §5.1: a server must not mask. A client that helpfully unmasked
     * one anyway would be accepting frames from something that is not a
     * compliant server. */
    uint8_t buf[16] = {0x82, 0x81, 0, 0, 0, 0, 0};
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_FALSE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
    TEST_ASSERT_FALSE_MESSAGE(need_more, "a masked server frame is malformed, not incomplete");
}

static void test_reserved_bits_are_refused(void) {
    /* RSV bits mean an extension was negotiated. We negotiate none, so a peer
     * setting one is talking a protocol we do not understand. */
    uint8_t buf[8] = {0xC2, 0x00};
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_FALSE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
    TEST_ASSERT_FALSE(need_more);
}

static void test_a_fragmented_frame_is_refused(void) {
    /* No FIN bit. The daemon never fragments; accepting it would mean carrying
     * reassembly state for a case that cannot happen. */
    uint8_t buf[8] = {0x02, 0x00};
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_FALSE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
}

static void test_a_non_canonical_length_is_refused(void) {
    /* 126 as a 16-bit length of 5: legal-looking, but it gives one payload two
     * encodings, and something upstream will eventually disagree about which. */
    uint8_t buf[16] = {0x82, 126, 0x00, 0x05, 1, 2, 3, 4, 5};
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_FALSE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
}

static void test_an_oversized_frame_is_refused_rather_than_buffered(void) {
    /* A peer claiming 4 GB must not put this device into a state where it waits
     * for 4 GB. The cap is the receive buffer, which is the only honest one. */
    uint8_t buf[16] = {0x82, 127, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_FALSE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
    TEST_ASSERT_FALSE_MESSAGE(need_more, "an impossible length is malformed, not incomplete");
}

static void test_a_zero_length_frame_parses(void) {
    uint8_t buf[4] = {0x8A, 0x00}; /* an empty pong */
    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_TRUE(
        nev_ws_frame_read(buf, sizeof(buf), &opcode, &at, &len, &consumed, &need_more));
    TEST_ASSERT_EQUAL_HEX8(0xA, opcode);
    TEST_ASSERT_EQUAL_UINT(0, len);
    TEST_ASSERT_EQUAL_UINT(2, consumed);
}

static void test_round_trip_through_both_halves(void) {
    /* What we write, unmasked by hand, is what we meant to send. */
    const uint8_t mask[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t payload[130];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i * 7);

    uint8_t frame[256];
    size_t n = nev_ws_frame_write(frame, sizeof(frame), 0x2, payload, sizeof(payload), mask);
    TEST_ASSERT_TRUE(n > 0);

    /* Clear the mask bit and strip the key, turning it into a server frame. */
    uint8_t server[256];
    size_t header = 4; /* 2 + 2 for the 16-bit length */
    memcpy(server, frame, header);
    server[1] = (uint8_t)(server[1] & 0x7F);
    for (size_t i = 0; i < sizeof(payload); i++) {
        server[header + i] = (uint8_t)(frame[header + 4 + i] ^ mask[i % 4]);
    }

    uint8_t opcode = 0;
    size_t at = 0, len = 0, consumed = 0;
    bool need_more = true;
    TEST_ASSERT_TRUE(nev_ws_frame_read(server, header + sizeof(payload), &opcode, &at, &len,
                                       &consumed, &need_more));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), len);
    TEST_ASSERT_EQUAL_MEMORY(payload, server + at, len);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accept_matches_the_rfc_example);
    RUN_TEST(test_accept_differs_per_key);
    RUN_TEST(test_a_short_frame_is_masked_and_flagged);
    RUN_TEST(test_a_medium_frame_uses_the_16_bit_length);
    RUN_TEST(test_a_frame_that_does_not_fit_is_refused);
    RUN_TEST(test_a_server_frame_parses);
    RUN_TEST(test_a_frame_split_anywhere_asks_for_more);
    RUN_TEST(test_a_masked_server_frame_is_refused);
    RUN_TEST(test_reserved_bits_are_refused);
    RUN_TEST(test_a_fragmented_frame_is_refused);
    RUN_TEST(test_a_non_canonical_length_is_refused);
    RUN_TEST(test_an_oversized_frame_is_refused_rather_than_buffered);
    RUN_TEST(test_a_zero_length_frame_parses);
    RUN_TEST(test_round_trip_through_both_halves);
    return UNITY_END();
}
