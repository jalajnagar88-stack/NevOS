/*
 * GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT
 *
 * Canonical encodings produced by the Python reference implementation in
 * tools/schema/gen.py. The C and Rust codecs are both checked against these, so
 * a disagreement between any two of the three is caught rather than shipped.
 */
#ifndef NEV_BRIDGE_GOLDEN_DATA_H
#define NEV_BRIDGE_GOLDEN_DATA_H

#include "nev_bridge/nev_proto.h"
#include <stdio.h>
#include <string.h>

/*
 * Every message in the schema, so a test can iterate rather than keep its own
 * list. A hand-written list is a list that will one day be missing the message
 * someone just added — which is precisely the drift this file exists to catch.
 */
#define NEV_GOLDEN_FOR_EACH(X) \
    X(hello) \
    X(hello_ack) \
    X(pair) \
    X(pair_result) \
    X(ping) \
    X(pong) \
    X(audio_chunk) \
    X(capture_marker) \
    X(transcript_partial) \
    X(transcript_final) \
    X(agent_request) \
    X(agent_token) \
    X(agent_done) \
    X(mood_hint) \
    X(notification) \
    X(ota_available)

static const uint8_t kGolden_hello[] = {
    0x85, 0x01, 0x07, 0x6B, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x5F, 0x69, 0x64, 0x2D, 0x31, 0x6A, 0x66, 0x69, 0x72, 0x6D, 0x77, 0x61, 0x72, 0x65, 0x2D, 0x32, 0x67, 0x74, 0x6F, 0x6B, 0x65, 0x6E, 0x2D, 0x33
};
static nev_msg_hello_t golden_sample_hello(void) {
    nev_msg_hello_t s;
    memset(&s, 0, sizeof(s));
    s.protocol = 7u;
    snprintf(s.device_id, sizeof(s.device_id), "%s", "device_id-1");
    snprintf(s.firmware, sizeof(s.firmware), "%s", "firmware-2");
    snprintf(s.token, sizeof(s.token), "%s", "token-3");
    return s;
}

static const uint8_t kGolden_hello_ack[] = {
    0x85, 0x02, 0xF5, 0xF4, 0x6D, 0x64, 0x61, 0x65, 0x6D, 0x6F, 0x6E, 0x5F, 0x6E, 0x61, 0x6D, 0x65, 0x2D, 0x32, 0x18, 0x1C
};
static nev_msg_hello_ack_t golden_sample_hello_ack(void) {
    nev_msg_hello_ack_t s;
    memset(&s, 0, sizeof(s));
    s.accepted = true;
    s.needs_pairing = false;
    snprintf(s.daemon_name, sizeof(s.daemon_name), "%s", "daemon_name-2");
    s.unix_time = 28u;
    return s;
}

static const uint8_t kGolden_pair[] = {
    0x82, 0x03, 0x66, 0x63, 0x6F, 0x64, 0x65, 0x2D, 0x30
};
static nev_msg_pair_t golden_sample_pair(void) {
    nev_msg_pair_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.code, sizeof(s.code), "%s", "code-0");
    return s;
}

static const uint8_t kGolden_pair_result[] = {
    0x84, 0x04, 0xF5, 0x67, 0x74, 0x6F, 0x6B, 0x65, 0x6E, 0x2D, 0x31, 0x68, 0x72, 0x65, 0x61, 0x73, 0x6F, 0x6E, 0x2D, 0x32
};
static nev_msg_pair_result_t golden_sample_pair_result(void) {
    nev_msg_pair_result_t s;
    memset(&s, 0, sizeof(s));
    s.granted = true;
    snprintf(s.token, sizeof(s.token), "%s", "token-1");
    snprintf(s.reason, sizeof(s.reason), "%s", "reason-2");
    return s;
}

static const uint8_t kGolden_ping[] = {
    0x82, 0x05, 0x07
};
static nev_msg_ping_t golden_sample_ping(void) {
    nev_msg_ping_t s;
    memset(&s, 0, sizeof(s));
    s.nonce = 7u;
    return s;
}

static const uint8_t kGolden_pong[] = {
    0x82, 0x06, 0x07
};
static nev_msg_pong_t golden_sample_pong(void) {
    nev_msg_pong_t s;
    memset(&s, 0, sizeof(s));
    s.nonce = 7u;
    return s;
}

static const uint8_t kGolden_audio_chunk[] = {
    0x86, 0x10, 0x07, 0x0E, 0xF5, 0x48, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x18, 0x23
};
static const uint8_t kGolden_audio_chunk_pcm[] = {0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A};
static nev_msg_audio_chunk_t golden_sample_audio_chunk(void) {
    nev_msg_audio_chunk_t s;
    memset(&s, 0, sizeof(s));
    s.seq = 7u;
    s.session = 14u;
    s.final = true;
    s.pcm = kGolden_audio_chunk_pcm;
    s.pcm_len = sizeof(kGolden_audio_chunk_pcm);
    s.kind = 35u;
    return s;
}

static const uint8_t kGolden_capture_marker[] = {
    0x83, 0x13, 0x07, 0xFA, 0x3F, 0xC0, 0x00, 0x00
};
static nev_msg_capture_marker_t golden_sample_capture_marker(void) {
    nev_msg_capture_marker_t s;
    memset(&s, 0, sizeof(s));
    s.session = 7u;
    s.at_seconds = 1.5f;
    return s;
}

static const uint8_t kGolden_transcript_partial[] = {
    0x83, 0x11, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31
};
static nev_msg_transcript_partial_t golden_sample_transcript_partial(void) {
    nev_msg_transcript_partial_t s;
    memset(&s, 0, sizeof(s));
    s.session = 7u;
    snprintf(s.text, sizeof(s.text), "%s", "text-1");
    return s;
}

static const uint8_t kGolden_transcript_final[] = {
    0x84, 0x12, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31, 0xFA, 0x40, 0x20, 0x00, 0x00
};
static nev_msg_transcript_final_t golden_sample_transcript_final(void) {
    nev_msg_transcript_final_t s;
    memset(&s, 0, sizeof(s));
    s.session = 7u;
    snprintf(s.text, sizeof(s.text), "%s", "text-1");
    s.confidence = 2.5f;
    return s;
}

static const uint8_t kGolden_agent_request[] = {
    0x84, 0x18, 0x20, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31, 0x65, 0x61, 0x70, 0x70, 0x2D, 0x32
};
static nev_msg_agent_request_t golden_sample_agent_request(void) {
    nev_msg_agent_request_t s;
    memset(&s, 0, sizeof(s));
    s.turn = 7u;
    snprintf(s.text, sizeof(s.text), "%s", "text-1");
    snprintf(s.app, sizeof(s.app), "%s", "app-2");
    return s;
}

static const uint8_t kGolden_agent_token[] = {
    0x83, 0x18, 0x21, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31
};
static nev_msg_agent_token_t golden_sample_agent_token(void) {
    nev_msg_agent_token_t s;
    memset(&s, 0, sizeof(s));
    s.turn = 7u;
    snprintf(s.text, sizeof(s.text), "%s", "text-1");
    return s;
}

static const uint8_t kGolden_agent_done[] = {
    0x83, 0x18, 0x22, 0x07, 0x67, 0x65, 0x72, 0x72, 0x6F, 0x72, 0x2D, 0x31
};
static nev_msg_agent_done_t golden_sample_agent_done(void) {
    nev_msg_agent_done_t s;
    memset(&s, 0, sizeof(s));
    s.turn = 7u;
    snprintf(s.error, sizeof(s.error), "%s", "error-1");
    return s;
}

static const uint8_t kGolden_mood_hint[] = {
    0x84, 0x18, 0x30, 0x07, 0x0E, 0x15
};
static nev_msg_mood_hint_t golden_sample_mood_hint(void) {
    nev_msg_mood_hint_t s;
    memset(&s, 0, sizeof(s));
    s.mood = 7u;
    s.intensity = 14u;
    s.duration_ms = 21u;
    return s;
}

static const uint8_t kGolden_notification[] = {
    0x84, 0x18, 0x31, 0x67, 0x74, 0x69, 0x74, 0x6C, 0x65, 0x2D, 0x30, 0x66, 0x62, 0x6F, 0x64, 0x79, 0x2D, 0x31, 0xF5
};
static nev_msg_notification_t golden_sample_notification(void) {
    nev_msg_notification_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.title, sizeof(s.title), "%s", "title-0");
    snprintf(s.body, sizeof(s.body), "%s", "body-1");
    s.urgent = true;
    return s;
}

static const uint8_t kGolden_ota_available[] = {
    0x85, 0x18, 0x32, 0x69, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x2D, 0x30, 0x65, 0x75, 0x72, 0x6C, 0x2D, 0x31, 0x15, 0x48, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A
};
static const uint8_t kGolden_ota_available_sha256[] = {0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A};
static nev_msg_ota_available_t golden_sample_ota_available(void) {
    nev_msg_ota_available_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.version, sizeof(s.version), "%s", "version-0");
    snprintf(s.url, sizeof(s.url), "%s", "url-1");
    s.size_bytes = 21u;
    s.sha256 = kGolden_ota_available_sha256;
    s.sha256_len = sizeof(kGolden_ota_available_sha256);
    return s;
}

#endif /* NEV_BRIDGE_GOLDEN_DATA_H */
