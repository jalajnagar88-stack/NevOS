/* NEVOS L6 — a WebSocket client, non-blocking, no allocation. */
#ifndef NEV_BRIDGE_NEV_WS_H
#define NEV_BRIDGE_NEV_WS_H

#include "nev_port/nev_net.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Why write this rather than use esp_websocket_client:
 *
 * esp_websocket_client is an ESP-IDF component, so a bridge built on it can
 * only run on the device. Every bug in the handshake, the frame codec, the
 * reconnect policy and the pairing flow would then be found by plugging in a
 * robot and reading a serial log. This implementation is portable C over
 * nev_port sockets, so the same code — the same code, not an equivalent — runs
 * against the real daemon from the simulator and from a unit test.
 *
 * It implements the subset the protocol uses: RFC 6455 client handshake,
 * binary frames, ping/pong, close. No text frames, no fragmentation on send,
 * no extensions, no TLS. The link is a home LAN to a daemon the user paired
 * with by reading a code off the screen; adding TLS would mean a certificate
 * story for a machine with a DHCP address and no name, which is a great deal
 * of machinery for a threat the pairing step already addresses.
 *
 * Nothing here allocates. The caller owns the struct, buffers included.
 */

/* Sized by the protocol: max_frame_bytes is 8192, but the largest message the
 * daemon actually sends is a 512-byte agent token, and the device is the one
 * with 512 KB of RAM. A larger inbound frame is refused rather than buffered. */
#ifndef NEV_WS_RX_CAP
#define NEV_WS_RX_CAP 2048
#endif
/* The device's largest outbound message is a 4096-byte audio chunk, plus CBOR
 * overhead, the length prefix and a 14-byte WebSocket header. */
#ifndef NEV_WS_TX_CAP
#define NEV_WS_TX_CAP 4352
#endif

typedef enum {
    NEV_WS_CLOSED = 0,
    NEV_WS_CONNECTING, /* TCP handshake in flight */
    NEV_WS_UPGRADING,  /* HTTP upgrade sent, response not yet complete */
    NEV_WS_OPEN,       /* ready for frames */
} nev_ws_state_t;

typedef enum {
    NEV_WS_EV_NONE = 0, /* nothing happened this poll */
    NEV_WS_EV_OPEN,     /* the handshake completed */
    NEV_WS_EV_MESSAGE,  /* a complete binary message is available */
    NEV_WS_EV_CLOSED,   /* the peer closed, or the link failed */
} nev_ws_event_t;

typedef struct {
    nev_socket_t sock;
    nev_ws_state_t state;

    /* The nonce we sent, and the accept value it obliges the server to return. */
    char accept_expected[32];

    uint8_t rx[NEV_WS_RX_CAP];
    size_t rx_len;
    /* A delivered message is left in place until the next poll, so the caller
     * gets a pointer into rx rather than a copy. This is how many bytes to drop
     * when that next poll comes. */
    size_t pending_consume;

    uint8_t tx[NEV_WS_TX_CAP];
    size_t tx_len;
    size_t tx_sent;

    /* Set when the peer says something the protocol does not allow. Sticky:
     * once a stream is out of sync there is no way back into it. */
    bool failed;
    const char *fail_reason;
} nev_ws_t;

/*
 * Starts connecting. Returns false if the socket could not be opened at all.
 * `path` is the HTTP resource, e.g. "/ws".
 */
bool nev_ws_open(nev_ws_t *ws, const char *ipv4, uint16_t port, const char *path);

/*
 * Advances the connection. Call it often; it never blocks.
 *
 * Returns one event per call. When it returns NEV_WS_EV_MESSAGE, the payload is
 * at *out / *out_len and stays valid until the next call — the caller must
 * consume it before polling again, which is why the bridge decodes into an
 * event on the spot rather than queueing the pointer.
 */
nev_ws_event_t nev_ws_poll(nev_ws_t *ws, const uint8_t **out, size_t *out_len);

/*
 * Queues a binary message. Returns false when the send buffer is still busy
 * with the previous one — the caller must retry rather than dropping, or must
 * decide to drop, which for audio is the right answer and for a pairing code
 * is not.
 */
bool nev_ws_send(nev_ws_t *ws, const uint8_t *payload, size_t len);

/* True when a message can be queued right now. */
bool nev_ws_can_send(const nev_ws_t *ws);

void nev_ws_close(nev_ws_t *ws);

nev_ws_state_t nev_ws_state(const nev_ws_t *ws);

/* Why the connection failed, for the log. NULL when it did not. */
const char *nev_ws_fail_reason(const nev_ws_t *ws);

/* ------------------------------------------------ exposed for unit tests */

/* Builds the Sec-WebSocket-Accept value a server must return for `key`. */
void nev_ws_accept_for_key(const char *key_b64, char *out, size_t out_cap);

/*
 * Writes a masked client frame into `out`. Returns the total length, or 0 if it
 * does not fit. `mask` is the 4-byte masking key.
 */
size_t nev_ws_frame_write(uint8_t *out, size_t out_cap, uint8_t opcode, const uint8_t *payload,
                          size_t len, const uint8_t mask[4]);

/*
 * Parses a server frame from `buf`. On success sets *payload_at / *payload_len
 * (offsets into buf) and *consumed, and returns true. Returns false and sets
 * *need_more when the frame is incomplete, or leaves it false on a malformed
 * frame.
 */
bool nev_ws_frame_read(const uint8_t *buf, size_t len, uint8_t *opcode, size_t *payload_at,
                       size_t *payload_len, size_t *consumed, bool *need_more);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BRIDGE_NEV_WS_H */
