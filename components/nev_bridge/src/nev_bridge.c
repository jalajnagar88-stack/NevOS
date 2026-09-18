/* The link to the companion daemon. See nev_bridge.h. */
#include "nev_bridge/nev_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nev_bridge/nev_mdns.h"
#include "nev_bridge/nev_proto.h"
#include "nev_bridge/nev_ws.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_kernel/nev_version.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_net.h"
#include "nev_port/nev_rand.h"
#include "nev_port/nev_time.h"

#define TAG              "bridge"

/* Reconnect backoff. The first retry is quick because the most common cause of
 * a drop is the daemon restarting, and the cap is 30 s because the most common
 * cause of a long outage is the laptop being shut, which is not worth waking
 * the radio for every second. */
#define BACKOFF_MIN_MS   1000u
#define BACKOFF_MAX_MS   30000u

/* Liveness. A Wi-Fi link that has stopped delivering does not always report
 * itself closed, and a device that thinks it is connected stops trying. */
#define PING_INTERVAL_MS 20000u
#define SILENCE_LIMIT_MS 60000u

/* Encoding scratch: the largest thing the device sends is an audio chunk. */
#define ENCODE_CAP       4300

typedef struct {
    nev_bridge_state_t state;
    bool started;

    nev_mdns_t mdns;
    nev_ws_t ws;

    char daemon_ip[16];
    uint16_t daemon_port;
    char daemon_name[49];

    char device_id[33];
    char token[65];
    char code[7]; /* six digits and a NUL */

    uint32_t backoff_ms;
    uint32_t retry_at_ms;
    uint32_t last_rx_ms;
    uint32_t last_ping_ms;

    uint8_t scratch[ENCODE_CAP];

    /* Audio arrives as bus events from audio_service, which has never heard of
     * a daemon. See nev_bridge.h. */
    nev_sub_t *sub;
} bridge_t;

static bridge_t s_bridge;

/* The kind of the capture currently being forwarded, taken from the audio
 * event. It travels on the wire per chunk, but the device only ever has one
 * capture running at a time. */
static uint8_t s_audio_kind;

/* ----------------------------------------------------------------- helpers */

static void publish_simple(uint16_t type) {
    nev_event_t ev = nev_event_make(type, NEV_SRC_BRIDGE);
    (void)nev_bus_publish(&ev);
}

/*
 * Publishes an event carrying text.
 *
 * Text goes in a blob because an event is 32 bytes and a sentence is not. When
 * the pool is empty the event is dropped rather than truncated: half a
 * transcript is worse than a missing one, because the user cannot tell it is
 * half.
 */
static void publish_text(uint16_t type, const char *text, uint32_t extra) {
    size_t len = strlen(text) + 1;
    uint8_t *data = NULL;
    nev_blob_t handle = nev_blob_alloc(len, &data);
    if (handle == NEV_BLOB_NONE) {
        NEV_LOGW(TAG, "no blob for %s; dropping it", nev_evt_name(type));
        return;
    }
    memcpy(data, text, len);

    nev_event_t ev = nev_event_make(type, NEV_SRC_BRIDGE);
    ev.flags |= NEV_EVF_BLOB;
    ev.p.blob.handle = handle;
    ev.p.blob.len = (uint32_t)len;
    ev.p.blob.chunk_seq = (uint16_t)extra;
    (void)nev_bus_publish(&ev);

    /*
     * Step 3 of the ownership protocol in nev_blob.h: the publisher releases
     * its own reference right after publishing, whether or not anyone took it.
     * The bus has already retained once per accepted delivery, so this hands
     * the buffer over rather than freeing it.
     *
     * Missing this leaks one reference per published blob, which is invisible
     * until the pool runs dry — and then it shows up as audio being dropped,
     * two subsystems away from the mistake.
     */
    nev_blob_release(handle);
}

/* Wraps an encoded payload in its length prefix and sends it. */
static bool send_payload(const uint8_t *payload, size_t len) {
    uint8_t framed[ENCODE_CAP + 8];
    size_t framed_len = 0;
    if (nev_proto_frame_wrap(payload, len, framed, sizeof(framed), &framed_len) != NEV_OK) {
        return false;
    }
    return nev_ws_send(&s_bridge.ws, framed, framed_len);
}

static void set_state(nev_bridge_state_t state) {
    if (s_bridge.state == state) return;
    s_bridge.state = state;
}

/* Drops the connection and schedules a retry. */
static void disconnect(const char *why) {
    if (s_bridge.state == NEV_BRIDGE_READY) {
        NEV_LOGI(TAG, "disconnected: %s", why);
        publish_simple(NEV_EVT_BRIDGE_DISCONNECTED);
    } else {
        NEV_LOGD(TAG, "connection attempt ended: %s", why);
    }

    nev_ws_close(&s_bridge.ws);
    nev_mdns_stop(&s_bridge.mdns);
    s_bridge.code[0] = '\0';
    s_bridge.daemon_name[0] = '\0';

    s_bridge.retry_at_ms = nev_now_ms() + s_bridge.backoff_ms;
    s_bridge.backoff_ms =
        (s_bridge.backoff_ms * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : s_bridge.backoff_ms * 2;
    set_state(NEV_BRIDGE_OFFLINE);
}

/* ------------------------------------------------------------- the protocol */

static void send_hello(void) {
    nev_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.protocol = NEV_PROTO_VERSION;
    snprintf(hello.device_id, sizeof(hello.device_id), "%s", s_bridge.device_id);
    snprintf(hello.firmware, sizeof(hello.firmware), "%s", NEVOS_VERSION);
    snprintf(hello.token, sizeof(hello.token), "%s", s_bridge.token);

    size_t len = 0;
    if (nev_proto_encode_hello(&hello, s_bridge.scratch, sizeof(s_bridge.scratch), &len) !=
            NEV_OK ||
        !send_payload(s_bridge.scratch, len)) {
        disconnect("could not send hello");
    }
}

static void send_pair(void) {
    /*
     * The code is generated here, on the device, and shown on its screen. The
     * daemon never sees it until the device sends it, and it grants nothing
     * until a human types the same digits into the desktop app. A code the
     * daemon invented would have to cross the network before anyone could read
     * it, which is exactly what this step exists to avoid.
     */
    snprintf(s_bridge.code, sizeof(s_bridge.code), "%06u", (unsigned)nev_rand_below(1000000u));

    nev_msg_pair_t pair;
    memset(&pair, 0, sizeof(pair));
    snprintf(pair.code, sizeof(pair.code), "%s", s_bridge.code);

    size_t len = 0;
    if (nev_proto_encode_pair(&pair, s_bridge.scratch, sizeof(s_bridge.scratch), &len) != NEV_OK ||
        !send_payload(s_bridge.scratch, len)) {
        disconnect("could not send the pairing code");
        return;
    }
    NEV_LOGI(TAG, "pairing: enter %s on %s", s_bridge.code, s_bridge.daemon_name);
    set_state(NEV_BRIDGE_PAIRING);
}

static void on_hello_ack(const uint8_t *payload, size_t len) {
    nev_msg_hello_ack_t ack;
    if (nev_proto_decode_hello_ack(payload, len, &ack) != NEV_OK) {
        disconnect("malformed hello_ack");
        return;
    }
    snprintf(s_bridge.daemon_name, sizeof(s_bridge.daemon_name), "%s", ack.daemon_name);

    /*
     * The device has no RTC. This is where its wall clock comes from, and it is
     * set before anything else so that a note taken in the first second of a
     * session is not stamped with the epoch.
     */
    if (ack.unix_time > 0) {
        nev_wallclock_set(ack.unix_time);
    }

    if (ack.accepted) {
        NEV_LOGI(TAG, "connected to %s", s_bridge.daemon_name);
        s_bridge.backoff_ms = BACKOFF_MIN_MS;
        set_state(NEV_BRIDGE_READY);
        publish_simple(NEV_EVT_BRIDGE_CONNECTED);
        return;
    }
    if (ack.needs_pairing) {
        /* The token we had is no longer good — the daemon was reinstalled, or
         * the user unpaired us. Forget it, or every reconnection retries a
         * token the daemon has already rejected. */
        if (s_bridge.token[0] != '\0') {
            NEV_LOGI(TAG, "the daemon no longer knows this device; pairing again");
            s_bridge.token[0] = '\0';
            (void)nev_store_set_str(NEV_SET_PAIR_TOKEN, "");
        }
        send_pair();
        return;
    }
    /* Refused for a reason that is not pairing: a protocol version mismatch.
     * Backing off to the maximum immediately is deliberate — retrying every
     * second will not make the firmware any newer. */
    NEV_LOGW(TAG, "the daemon refused this firmware's protocol version");
    s_bridge.backoff_ms = BACKOFF_MAX_MS;
    disconnect("protocol refused");
}

static void on_pair_result(const uint8_t *payload, size_t len) {
    nev_msg_pair_result_t result;
    if (nev_proto_decode_pair_result(payload, len, &result) != NEV_OK) {
        disconnect("malformed pair_result");
        return;
    }
    if (!result.granted) {
        NEV_LOGI(TAG, "pairing refused: %s", result.reason);
        disconnect("pairing refused");
        return;
    }

    snprintf(s_bridge.token, sizeof(s_bridge.token), "%s", result.token);
    if (nev_store_set_str(NEV_SET_PAIR_TOKEN, s_bridge.token) != NEV_OK) {
        /* The session works, but the pairing will not survive a reboot, and a
         * device that quietly asks to pair again every morning is a mystery
         * rather than a bug report. */
        NEV_LOGE(TAG, "could not store the pairing token; this pairing will not persist");
    }
    /* Committed now rather than on the next tick: the point of pairing is that
     * it survives, and the most likely moment for a user to unplug the device
     * is right after the screen says it worked. */
    (void)nev_store_commit();

    s_bridge.code[0] = '\0';
    s_bridge.backoff_ms = BACKOFF_MIN_MS;
    NEV_LOGI(TAG, "paired with %s", s_bridge.daemon_name);
    set_state(NEV_BRIDGE_READY);
    publish_simple(NEV_EVT_BRIDGE_PAIRED);
    publish_simple(NEV_EVT_BRIDGE_CONNECTED);
}

static void on_message(const uint8_t *frame, size_t frame_len) {
    const uint8_t *payload = NULL;
    size_t payload_len = 0, used = 0;

    /* One WebSocket message carries one length-prefixed protocol frame. The
     * prefix is redundant over WebSocket and load-bearing over anything else,
     * so it stays: the codec is shared with the daemon, which does not care
     * which transport it is on. */
    if (nev_proto_frame_split(frame, frame_len, &payload, &payload_len, &used) != NEV_OK) {
        disconnect("malformed frame");
        return;
    }

    uint16_t id = 0;
    if (nev_proto_peek_id(payload, payload_len, &id) != NEV_OK) {
        disconnect("frame is not a protocol message");
        return;
    }
    s_bridge.last_rx_ms = nev_now_ms();

    switch (id) {
        case NEV_MSG_HELLO_ACK:
            on_hello_ack(payload, payload_len);
            break;

        case NEV_MSG_PAIR_RESULT:
            on_pair_result(payload, payload_len);
            break;

        case NEV_MSG_PING: {
            nev_msg_ping_t ping;
            if (nev_proto_decode_ping(payload, payload_len, &ping) != NEV_OK) break;
            nev_msg_pong_t pong = {.nonce = ping.nonce};
            size_t len = 0;
            if (nev_proto_encode_pong(&pong, s_bridge.scratch, sizeof(s_bridge.scratch), &len) ==
                NEV_OK) {
                (void)send_payload(s_bridge.scratch, len);
            }
            break;
        }

        case NEV_MSG_PONG:
            break; /* the timestamp above is the whole point of it */

        case NEV_MSG_TRANSCRIPT_PARTIAL: {
            nev_msg_transcript_partial_t msg;
            if (nev_proto_decode_transcript_partial(payload, payload_len, &msg) != NEV_OK) break;
            publish_text(NEV_EVT_BRIDGE_TRANSCRIPT_PARTIAL, msg.text, msg.session);
            break;
        }

        case NEV_MSG_TRANSCRIPT_FINAL: {
            nev_msg_transcript_final_t msg;
            if (nev_proto_decode_transcript_final(payload, payload_len, &msg) != NEV_OK) break;
            publish_text(NEV_EVT_BRIDGE_TRANSCRIPT_FINAL, msg.text, msg.session);
            break;
        }

        case NEV_MSG_AGENT_TOKEN: {
            nev_msg_agent_token_t msg;
            if (nev_proto_decode_agent_token(payload, payload_len, &msg) != NEV_OK) break;
            publish_text(NEV_EVT_BRIDGE_AGENT_TOKEN, msg.text, msg.turn);
            break;
        }

        case NEV_MSG_AGENT_DONE: {
            nev_msg_agent_done_t msg;
            if (nev_proto_decode_agent_done(payload, payload_len, &msg) != NEV_OK) break;
            /* The error text rides along so the app can show what went wrong
             * rather than a reply that simply stops. */
            publish_text(NEV_EVT_BRIDGE_AGENT_DONE, msg.error, msg.turn);
            break;
        }

        case NEV_MSG_MOOD_HINT: {
            nev_msg_mood_hint_t msg;
            if (nev_proto_decode_mood_hint(payload, payload_len, &msg) != NEV_OK) break;
            /* Published, not applied: the bridge does not call the persona.
             * They are peers, and the persona decides what to do with a hint. */
            nev_event_t ev = nev_event_make(NEV_EVT_BRIDGE_MOOD_HINT, NEV_SRC_BRIDGE);
            ev.p.mood.mood = msg.mood;
            ev.p.mood.intensity = msg.intensity;
            ev.p.mood.duration_ms = msg.duration_ms;
            (void)nev_bus_publish(&ev);
            break;
        }

        case NEV_MSG_NOTIFICATION: {
            nev_msg_notification_t msg;
            if (nev_proto_decode_notification(payload, payload_len, &msg) != NEV_OK) break;
            publish_text(NEV_EVT_BRIDGE_NOTIFICATION, msg.title, msg.urgent ? 1u : 0u);
            break;
        }

        case NEV_MSG_OTA_AVAILABLE: {
            nev_msg_ota_available_t msg;
            if (nev_proto_decode_ota_available(payload, payload_len, &msg) != NEV_OK) break;
            publish_text(NEV_EVT_OTA_AVAILABLE, msg.version, 0);
            break;
        }

        default:
            /* A newer daemon may send messages this firmware has never heard
             * of. Ignoring them is what the schema's compatibility rules
             * promise, and dropping the connection instead would make every
             * daemon upgrade a device outage. */
            NEV_LOGD(TAG, "ignoring message id %u", (unsigned)id);
            break;
    }
}

/* ---------------------------------------------------------------- lifecycle */

/*
 * A fixed daemon address, for development and for CI.
 *
 * Host builds only, and deliberately so: on the device there is nowhere for an
 * environment variable to come from and no reason to want one — the robot has
 * to find the daemon by itself or the setup story falls apart.
 *
 * It exists because multicast is the one thing in this system that depends on
 * the network being friendly. A CI runner that silently drops mDNS would fail
 * the end-to-end test for a reason that has nothing to do with NEVOS, and a
 * flaky gate gets ignored, which is worse than not having one. Discovery itself
 * is covered by nev_mdns's own tests and by running the simulator without this
 * set, which is what a developer does by default.
 *
 *   NEVOS_DAEMON=127.0.0.1:4821 ./build/host/nevos_sim --bridge
 */
static bool fixed_daemon(char *ip, size_t ip_len, uint16_t *port) {
#ifdef NEV_TARGET_HOST
    const char *spec = getenv("NEVOS_DAEMON");
    if (!spec || !*spec) return false;

    const char *colon = strchr(spec, ':');
    if (!colon) return false;
    const size_t host_len = (size_t)(colon - spec);
    if (host_len == 0 || host_len >= ip_len) return false;

    memcpy(ip, spec, host_len);
    ip[host_len] = '\0';
    const unsigned parsed = (unsigned)atoi(colon + 1);
    if (parsed == 0 || parsed > 65535u) return false;
    *port = (uint16_t)parsed;
    return true;
#else
    (void)ip;
    (void)ip_len;
    (void)port;
    return false;
#endif
}

void nev_bridge_init(void) {
    memset(&s_bridge, 0, sizeof(s_bridge));
    s_bridge.ws.sock = NEV_SOCKET_INVALID;
    s_bridge.mdns.sock = NEV_SOCKET_INVALID;
    s_bridge.backoff_ms = BACKOFF_MIN_MS;
    s_bridge.started = true;

    const nev_sub_cfg_t sub = {
        .name = "bridge",
        .domains = NEV_DOM(AUDIO),
        .depth = 12,
        /* Audio is a stream with a sequence number: losing the oldest chunk
         * leaves a gap the daemon can see, where losing the newest would stall
         * the capture behind a backlog it can never clear. */
        .full_policy = NEV_FULL_DROP_OLDEST,
    };
    s_bridge.sub = nev_bus_subscribe(&sub);
    if (!s_bridge.sub) NEV_LOGE(TAG, "no subscriber slot; audio will not be sent");

    snprintf(s_bridge.token, sizeof(s_bridge.token), "%s", nev_store_str(NEV_SET_PAIR_TOKEN));

    const char *stored_id = nev_store_str(NEV_SET_DEVICE_ID);
    if (stored_id && stored_id[0] != '\0') {
        snprintf(s_bridge.device_id, sizeof(s_bridge.device_id), "%s", stored_id);
    } else {
        uint8_t raw[6];
        nev_rand_fill(raw, sizeof(raw));
        snprintf(s_bridge.device_id, sizeof(s_bridge.device_id), "nev-%02x%02x%02x%02x%02x%02x",
                 raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
        (void)nev_store_set_str(NEV_SET_DEVICE_ID, s_bridge.device_id);
        (void)nev_store_commit();
        NEV_LOGI(TAG, "this device is %s", s_bridge.device_id);
    }
}

void nev_bridge_stop(void) {
    nev_ws_close(&s_bridge.ws);
    nev_mdns_stop(&s_bridge.mdns);
    s_bridge.started = false;
    s_bridge.state = NEV_BRIDGE_OFFLINE;
}

/* Forwards captured audio. Chunks arrive as bus events because audio_service is
 * L2 and this is L3: it publishes, and the bridge is simply a subscriber. */
static void drain_audio(void) {
    if (!s_bridge.sub) return;

    nev_event_t ev;
    while (nev_bus_recv(s_bridge.sub, &ev, NEV_NO_WAIT)) {
        if (ev.type == NEV_EVT_AUDIO_CHUNK && (ev.flags & NEV_EVF_BLOB)) {
            /* Dropped rather than queued when the link is down: audio that
             * cannot be sent now is worth less every second it waits, and the
             * sequence number tells the daemon what is missing. */
            if (s_bridge.state == NEV_BRIDGE_READY) {
                s_audio_kind = ev.p.audio.kind;
                const int16_t *pcm = (const int16_t *)nev_blob_data(ev.p.audio.handle);
                (void)nev_bridge_send_audio(ev.p.audio.session, ev.p.audio.seq,
                                            ev.p.audio.final != 0, pcm, ev.p.audio.samples);
            }
        }
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
}

void nev_bridge_poll(void) {
    if (!s_bridge.started) return;

    drain_audio();

    if (!nev_net_is_up()) {
        if (s_bridge.state != NEV_BRIDGE_OFFLINE) disconnect("the network went away");
        return;
    }

    switch (s_bridge.state) {
        case NEV_BRIDGE_OFFLINE: {
            if (nev_elapsed_ms(s_bridge.retry_at_ms) > (uint32_t)INT32_MAX) return; /* not yet */

            if (fixed_daemon(s_bridge.daemon_ip, sizeof(s_bridge.daemon_ip),
                             &s_bridge.daemon_port)) {
                snprintf(s_bridge.daemon_name, sizeof(s_bridge.daemon_name), "%s",
                         s_bridge.daemon_ip);
                NEV_LOGI(TAG, "using the configured daemon at %s:%u", s_bridge.daemon_ip,
                         (unsigned)s_bridge.daemon_port);
                if (!nev_ws_open(&s_bridge.ws, s_bridge.daemon_ip, s_bridge.daemon_port, "/ws")) {
                    disconnect("could not open a socket");
                    return;
                }
                set_state(NEV_BRIDGE_CONNECTING);
                break;
            }

            if (!nev_mdns_start(&s_bridge.mdns)) {
                s_bridge.retry_at_ms = nev_now_ms() + BACKOFF_MAX_MS;
                return;
            }
            set_state(NEV_BRIDGE_SEARCHING);
            break;
        }

        case NEV_BRIDGE_SEARCHING: {
            nev_mdns_result_t found;
            if (!nev_mdns_poll(&s_bridge.mdns, &found)) return;

            nev_net_ipv4_str(found.ipv4, s_bridge.daemon_ip, sizeof(s_bridge.daemon_ip));
            s_bridge.daemon_port = found.port;
            snprintf(s_bridge.daemon_name, sizeof(s_bridge.daemon_name), "%s", found.name);
            nev_mdns_stop(&s_bridge.mdns);

            NEV_LOGI(TAG, "found %s at %s:%u", found.name, s_bridge.daemon_ip,
                     (unsigned)found.port);
            /* NET.DAEMON_FOUND is net_service's to publish, not ours; the
             * bridge says only what the bridge knows. */
            if (!nev_ws_open(&s_bridge.ws, s_bridge.daemon_ip, s_bridge.daemon_port, "/ws")) {
                disconnect("could not open a socket");
                return;
            }
            set_state(NEV_BRIDGE_CONNECTING);
            break;
        }

        case NEV_BRIDGE_CONNECTING:
        case NEV_BRIDGE_PAIRING:
        case NEV_BRIDGE_READY:
            break;
    }

    if (s_bridge.state == NEV_BRIDGE_SEARCHING || s_bridge.state == NEV_BRIDGE_OFFLINE) return;

    /*
     * Several messages per poll, but not unboundedly many.
     *
     * One per poll would make a burst of agent tokens arrive one every 33 ms,
     * which reads as the model thinking slowly rather than as the device being
     * behind. All of them would overrun the subscriber rings downstream, and
     * the first half of a reply would be evicted before anything drew it.
     *
     * What is left stays in the socket, where TCP's own window holds it — a far
     * better backlog than a ring of 32-byte slots on a device with 512 KB.
     */
    for (int budget = 6; budget > 0; budget--) {
        const uint8_t *payload = NULL;
        size_t payload_len = 0;
        nev_ws_event_t ev = nev_ws_poll(&s_bridge.ws, &payload, &payload_len);

        if (ev == NEV_WS_EV_NONE) break;
        if (ev == NEV_WS_EV_CLOSED) {
            const char *why = nev_ws_fail_reason(&s_bridge.ws);
            disconnect(why ? why : "the link closed");
            return;
        }
        if (ev == NEV_WS_EV_OPEN) {
            s_bridge.last_rx_ms = nev_now_ms();
            s_bridge.last_ping_ms = nev_now_ms();
            send_hello();
            continue;
        }
        on_message(payload, payload_len);
        /* on_message may have torn the link down. */
        if (s_bridge.state == NEV_BRIDGE_OFFLINE) return;
    }

    /* Liveness, once connected. */
    if (s_bridge.state == NEV_BRIDGE_READY) {
        if (nev_elapsed_ms(s_bridge.last_rx_ms) > SILENCE_LIMIT_MS) {
            disconnect("the daemon stopped answering");
            return;
        }
        if (nev_elapsed_ms(s_bridge.last_ping_ms) > PING_INTERVAL_MS &&
            nev_ws_can_send(&s_bridge.ws)) {
            nev_msg_ping_t ping = {.nonce = nev_rand_u32()};
            size_t len = 0;
            if (nev_proto_encode_ping(&ping, s_bridge.scratch, sizeof(s_bridge.scratch), &len) ==
                NEV_OK) {
                (void)send_payload(s_bridge.scratch, len);
            }
            s_bridge.last_ping_ms = nev_now_ms();
        }
    }
}

/* -------------------------------------------------------------------- calls */

nev_bridge_state_t nev_bridge_state(void) {
    return s_bridge.state;
}

const char *nev_bridge_daemon_name(void) {
    return s_bridge.daemon_name;
}

const char *nev_bridge_pairing_code(void) {
    return (s_bridge.state == NEV_BRIDGE_PAIRING && s_bridge.code[0]) ? s_bridge.code : NULL;
}

const char *nev_bridge_device_id(void) {
    return s_bridge.device_id;
}

bool nev_bridge_is_paired(void) {
    return s_bridge.token[0] != '\0';
}

bool nev_bridge_ask(uint32_t turn, const char *text, const char *app) {
    if (s_bridge.state != NEV_BRIDGE_READY || !text) return false;

    nev_msg_agent_request_t req;
    memset(&req, 0, sizeof(req));
    req.turn = turn;
    snprintf(req.text, sizeof(req.text), "%s", text);
    snprintf(req.app, sizeof(req.app), "%s", app ? app : "");

    size_t len = 0;
    if (nev_proto_encode_agent_request(&req, s_bridge.scratch, sizeof(s_bridge.scratch), &len) !=
        NEV_OK) {
        return false;
    }
    return send_payload(s_bridge.scratch, len);
}

bool nev_bridge_mark(uint32_t session, float at_seconds) {
    if (s_bridge.state != NEV_BRIDGE_READY) return false;

    nev_msg_capture_marker_t marker = {.session = session, .at_seconds = at_seconds};
    size_t len = 0;
    if (nev_proto_encode_capture_marker(&marker, s_bridge.scratch, sizeof(s_bridge.scratch),
                                        &len) != NEV_OK) {
        return false;
    }
    return send_payload(s_bridge.scratch, len);
}

bool nev_bridge_send_audio(uint32_t session, uint32_t seq, bool final, const int16_t *pcm,
                           size_t samples) {
    if (s_bridge.state != NEV_BRIDGE_READY) return false;
    if (samples > 0 && !pcm) return false;

    /* The schema caps a chunk at 4096 bytes. The audio service sends smaller
     * ones; a caller that does not is a bug worth failing rather than
     * truncating, since a truncated chunk is silently wrong audio. */
    if (samples * sizeof(int16_t) > 4096) return false;

    /*
     * The device is little-endian and the wire format is little-endian 16-bit
     * PCM, so the samples go out as they sit in memory. Stated rather than
     * assumed: on a big-endian port this is the line that breaks, and it would
     * break as noise rather than as a failure.
     */
    nev_msg_audio_chunk_t chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.seq = seq;
    chunk.session = session;
    chunk.final = final;
    chunk.pcm = (const uint8_t *)pcm;
    chunk.pcm_len = samples * sizeof(int16_t);
    chunk.kind = s_audio_kind;

    size_t len = 0;
    if (nev_proto_encode_audio_chunk(&chunk, s_bridge.scratch, sizeof(s_bridge.scratch), &len) !=
        NEV_OK) {
        return false;
    }
    return send_payload(s_bridge.scratch, len);
}

void nev_bridge_forget(void) {
    s_bridge.token[0] = '\0';
    (void)nev_store_set_str(NEV_SET_PAIR_TOKEN, "");
    (void)nev_store_commit();
    if (s_bridge.state != NEV_BRIDGE_OFFLINE) {
        disconnect("unpaired");
    }
    NEV_LOGI(TAG, "pairing forgotten");
}
