/* A WebSocket client. See nev_ws.h for why this is written rather than used. */
#include "nev_bridge/nev_ws.h"

#include <stdio.h>
#include <string.h>

#include "nev_port/nev_log.h"
#include "nev_port/nev_rand.h"
#include "nev_sha1.h"

#define WS_OP_CONT  0x0
#define WS_OP_TEXT  0x1
#define WS_OP_BIN   0x2
#define WS_OP_CLOSE 0x8
#define WS_OP_PING  0x9
#define WS_OP_PONG  0xA

/* RFC 6455 §1.3. A constant, not a secret. */
static const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void fail(nev_ws_t *ws, const char *why) {
    if (!ws->failed) {
        ws->failed = true;
        ws->fail_reason = why;
        NEV_LOGW("ws", "%s", why);
    }
    ws->state = NEV_WS_CLOSED;
    if (ws->sock != NEV_SOCKET_INVALID) {
        nev_net_close(ws->sock);
        ws->sock = NEV_SOCKET_INVALID;
    }
}

void nev_ws_accept_for_key(const char *key_b64, char *out, size_t out_cap) {
    if (!key_b64 || !out || out_cap == 0) return;

    char joined[128];
    size_t key_len = strlen(key_b64);
    if (key_len + sizeof(kGuid) > sizeof(joined)) {
        out[0] = '\0';
        return;
    }
    memcpy(joined, key_b64, key_len);
    memcpy(joined + key_len, kGuid, sizeof(kGuid)); /* includes the NUL */

    uint8_t digest[NEV_SHA1_DIGEST_LEN];
    nev_sha1((const uint8_t *)joined, key_len + sizeof(kGuid) - 1, digest);
    nev_base64(digest, sizeof(digest), out, out_cap);
}

size_t nev_ws_frame_write(uint8_t *out, size_t out_cap, uint8_t opcode, const uint8_t *payload,
                          size_t len, const uint8_t mask[4]) {
    if (!out || !mask) return 0;

    /* 2 bytes of header, up to 8 more for an extended length, 4 for the mask. */
    size_t header = 2 + 4;
    if (len > 125 && len <= 0xFFFF) {
        header += 2;
    } else if (len > 0xFFFF) {
        header += 8;
    }
    if (header + len > out_cap) return 0;

    size_t o = 0;
    out[o++] = (uint8_t)(0x80 | (opcode & 0x0F)); /* FIN, no RSV */

    if (len <= 125) {
        out[o++] = (uint8_t)(0x80 | len); /* MASK set: a client frame must be */
    } else if (len <= 0xFFFF) {
        out[o++] = 0x80 | 126;
        out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)len;
    } else {
        out[o++] = 0x80 | 127;
        for (int i = 0; i < 8; i++) {
            out[o++] = (uint8_t)((uint64_t)len >> (56 - 8 * i));
        }
    }

    memcpy(out + o, mask, 4);
    o += 4;
    for (size_t i = 0; i < len; i++) {
        out[o + i] = (uint8_t)(payload[i] ^ mask[i % 4]);
    }
    return o + len;
}

bool nev_ws_frame_read(const uint8_t *buf, size_t len, uint8_t *opcode, size_t *payload_at,
                       size_t *payload_len, size_t *consumed, bool *need_more) {
    if (need_more) *need_more = false;
    if (!buf || !opcode || !payload_at || !payload_len || !consumed) return false;

    if (len < 2) {
        if (need_more) *need_more = true;
        return false;
    }

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];

    /* The subset this client speaks: no reserved bits, no fragmentation
     * inbound, and never a mask — a server frame that sets MASK is a protocol
     * error, not something to helpfully unmask. */
    if ((b0 & 0x70) != 0) return false;
    if ((b0 & 0x80) == 0) return false;
    if ((b1 & 0x80) != 0) return false;

    *opcode = (uint8_t)(b0 & 0x0F);

    size_t at = 2;
    uint64_t plen = (uint64_t)(b1 & 0x7F);
    if (plen == 126) {
        if (len < 4) {
            if (need_more) *need_more = true;
            return false;
        }
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        at = 4;
        /* A length that could have been encoded shorter is not canonical.
         * Refusing it keeps one frame from having two encodings. */
        if (plen <= 125) return false;
    } else if (plen == 127) {
        if (len < 10) {
            if (need_more) *need_more = true;
            return false;
        }
        plen = 0;
        for (int i = 0; i < 8; i++) {
            plen = (plen << 8) | buf[2 + i];
        }
        at = 10;
        if (plen <= 0xFFFF) return false;
        /* The protocol's own cap is 8192; anything claiming more is either a
         * bug or an attempt to make us allocate. */
        if (plen > NEV_WS_RX_CAP) return false;
    }

    if (plen > NEV_WS_RX_CAP) return false;
    if (len < at + plen) {
        if (need_more) *need_more = true;
        return false;
    }

    *payload_at = at;
    *payload_len = (size_t)plen;
    *consumed = at + (size_t)plen;
    return true;
}

bool nev_ws_open(nev_ws_t *ws, const char *ipv4, uint16_t port, const char *path) {
    if (!ws || !ipv4 || !path) return false;

    memset(ws, 0, sizeof(*ws));
    ws->sock = nev_net_connect(ipv4, port);
    if (ws->sock == NEV_SOCKET_INVALID) {
        ws->fail_reason = "could not open a socket";
        ws->failed = true;
        return false;
    }

    /* Build the upgrade request now, while the address and path are in hand;
     * it is sent once the TCP connection completes. */
    uint8_t nonce[16];
    nev_rand_fill(nonce, sizeof(nonce));
    char key_b64[25];
    nev_base64(nonce, sizeof(nonce), key_b64, sizeof(key_b64));
    nev_ws_accept_for_key(key_b64, ws->accept_expected, sizeof(ws->accept_expected));

    int n = snprintf((char *)ws->tx, sizeof(ws->tx),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     path, ipv4, (unsigned)port, key_b64);
    if (n <= 0 || (size_t)n >= sizeof(ws->tx)) {
        fail(ws, "upgrade request does not fit");
        return false;
    }
    ws->tx_len = (size_t)n;
    ws->tx_sent = 0;
    ws->state = NEV_WS_CONNECTING;
    return true;
}

/* Pushes whatever is left of the outbound buffer. Returns false on a dead link. */
static bool flush_tx(nev_ws_t *ws) {
    while (ws->tx_sent < ws->tx_len) {
        int n = nev_net_send(ws->sock, ws->tx + ws->tx_sent, ws->tx_len - ws->tx_sent);
        if (n < 0) return false;
        if (n == 0) return true; /* socket full; try again next poll */
        ws->tx_sent += (size_t)n;
    }
    ws->tx_len = 0;
    ws->tx_sent = 0;
    return true;
}

/* Case-insensitive search for a header value in a NUL-terminated response. */
static const char *header_value(const char *head, const char *name) {
    size_t name_len = strlen(name);
    for (const char *p = head; *p; p++) {
        if ((p == head || p[-1] == '\n')) {
            size_t i = 0;
            while (i < name_len && p[i] && ((p[i] | 0x20) == (name[i] | 0x20))) {
                i++;
            }
            if (i == name_len && p[i] == ':') {
                const char *v = p + i + 1;
                while (*v == ' ' || *v == '\t')
                    v++;
                return v;
            }
        }
    }
    return NULL;
}

/* Reads and checks the HTTP upgrade response. */
static void poll_upgrade(nev_ws_t *ws) {
    int n = nev_net_recv(ws->sock, ws->rx + ws->rx_len, sizeof(ws->rx) - ws->rx_len - 1);
    if (n < 0) {
        fail(ws, "connection closed during the upgrade");
        return;
    }
    if (n == 0) return;
    ws->rx_len += (size_t)n;
    ws->rx[ws->rx_len] = '\0';

    char *end = strstr((char *)ws->rx, "\r\n\r\n");
    if (!end) {
        /* Leave one byte for the NUL above. */
        if (ws->rx_len + 2 >= sizeof(ws->rx)) fail(ws, "upgrade response is too large");
        return;
    }

    size_t head_len = (size_t)(end - (char *)ws->rx) + 4;
    *end = '\0';

    if (strncmp((char *)ws->rx, "HTTP/1.1 101", 12) != 0) {
        fail(ws, "the daemon refused the upgrade");
        return;
    }
    const char *accept = header_value((char *)ws->rx, "Sec-WebSocket-Accept");
    if (!accept) {
        fail(ws, "no Sec-WebSocket-Accept in the response");
        return;
    }
    /*
     * Checking the accept value is what makes this a WebSocket handshake rather
     * than an HTTP request that happened to return 101. Without it, anything
     * answering on the port — a captive portal, a stale process — would be
     * treated as the daemon and fed audio.
     */
    size_t expect_len = strlen(ws->accept_expected);
    if (strncmp(accept, ws->accept_expected, expect_len) != 0) {
        fail(ws, "Sec-WebSocket-Accept did not match the key we sent");
        return;
    }

    /* Anything after the header is already frame data. */
    memmove(ws->rx, ws->rx + head_len, ws->rx_len - head_len);
    ws->rx_len -= head_len;
    ws->state = NEV_WS_OPEN;
}

/* Queues a control frame, discarding it if the buffer is busy. */
static void send_control(nev_ws_t *ws, uint8_t opcode, const uint8_t *payload, size_t len) {
    if (ws->tx_len != 0) return; /* a pong we cannot send now is not worth stalling for */
    uint8_t mask[4];
    nev_rand_fill(mask, sizeof(mask));
    size_t written = nev_ws_frame_write(ws->tx, sizeof(ws->tx), opcode, payload, len, mask);
    if (written == 0) return;
    ws->tx_len = written;
    ws->tx_sent = 0;
    (void)flush_tx(ws);
}

nev_ws_event_t nev_ws_poll(nev_ws_t *ws, const uint8_t **out, size_t *out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!ws || ws->state == NEV_WS_CLOSED) return NEV_WS_EV_NONE;

    if (ws->state == NEV_WS_CONNECTING) {
        switch (nev_net_connect_poll(ws->sock)) {
            case NEV_CONN_PENDING:
                return NEV_WS_EV_NONE;
            case NEV_CONN_FAILED:
                fail(ws, "could not reach the daemon");
                return NEV_WS_EV_CLOSED;
            case NEV_CONN_READY:
                ws->state = NEV_WS_UPGRADING;
                break;
        }
    }

    if (!flush_tx(ws)) {
        fail(ws, "the connection dropped while sending");
        return NEV_WS_EV_CLOSED;
    }

    if (ws->state == NEV_WS_UPGRADING) {
        /* Nothing is sent until the whole request has gone out. */
        if (ws->tx_len != 0) return NEV_WS_EV_NONE;
        poll_upgrade(ws);
        if (ws->failed) return NEV_WS_EV_CLOSED;
        return (ws->state == NEV_WS_OPEN) ? NEV_WS_EV_OPEN : NEV_WS_EV_NONE;
    }

    /* Drop the message handed out last time. Deferring the compaction is what
     * lets nev_ws_poll return a pointer into rx instead of copying every frame
     * — at the cost of the caller having to consume it before polling again,
     * which the header states and the bridge does. */
    if (ws->pending_consume != 0) {
        memmove(ws->rx, ws->rx + ws->pending_consume, ws->rx_len - ws->pending_consume);
        ws->rx_len -= ws->pending_consume;
        ws->pending_consume = 0;
    }

    /* Open: read whatever has arrived, then take one message from the buffer. */
    if (ws->rx_len < sizeof(ws->rx)) {
        int n = nev_net_recv(ws->sock, ws->rx + ws->rx_len, sizeof(ws->rx) - ws->rx_len);
        if (n < 0) {
            fail(ws, "the daemon closed the connection");
            return NEV_WS_EV_CLOSED;
        }
        ws->rx_len += (size_t)n;
    }

    for (;;) {
        uint8_t opcode = 0;
        size_t at = 0, plen = 0, consumed = 0;
        bool need_more = false;
        if (!nev_ws_frame_read(ws->rx, ws->rx_len, &opcode, &at, &plen, &consumed, &need_more)) {
            if (need_more) {
                /* A frame larger than the buffer can never complete, so waiting
                 * for more would be waiting forever. */
                if (ws->rx_len == sizeof(ws->rx)) {
                    fail(ws, "inbound frame is larger than the receive buffer");
                    return NEV_WS_EV_CLOSED;
                }
                return NEV_WS_EV_NONE;
            }
            fail(ws, "malformed WebSocket frame");
            return NEV_WS_EV_CLOSED;
        }

        switch (opcode) {
            case WS_OP_BIN:
                if (out) *out = ws->rx + at;
                if (out_len) *out_len = plen;
                ws->pending_consume = consumed;
                return NEV_WS_EV_MESSAGE;

            case WS_OP_PING:
                send_control(ws, WS_OP_PONG, ws->rx + at, plen);
                break;

            case WS_OP_PONG:
                break;

            case WS_OP_CLOSE:
                fail(ws, "the daemon closed the connection");
                return NEV_WS_EV_CLOSED;

            case WS_OP_TEXT:
                fail(ws, "the daemon sent a text frame on a binary protocol");
                return NEV_WS_EV_CLOSED;

            default:
                fail(ws, "unknown WebSocket opcode");
                return NEV_WS_EV_CLOSED;
        }

        memmove(ws->rx, ws->rx + consumed, ws->rx_len - consumed);
        ws->rx_len -= consumed;
    }
}

bool nev_ws_can_send(const nev_ws_t *ws) {
    return ws && ws->state == NEV_WS_OPEN && ws->tx_len == 0;
}

bool nev_ws_send(nev_ws_t *ws, const uint8_t *payload, size_t len) {
    if (!nev_ws_can_send(ws) || (!payload && len > 0)) return false;

    uint8_t mask[4];
    nev_rand_fill(mask, sizeof(mask));
    size_t written = nev_ws_frame_write(ws->tx, sizeof(ws->tx), WS_OP_BIN, payload, len, mask);
    if (written == 0) return false;

    ws->tx_len = written;
    ws->tx_sent = 0;
    if (!flush_tx(ws)) {
        fail(ws, "the connection dropped while sending");
        return false;
    }
    return true;
}

void nev_ws_close(nev_ws_t *ws) {
    if (!ws) return;
    if (ws->state == NEV_WS_OPEN) {
        /* A close frame is a courtesy; if it cannot go out now, the socket
         * closing says the same thing. */
        uint8_t reason[2] = {0x03, 0xE8}; /* 1000, normal closure */
        send_control(ws, WS_OP_CLOSE, reason, sizeof(reason));
    }
    if (ws->sock != NEV_SOCKET_INVALID) {
        nev_net_close(ws->sock);
        ws->sock = NEV_SOCKET_INVALID;
    }
    ws->state = NEV_WS_CLOSED;
    ws->rx_len = 0;
    ws->tx_len = 0;
    ws->tx_sent = 0;
}

nev_ws_state_t nev_ws_state(const nev_ws_t *ws) {
    return ws ? ws->state : NEV_WS_CLOSED;
}

const char *nev_ws_fail_reason(const nev_ws_t *ws) {
    return (ws && ws->failed) ? ws->fail_reason : NULL;
}
