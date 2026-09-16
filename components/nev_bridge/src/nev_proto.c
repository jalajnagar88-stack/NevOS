/* GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT */
#include "nev_bridge/nev_proto.h"
#include "nev_bridge/nev_cbor.h"
#include <string.h>

const char *nev_proto_name(uint16_t id) {
    switch (id) {
        case NEV_MSG_HELLO: return "hello";
        case NEV_MSG_HELLO_ACK: return "hello_ack";
        case NEV_MSG_PAIR: return "pair";
        case NEV_MSG_PAIR_RESULT: return "pair_result";
        case NEV_MSG_PING: return "ping";
        case NEV_MSG_PONG: return "pong";
        case NEV_MSG_AUDIO_CHUNK: return "audio_chunk";
        case NEV_MSG_TRANSCRIPT_PARTIAL: return "transcript_partial";
        case NEV_MSG_TRANSCRIPT_FINAL: return "transcript_final";
        case NEV_MSG_AGENT_REQUEST: return "agent_request";
        case NEV_MSG_AGENT_TOKEN: return "agent_token";
        case NEV_MSG_AGENT_DONE: return "agent_done";
        case NEV_MSG_MOOD_HINT: return "mood_hint";
        case NEV_MSG_NOTIFICATION: return "notification";
        case NEV_MSG_OTA_AVAILABLE: return "ota_available";
        default: return "?";
    }
}

nev_err_t nev_proto_encode_hello(const nev_msg_hello_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 5);
    nev_cbor_w_u64(&w, NEV_MSG_HELLO);
    nev_cbor_w_u64(&w, msg->protocol);
    nev_cbor_w_text(&w, msg->device_id);
    nev_cbor_w_text(&w, msg->firmware);
    nev_cbor_w_text(&w, msg->token);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_hello(const uint8_t *buf, size_t len, nev_msg_hello_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_HELLO) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        if (tmp32 > 0xFFFFu) return NEV_ERR_INVALID_ARG;
        out->protocol = (uint16_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->device_id, sizeof(out->device_id))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_text(&r, out->firmware, sizeof(out->firmware))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 3) {
        if (!nev_cbor_r_text(&r, out->token, sizeof(out->token))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 4; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_hello_ack(const nev_msg_hello_ack_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 5);
    nev_cbor_w_u64(&w, NEV_MSG_HELLO_ACK);
    nev_cbor_w_bool(&w, msg->accepted);
    nev_cbor_w_bool(&w, msg->needs_pairing);
    nev_cbor_w_text(&w, msg->daemon_name);
    nev_cbor_w_u64(&w, msg->unix_time);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_hello_ack(const uint8_t *buf, size_t len, nev_msg_hello_ack_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_HELLO_ACK) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_bool(&r, &out->accepted)) return NEV_ERR_INVALID_ARG;
    }
    if (present > 1) {
        if (!nev_cbor_r_bool(&r, &out->needs_pairing)) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_text(&r, out->daemon_name, sizeof(out->daemon_name))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 3) {
        if (!nev_cbor_r_u64(&r, &out->unix_time)) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 4; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_pair(const nev_msg_pair_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 2);
    nev_cbor_w_u64(&w, NEV_MSG_PAIR);
    nev_cbor_w_text(&w, msg->code);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_pair(const uint8_t *buf, size_t len, nev_msg_pair_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_PAIR) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_text(&r, out->code, sizeof(out->code))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 1; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_pair_result(const nev_msg_pair_result_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 4);
    nev_cbor_w_u64(&w, NEV_MSG_PAIR_RESULT);
    nev_cbor_w_bool(&w, msg->granted);
    nev_cbor_w_text(&w, msg->token);
    nev_cbor_w_text(&w, msg->reason);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_pair_result(const uint8_t *buf, size_t len, nev_msg_pair_result_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_PAIR_RESULT) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_bool(&r, &out->granted)) return NEV_ERR_INVALID_ARG;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->token, sizeof(out->token))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_text(&r, out->reason, sizeof(out->reason))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 3; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_ping(const nev_msg_ping_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 2);
    nev_cbor_w_u64(&w, NEV_MSG_PING);
    nev_cbor_w_u64(&w, msg->nonce);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_ping(const uint8_t *buf, size_t len, nev_msg_ping_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_PING) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->nonce = (uint32_t)tmp32;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 1; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_pong(const nev_msg_pong_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 2);
    nev_cbor_w_u64(&w, NEV_MSG_PONG);
    nev_cbor_w_u64(&w, msg->nonce);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_pong(const uint8_t *buf, size_t len, nev_msg_pong_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_PONG) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->nonce = (uint32_t)tmp32;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 1; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_audio_chunk(const nev_msg_audio_chunk_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 5);
    nev_cbor_w_u64(&w, NEV_MSG_AUDIO_CHUNK);
    nev_cbor_w_u64(&w, msg->seq);
    nev_cbor_w_u64(&w, msg->session);
    nev_cbor_w_bool(&w, msg->final);
    if (msg->pcm_len > 4096u) return NEV_ERR_NO_SPACE;
    nev_cbor_w_bytes(&w, msg->pcm, msg->pcm_len);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_audio_chunk(const uint8_t *buf, size_t len, nev_msg_audio_chunk_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_AUDIO_CHUNK) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->seq = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->session = (uint32_t)tmp32;
    }
    if (present > 2) {
        if (!nev_cbor_r_bool(&r, &out->final)) return NEV_ERR_INVALID_ARG;
    }
    if (present > 3) {
        if (!nev_cbor_r_bytes(&r, &out->pcm, &blen)) return NEV_ERR_INVALID_ARG;
        if (blen > 4096u) return NEV_ERR_NO_SPACE;
        out->pcm_len = blen;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 4; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_transcript_partial(const nev_msg_transcript_partial_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 3);
    nev_cbor_w_u64(&w, NEV_MSG_TRANSCRIPT_PARTIAL);
    nev_cbor_w_u64(&w, msg->session);
    nev_cbor_w_text(&w, msg->text);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_transcript_partial(const uint8_t *buf, size_t len, nev_msg_transcript_partial_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_TRANSCRIPT_PARTIAL) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->session = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->text, sizeof(out->text))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 2; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_transcript_final(const nev_msg_transcript_final_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 4);
    nev_cbor_w_u64(&w, NEV_MSG_TRANSCRIPT_FINAL);
    nev_cbor_w_u64(&w, msg->session);
    nev_cbor_w_text(&w, msg->text);
    nev_cbor_w_f32(&w, msg->confidence);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_transcript_final(const uint8_t *buf, size_t len, nev_msg_transcript_final_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_TRANSCRIPT_FINAL) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->session = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->text, sizeof(out->text))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_f32(&r, &out->confidence)) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 3; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_agent_request(const nev_msg_agent_request_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 4);
    nev_cbor_w_u64(&w, NEV_MSG_AGENT_REQUEST);
    nev_cbor_w_u64(&w, msg->turn);
    nev_cbor_w_text(&w, msg->text);
    nev_cbor_w_text(&w, msg->app);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_agent_request(const uint8_t *buf, size_t len, nev_msg_agent_request_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_AGENT_REQUEST) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->turn = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->text, sizeof(out->text))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_text(&r, out->app, sizeof(out->app))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 3; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_agent_token(const nev_msg_agent_token_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 3);
    nev_cbor_w_u64(&w, NEV_MSG_AGENT_TOKEN);
    nev_cbor_w_u64(&w, msg->turn);
    nev_cbor_w_text(&w, msg->text);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_agent_token(const uint8_t *buf, size_t len, nev_msg_agent_token_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_AGENT_TOKEN) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->turn = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->text, sizeof(out->text))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 2; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_agent_done(const nev_msg_agent_done_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 3);
    nev_cbor_w_u64(&w, NEV_MSG_AGENT_DONE);
    nev_cbor_w_u64(&w, msg->turn);
    nev_cbor_w_text(&w, msg->error);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_agent_done(const uint8_t *buf, size_t len, nev_msg_agent_done_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_AGENT_DONE) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->turn = (uint32_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->error, sizeof(out->error))) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 2; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_mood_hint(const nev_msg_mood_hint_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 4);
    nev_cbor_w_u64(&w, NEV_MSG_MOOD_HINT);
    nev_cbor_w_u64(&w, msg->mood);
    nev_cbor_w_u64(&w, msg->intensity);
    nev_cbor_w_u64(&w, msg->duration_ms);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_mood_hint(const uint8_t *buf, size_t len, nev_msg_mood_hint_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_MOOD_HINT) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        if (tmp32 > 0xFFu) return NEV_ERR_INVALID_ARG;
        out->mood = (uint8_t)tmp32;
    }
    if (present > 1) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        if (tmp32 > 0xFFu) return NEV_ERR_INVALID_ARG;
        out->intensity = (uint8_t)tmp32;
    }
    if (present > 2) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        if (tmp32 > 0xFFFFu) return NEV_ERR_INVALID_ARG;
        out->duration_ms = (uint16_t)tmp32;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 3; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_notification(const nev_msg_notification_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 4);
    nev_cbor_w_u64(&w, NEV_MSG_NOTIFICATION);
    nev_cbor_w_text(&w, msg->title);
    nev_cbor_w_text(&w, msg->body);
    nev_cbor_w_bool(&w, msg->urgent);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_notification(const uint8_t *buf, size_t len, nev_msg_notification_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_NOTIFICATION) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_text(&r, out->title, sizeof(out->title))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->body, sizeof(out->body))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_bool(&r, &out->urgent)) return NEV_ERR_INVALID_ARG;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 3; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_encode_ota_available(const nev_msg_ota_available_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!msg || !buf) return NEV_ERR_INVALID_ARG;
    nev_cbor_w_t w;
    nev_cbor_w_init(&w, buf, cap);
    nev_cbor_w_array(&w, 5);
    nev_cbor_w_u64(&w, NEV_MSG_OTA_AVAILABLE);
    nev_cbor_w_text(&w, msg->version);
    nev_cbor_w_text(&w, msg->url);
    nev_cbor_w_u64(&w, msg->size_bytes);
    if (msg->sha256_len > 32u) return NEV_ERR_NO_SPACE;
    nev_cbor_w_bytes(&w, msg->sha256, msg->sha256_len);
    if (w.overflow) return NEV_ERR_NO_SPACE;
    if (out_len) *out_len = w.len;
    return NEV_OK;
}

nev_err_t nev_proto_decode_ota_available(const uint8_t *buf, size_t len, nev_msg_ota_available_t *out) {
    if (!buf || !out) return NEV_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;
    if (id != NEV_MSG_OTA_AVAILABLE) return NEV_ERR_INVALID_ARG;
    const size_t present = count - 1;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    size_t blen = 0;
    (void)tmp32; (void)tmp64; (void)blen;
    if (present > 0) {
        if (!nev_cbor_r_text(&r, out->version, sizeof(out->version))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 1) {
        if (!nev_cbor_r_text(&r, out->url, sizeof(out->url))) return NEV_ERR_INVALID_ARG;
    }
    if (present > 2) {
        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;
        out->size_bytes = (uint32_t)tmp32;
    }
    if (present > 3) {
        if (!nev_cbor_r_bytes(&r, &out->sha256, &blen)) return NEV_ERR_INVALID_ARG;
        if (blen > 32u) return NEV_ERR_NO_SPACE;
        out->sha256_len = blen;
    }
    /* Fields appended by a newer peer: stepped over, not an error. */
    for (size_t i = 4; i < present; i++) {
        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;
    }
    return NEV_OK;
}

nev_err_t nev_proto_peek_id(const uint8_t *buf, size_t len, uint16_t *out_id) {
    if (!buf || !out_id) return NEV_ERR_INVALID_ARG;
    nev_cbor_r_t r;
    nev_cbor_r_init(&r, buf, len);
    size_t count = 0;
    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;
    uint32_t id = 0;
    if (!nev_cbor_r_u32(&r, &id) || id > 0xFFFFu) return NEV_ERR_INVALID_ARG;
    *out_id = (uint16_t)id;
    return NEV_OK;
}

nev_err_t nev_proto_frame_wrap(const uint8_t *payload, size_t payload_len, uint8_t *buf,
                               size_t cap, size_t *out_len) {
    if (!payload || !buf) return NEV_ERR_INVALID_ARG;
    if (payload_len > NEV_PROTO_MAX_FRAME) return NEV_ERR_NO_SPACE;
    if (cap < payload_len + NEV_PROTO_FRAME_HEADER) return NEV_ERR_NO_SPACE;
    buf[0] = (uint8_t)(payload_len >> 24);
    buf[1] = (uint8_t)(payload_len >> 16);
    buf[2] = (uint8_t)(payload_len >> 8);
    buf[3] = (uint8_t)payload_len;
    memcpy(buf + NEV_PROTO_FRAME_HEADER, payload, payload_len);
    if (out_len) *out_len = payload_len + NEV_PROTO_FRAME_HEADER;
    return NEV_OK;
}

nev_err_t nev_proto_frame_split(const uint8_t *buf, size_t len, const uint8_t **payload,
                                size_t *payload_len, size_t *frame_len) {
    if (!buf) return NEV_ERR_INVALID_ARG;
    if (len < NEV_PROTO_FRAME_HEADER) return NEV_ERR_TIMEOUT; /* need more */
    const uint32_t declared = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                              ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    /* Checked before it is trusted: a length prefix is the first thing an
     * attacker controls, and the device cannot allocate its way out of a lie. */
    if (declared > NEV_PROTO_MAX_FRAME) return NEV_ERR_NO_SPACE;
    if (len - NEV_PROTO_FRAME_HEADER < declared) return NEV_ERR_TIMEOUT;
    if (payload) *payload = buf + NEV_PROTO_FRAME_HEADER;
    if (payload_len) *payload_len = declared;
    if (frame_len) *frame_len = declared + NEV_PROTO_FRAME_HEADER;
    return NEV_OK;
}
