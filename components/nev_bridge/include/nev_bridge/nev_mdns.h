/* NEVOS L6 — just enough mDNS to find the daemon. */
#ifndef NEV_BRIDGE_NEV_MDNS_H
#define NEV_BRIDGE_NEV_MDNS_H

#include "nev_port/nev_net.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The device has no keyboard and the daemon has a DHCP address, so the address
 * has to be discovered. ESP-IDF has an mDNS component and the host has none, so
 * a shared implementation means writing the query and the parse — about 250
 * lines for one question ("who serves _nevos._tcp?") and three record types.
 *
 * This is a resolver for exactly that question. It is not a general mDNS
 * implementation: no caching, no announcing, no conflict resolution, no
 * continuous browsing. It asks, reads what comes back, and stops.
 *
 * Discovery is not authentication. Anything on the network can answer this
 * query, and a device that connected to a liar would then be told to pair —
 * which requires someone to read a code off the device's screen and type it
 * into the machine that is lying. That is the check; nothing here is.
 */

#define NEV_MDNS_SERVICE "_nevos._tcp.local"
#define NEV_MDNS_GROUP   "224.0.0.251"
#define NEV_MDNS_PORT    5353

typedef struct {
    uint32_t ipv4;
    uint16_t port;
    /* The daemon's instance name, shown on the device so the user can tell
     * which of their machines answered. */
    char name[48];
} nev_mdns_result_t;

typedef struct {
    nev_socket_t sock;
    uint32_t last_query_ms;
    uint32_t queries_sent;
    uint8_t buf[600];
} nev_mdns_t;

/* Opens the socket and sends the first query. False if multicast is unavailable. */
bool nev_mdns_start(nev_mdns_t *m);

/*
 * Reads any replies and re-asks on a backoff. Returns true once a daemon has
 * been found, filling `out`.
 *
 * Re-asking matters: the first query often goes out before the Wi-Fi driver has
 * finished joining the multicast group, and a resolver that asked once would
 * then never find a daemon that is running perfectly well.
 */
bool nev_mdns_poll(nev_mdns_t *m, nev_mdns_result_t *out);

void nev_mdns_stop(nev_mdns_t *m);

/* ------------------------------------------------ exposed for unit tests */

/* Builds a PTR query for NEV_MDNS_SERVICE. Returns its length. */
size_t nev_mdns_build_query(uint8_t *out, size_t out_cap);

/*
 * Parses a response packet. True when it contains a complete answer for our
 * service: an instance, a port and an address.
 */
bool nev_mdns_parse(const uint8_t *pkt, size_t len, nev_mdns_result_t *out);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BRIDGE_NEV_MDNS_H */
