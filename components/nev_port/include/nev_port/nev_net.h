/* NEVOS L-1 — non-blocking TCP and UDP, for the one thing that talks off-device. */
#ifndef NEV_PORT_NEV_NET_H
#define NEV_PORT_NEV_NET_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Why this exists at L-1 rather than as an ESP-IDF call in nev_bridge:
 *
 * The bridge is the largest piece of logic in the system that nobody can watch
 * while it runs — a WebSocket handshake, a frame codec, a reconnect policy and
 * a pairing flow, all of it happening while the user is looking at a face. If
 * it could only run on the device, every bug in it would be found by plugging
 * in a robot and reading a serial log. Behind this header, the entire bridge
 * compiles on the host and talks to the real daemon over a real socket.
 *
 * The implementation is shared rather than forked: ESP-IDF's lwIP provides the
 * BSD socket API under the standard headers, so src/common/nev_net_posix.c is
 * the implementation for both targets. See the note at the top of that file
 * for the two places where they genuinely differ.
 *
 * Everything here is non-blocking. A socket that blocks blocks the task, and
 * the task that owns the bridge also owns Wi-Fi. `nev_net_connect` starts a
 * connection and returns immediately; `nev_net_connect_poll` says when it has
 * finished. R3 (nothing blocks the frame) applies here as much as anywhere.
 */

#define NEV_SOCKET_INVALID (-1)

typedef int nev_socket_t;

typedef enum {
    NEV_CONN_PENDING = 0, /* still in progress; ask again later */
    NEV_CONN_READY,       /* connected */
    NEV_CONN_FAILED,      /* refused, unreachable, or timed out */
} nev_conn_state_t;

/* An IPv4 address in host byte order, and a port. */
typedef struct {
    uint32_t ipv4;
    uint16_t port;
} nev_endpoint_t;

/* ------------------------------------------------------------------- TCP */

/*
 * Starts a connection. Returns a socket immediately — the connection is not
 * established yet. Poll it with nev_net_connect_poll.
 *
 * `host` is a dotted-quad address, not a hostname: the device finds the daemon
 * by mDNS, which answers with an address, and a DNS resolver call is the one
 * socket operation that has no non-blocking form worth using here.
 */
nev_socket_t nev_net_connect(const char *ipv4, uint16_t port);

nev_conn_state_t nev_net_connect_poll(nev_socket_t sock);

/*
 * Sends as much as the socket will take. Returns bytes accepted, 0 if the
 * socket is full right now, or negative on a dead connection. A partial send is
 * normal and is the caller's to finish.
 */
int nev_net_send(nev_socket_t sock, const uint8_t *data, size_t len);

/*
 * Reads what is available. Returns bytes read, 0 if nothing is waiting, or
 * negative when the peer has closed or the connection has failed.
 */
int nev_net_recv(nev_socket_t sock, uint8_t *out, size_t max);

void nev_net_close(nev_socket_t sock);

/* ------------------------------------------------------------------- UDP */

/*
 * A UDP socket bound to `port` (0 for any), joined to `mcast_ipv4` if non-NULL.
 * Used for one thing: mDNS discovery of the daemon.
 */
nev_socket_t nev_net_udp_open(uint16_t port, const char *mcast_ipv4);

int nev_net_sendto(nev_socket_t sock, const char *ipv4, uint16_t port, const uint8_t *data,
                   size_t len);

/* Returns bytes read and fills `from` when non-NULL; 0 when nothing is waiting. */
int nev_net_recvfrom(nev_socket_t sock, uint8_t *out, size_t max, nev_endpoint_t *from);

/* Formats an address from nev_net_recvfrom into dotted quad. `out` needs 16 bytes. */
void nev_net_ipv4_str(uint32_t ipv4, char *out, size_t out_len);

/* True once the network stack has an address. The host build is always up. */
bool nev_net_is_up(void);

/* Called by nev_board (L0) when Wi-Fi gains or loses an address. Nothing above
 * L0 has any business calling it. */
void nev_net_set_up(bool up);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_NET_H */
