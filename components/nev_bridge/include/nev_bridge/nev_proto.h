/*
 * GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT
 *
 * The NEVOS bridge wire protocol. See docs/protocol.md and schema/nevos.toml.
 *
 * Strings decode into fixed arrays sized by the schema. Byte fields decode to a
 * BORROWED pointer into the caller's frame buffer — nothing is copied and nothing
 * is allocated, so the data is valid only while that buffer is.
 */
#ifndef NEV_BRIDGE_NEV_PROTO_H
#define NEV_BRIDGE_NEV_PROTO_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEV_PROTO_VERSION 1u
#define NEV_PROTO_MAX_FRAME 8192u
#define NEV_PROTO_FRAME_HEADER 4u  /* u32 big-endian length prefix */

typedef enum {
    NEV_MSG_HELLO = 1,
    NEV_MSG_HELLO_ACK = 2,
    NEV_MSG_PAIR = 3,
    NEV_MSG_PAIR_RESULT = 4,
    NEV_MSG_PING = 5,
    NEV_MSG_PONG = 6,
    NEV_MSG_AUDIO_CHUNK = 16,
    NEV_MSG_TRANSCRIPT_PARTIAL = 17,
    NEV_MSG_TRANSCRIPT_FINAL = 18,
    NEV_MSG_AGENT_REQUEST = 32,
    NEV_MSG_AGENT_TOKEN = 33,
    NEV_MSG_AGENT_DONE = 34,
    NEV_MSG_MOOD_HINT = 48,
    NEV_MSG_NOTIFICATION = 49,
    NEV_MSG_OTA_AVAILABLE = 50,
} nev_msg_id_t;

const char *nev_proto_name(uint16_t id);

/* First frame after the socket opens. Identifies the device and states what it can do. */
typedef struct {
    /* protocol_version from this schema. A mismatch is refused by the daemon. */
    uint16_t protocol;
    /* Stable per device. Derived from the MAC, not a secret. */
    char device_id[33];
    char firmware[25];
    /* The pairing token from a previous session, or empty when unpaired. */
    char token[65];
} nev_msg_hello_t;

/* Accepts the session, or tells the device it must pair first. */
typedef struct {
    bool accepted;
    bool needs_pairing;
    /* Shown on the device so the user can tell which machine answered. */
    char daemon_name[49];
    /* The device has no RTC; this is where its wall clock comes from. */
    uint64_t unix_time;
} nev_msg_hello_ack_t;

/* Sent while the user is entering the on-screen code in the desktop app. */
typedef struct {
    /* The one-time code shown on the device screen. */
    char code[13];
} nev_msg_pair_t;

/* Grants a long-lived device token, or refuses. */
typedef struct {
    bool granted;
    /* Stored in NVS. Erased by a factory reset. */
    char token[65];
    char reason[65];
} nev_msg_pair_result_t;

/* Liveness. A socket that has stopped delivering does not always report itself closed. */
typedef struct {
    uint32_t nonce;
} nev_msg_ping_t;

typedef struct {
    /* Echoed from the ping, so a stale reply cannot be mistaken for a fresh one. */
    uint32_t nonce;
} nev_msg_pong_t;

/* One frame of captured microphone audio. Roughly 50 a second while recording. */
typedef struct {
    /* Monotonic within a capture. Lets the daemon spot a dropped frame. */
    uint32_t seq;
    /* Groups chunks into one utterance or recording. */
    uint32_t session;
    /* Last chunk of this session; the daemon can finalise the transcript. */
    bool final;
    /* 16 kHz mono signed 16-bit little-endian. */
    const uint8_t *pcm; /* borrowed */
    size_t pcm_len;
} nev_msg_audio_chunk_t;

/* Best guess so far. Replaces any previous partial for this session. */
typedef struct {
    uint32_t session;
    char text[513];
} nev_msg_transcript_partial_t;

typedef struct {
    uint32_t session;
    char text[513];
    float confidence;
} nev_msg_transcript_final_t;

typedef struct {
    /* Increments per exchange. The daemon keeps the conversation context. */
    uint32_t turn;
    char text[513];
    /* Which app asked, so the daemon can vary its tone or tools. */
    char app[17];
} nev_msg_agent_request_t;

/* One streamed fragment of the reply. Rendered as it arrives. */
typedef struct {
    uint32_t turn;
    char text[513];
} nev_msg_agent_token_t;

typedef struct {
    uint32_t turn;
    /* Empty on success. The device shows it rather than failing silently. */
    char error[129];
} nev_msg_agent_done_t;

/* Lets the agent's tone drive the face, so the device's mood and the agent's mood
 * are the same thing to the user. Published as BRIDGE.MOOD_HINT; the persona
 * subscribes. The bridge never calls the persona directly — they are peers. */
typedef struct {
    /* nev_mood_t. An unknown value is ignored rather than defaulting to idle. */
    uint8_t mood;
    uint8_t intensity;
    /* 0 makes it the resting mood; non-zero returns to the previous one. */
    uint16_t duration_ms;
} nev_msg_mood_hint_t;

typedef struct {
    char title[49];
    char body[161];
    bool urgent;
} nev_msg_notification_t;

typedef struct {
    char version[25];
    char url[257];
    uint32_t size_bytes;
    /* The image is verified against this and its signature before it is applied. */
    const uint8_t *sha256; /* borrowed */
    size_t sha256_len;
} nev_msg_ota_available_t;

/* Encodes the payload (no length prefix). NEV_ERR_NO_SPACE if it will not fit. */
nev_err_t nev_proto_encode_hello(const nev_msg_hello_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_hello_ack(const nev_msg_hello_ack_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_pair(const nev_msg_pair_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_pair_result(const nev_msg_pair_result_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_ping(const nev_msg_ping_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_pong(const nev_msg_pong_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_audio_chunk(const nev_msg_audio_chunk_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_transcript_partial(const nev_msg_transcript_partial_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_transcript_final(const nev_msg_transcript_final_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_agent_request(const nev_msg_agent_request_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_agent_token(const nev_msg_agent_token_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_agent_done(const nev_msg_agent_done_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_mood_hint(const nev_msg_mood_hint_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_notification(const nev_msg_notification_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
nev_err_t nev_proto_encode_ota_available(const nev_msg_ota_available_t *msg, uint8_t *buf, size_t cap, size_t *out_len);

/* Decodes a payload. Trailing fields from a newer peer are ignored; missing
 * trailing fields are left at zero. See the compatibility rules in the schema. */
nev_err_t nev_proto_decode_hello(const uint8_t *buf, size_t len, nev_msg_hello_t *out);
nev_err_t nev_proto_decode_hello_ack(const uint8_t *buf, size_t len, nev_msg_hello_ack_t *out);
nev_err_t nev_proto_decode_pair(const uint8_t *buf, size_t len, nev_msg_pair_t *out);
nev_err_t nev_proto_decode_pair_result(const uint8_t *buf, size_t len, nev_msg_pair_result_t *out);
nev_err_t nev_proto_decode_ping(const uint8_t *buf, size_t len, nev_msg_ping_t *out);
nev_err_t nev_proto_decode_pong(const uint8_t *buf, size_t len, nev_msg_pong_t *out);
nev_err_t nev_proto_decode_audio_chunk(const uint8_t *buf, size_t len, nev_msg_audio_chunk_t *out);
nev_err_t nev_proto_decode_transcript_partial(const uint8_t *buf, size_t len, nev_msg_transcript_partial_t *out);
nev_err_t nev_proto_decode_transcript_final(const uint8_t *buf, size_t len, nev_msg_transcript_final_t *out);
nev_err_t nev_proto_decode_agent_request(const uint8_t *buf, size_t len, nev_msg_agent_request_t *out);
nev_err_t nev_proto_decode_agent_token(const uint8_t *buf, size_t len, nev_msg_agent_token_t *out);
nev_err_t nev_proto_decode_agent_done(const uint8_t *buf, size_t len, nev_msg_agent_done_t *out);
nev_err_t nev_proto_decode_mood_hint(const uint8_t *buf, size_t len, nev_msg_mood_hint_t *out);
nev_err_t nev_proto_decode_notification(const uint8_t *buf, size_t len, nev_msg_notification_t *out);
nev_err_t nev_proto_decode_ota_available(const uint8_t *buf, size_t len, nev_msg_ota_available_t *out);

/* Reads the message id without decoding the body, so a receiver can dispatch. */
nev_err_t nev_proto_peek_id(const uint8_t *buf, size_t len, uint16_t *out_id);

/* Length-prefixed framing. */
nev_err_t nev_proto_frame_wrap(const uint8_t *payload, size_t payload_len, uint8_t *buf,
                               size_t cap, size_t *out_len);
/* Reports the payload span of the first complete frame in `buf`, and how many
 * bytes that frame occupied. NEV_ERR_TIMEOUT means more data is needed. */
nev_err_t nev_proto_frame_split(const uint8_t *buf, size_t len, const uint8_t **payload,
                                size_t *payload_len, size_t *frame_len);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BRIDGE_NEV_PROTO_H */
