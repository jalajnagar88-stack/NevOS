/* Just enough mDNS to find the daemon. See nev_mdns.h. */
#include "nev_bridge/nev_mdns.h"

#include <string.h>

#include "nev_port/nev_log.h"
#include "nev_port/nev_time.h"

#define DNS_TYPE_A        1
#define DNS_TYPE_PTR      12
#define DNS_TYPE_TXT      16
#define DNS_TYPE_SRV      33

/* How long between re-asks, and how many times. The first query frequently goes
 * out before the multicast group has been joined. */
#define QUERY_INTERVAL_MS 1500
#define MAX_QUERIES       40 /* a minute of trying, then the caller decides */

/*
 * Reads a DNS name at `at` into `out` as dotted text.
 *
 * Handles compression pointers, which the responders that matter all use, and
 * refuses to follow more than a bounded number of them: a packet can point a
 * name at itself, and a parser that followed it would hang the network task
 * forever on one malformed datagram from anyone on the LAN.
 *
 * Returns the offset just past the name as encoded at `at` (not past whatever a
 * pointer led to), or 0 on a malformed name.
 */
static size_t read_name(const uint8_t *pkt, size_t len, size_t at, char *out, size_t out_cap) {
    size_t o = 0;
    size_t here = at;
    size_t end_of_name = 0;
    int jumps = 0;

    if (out_cap == 0) return 0;
    out[0] = '\0';

    for (;;) {
        if (here >= len) return 0;
        uint8_t label = pkt[here];

        if ((label & 0xC0) == 0xC0) {
            if (here + 1 >= len) return 0;
            if (++jumps > 8) return 0;
            size_t target = (size_t)(((label & 0x3F) << 8) | pkt[here + 1]);
            if (end_of_name == 0) end_of_name = here + 2;
            /* A pointer must point backwards. Forward or self pointers are the
             * loop this guards against. */
            if (target >= here) return 0;
            here = target;
            continue;
        }
        if (label == 0) {
            if (end_of_name == 0) end_of_name = here + 1;
            break;
        }
        if ((label & 0xC0) != 0) return 0; /* reserved label type */

        here++;
        if (here + label > len) return 0;
        if (o != 0) {
            if (o + 1 >= out_cap) return 0;
            out[o++] = '.';
        }
        if (o + label >= out_cap) return 0;
        memcpy(out + o, pkt + here, label);
        o += label;
        here += label;
    }

    out[o] = '\0';
    return end_of_name;
}

/* Case-insensitive, because DNS names are. */
static bool name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* True when `name` is an instance of our service, e.g.
 * "studio-mac._nevos._tcp.local". */
static bool is_our_instance(const char *name) {
    size_t n = strlen(name);
    size_t suffix = strlen(NEV_MDNS_SERVICE) + 1; /* the dot before it */
    if (n <= suffix) return false;
    const char *tail = name + n - suffix;
    return tail[0] == '.' && name_eq(tail + 1, NEV_MDNS_SERVICE);
}

/* Writes a dotted name in DNS label form. Returns bytes written, 0 if it does
 * not fit. */
static size_t write_name(uint8_t *out, size_t out_cap, const char *name) {
    size_t o = 0;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t label_len = dot ? (size_t)(dot - p) : strlen(p);
        if (label_len == 0 || label_len > 63) return 0;
        if (o + 1 + label_len + 1 > out_cap) return 0;
        out[o++] = (uint8_t)label_len;
        memcpy(out + o, p, label_len);
        o += label_len;
        p += label_len;
        if (*p == '.') p++;
    }
    if (o + 1 > out_cap) return 0;
    out[o++] = 0;
    return o;
}

size_t nev_mdns_build_query(uint8_t *out, size_t out_cap) {
    if (!out || out_cap < 12) return 0;

    memset(out, 0, 12);
    /* Transaction id 0: mDNS responders ignore it, and a device with no entropy
     * yet at boot should not pretend otherwise. Flags 0 is a standard query. */
    out[4] = 0;
    out[5] = 1; /* one question */

    size_t o = 12;
    size_t n = write_name(out + o, out_cap - o, NEV_MDNS_SERVICE);
    if (n == 0) return 0;
    o += n;

    if (o + 4 > out_cap) return 0;
    out[o++] = 0;
    out[o++] = DNS_TYPE_PTR;
    /*
     * Class IN with the unicast-response bit set (RFC 6762 §5.4).
     *
     * Without it, answers are multicast to 224.0.0.251:5353 and only a socket
     * bound to 5353 sees them — which means fighting the machine's existing
     * responder for the port. Asking for a unicast reply gets the answer sent
     * straight back to our source port. We bind 5353 as well, because a
     * responder is allowed to ignore the bit, but this is what makes discovery
     * work on a machine already running avahi.
     */
    out[o++] = 0x80;
    out[o++] = 1;
    return o;
}

/* Steps over a question entry. Returns the offset after it, or 0. */
static size_t skip_question(const uint8_t *pkt, size_t len, size_t at) {
    char scratch[256];
    size_t after = read_name(pkt, len, at, scratch, sizeof(scratch));
    if (after == 0 || after + 4 > len) return 0;
    return after + 4;
}

bool nev_mdns_parse(const uint8_t *pkt, size_t len, nev_mdns_result_t *out) {
    if (!pkt || !out || len < 12) return false;

    uint16_t qd = (uint16_t)((pkt[4] << 8) | pkt[5]);
    uint16_t counts[3] = {
        (uint16_t)((pkt[6] << 8) | pkt[7]),   /* answers */
        (uint16_t)((pkt[8] << 8) | pkt[9]),   /* authority */
        (uint16_t)((pkt[10] << 8) | pkt[11]), /* additional */
    };

    size_t at = 12;
    for (uint16_t i = 0; i < qd; i++) {
        at = skip_question(pkt, len, at);
        if (at == 0) return false;
    }

    /*
     * Three records have to agree before we have an address to connect to:
     * PTR names the instance, SRV gives that instance a port and a hostname,
     * and A gives that hostname an address. A responder normally packs all
     * three into one datagram, which is why one pass over one packet is enough.
     */
    char instance[128] = {0};
    char target[128] = {0};
    uint16_t port = 0;
    uint32_t ipv4 = 0;

    /* Names seen with an A record, so the address can be matched to the SRV
     * target whichever order they arrive in. */
    char a_name[128] = {0};

    size_t total = (size_t)counts[0] + counts[1] + counts[2];
    for (size_t i = 0; i < total; i++) {
        char name[128];
        size_t after = read_name(pkt, len, at, name, sizeof(name));
        if (after == 0 || after + 10 > len) return false;

        uint16_t type = (uint16_t)((pkt[after] << 8) | pkt[after + 1]);
        uint16_t rdlen = (uint16_t)((pkt[after + 8] << 8) | pkt[after + 9]);
        size_t rdata = after + 10;
        if (rdata + rdlen > len) return false;

        switch (type) {
            case DNS_TYPE_PTR:
                if (name_eq(name, NEV_MDNS_SERVICE) && instance[0] == '\0') {
                    if (read_name(pkt, len, rdata, instance, sizeof(instance)) == 0) return false;
                }
                break;

            case DNS_TYPE_SRV:
                /* Priority, weight, port, target. */
                if (rdlen < 7) return false;
                /*
                 * The name must be an instance of our service, not merely the
                 * first SRV in the packet. A home network answers this group
                 * with printers and televisions, and taking any SRV would point
                 * the device at one of them.
                 *
                 * Matched by suffix rather than against the PTR, because some
                 * responders answer with SRV and A but no PTR at all.
                 */
                if (!is_our_instance(name)) break;
                if (instance[0] != '\0' && !name_eq(name, instance)) break;
                port = (uint16_t)((pkt[rdata + 4] << 8) | pkt[rdata + 5]);
                if (read_name(pkt, len, rdata + 6, target, sizeof(target)) == 0) return false;
                if (instance[0] == '\0') {
                    memcpy(instance, name, sizeof(instance) - 1);
                }
                break;

            case DNS_TYPE_A:
                if (rdlen == 4 && ipv4 == 0) {
                    ipv4 = ((uint32_t)pkt[rdata] << 24) | ((uint32_t)pkt[rdata + 1] << 16) |
                           ((uint32_t)pkt[rdata + 2] << 8) | (uint32_t)pkt[rdata + 3];
                    memcpy(a_name, name, sizeof(a_name) - 1);
                }
                break;

            default:
                break;
        }
        at = rdata + rdlen;
    }

    if (port == 0 || ipv4 == 0) return false;
    /* If both are present they must agree, or we would be connecting to some
     * other service that happened to share the packet. */
    if (target[0] && a_name[0] && !name_eq(target, a_name)) return false;

    out->ipv4 = ipv4;
    out->port = port;

    /* "studio-mac._nevos._tcp.local" reads better as "studio-mac". */
    out->name[0] = '\0';
    if (instance[0]) {
        const char *dot = strchr(instance, '.');
        size_t n = dot ? (size_t)(dot - instance) : strlen(instance);
        if (n >= sizeof(out->name)) n = sizeof(out->name) - 1;
        memcpy(out->name, instance, n);
        out->name[n] = '\0';
    }
    return true;
}

bool nev_mdns_start(nev_mdns_t *m) {
    if (!m) return false;
    memset(m, 0, sizeof(*m));

    /*
     * Port 5353 first, so multicast answers are seen; an ephemeral port if that
     * is taken by a responder that will not share it. The query asks for a
     * unicast reply either way, which is what makes the fallback work.
     */
    m->sock = nev_net_udp_open(NEV_MDNS_PORT, NEV_MDNS_GROUP);
    if (m->sock == NEV_SOCKET_INVALID) {
        m->sock = nev_net_udp_open(0, NEV_MDNS_GROUP);
    }
    if (m->sock == NEV_SOCKET_INVALID) return false;

    uint8_t query[128];
    size_t n = nev_mdns_build_query(query, sizeof(query));
    if (n == 0) {
        nev_mdns_stop(m);
        return false;
    }
    if (nev_net_sendto(m->sock, NEV_MDNS_GROUP, NEV_MDNS_PORT, query, n) < 0) {
        nev_mdns_stop(m);
        return false;
    }
    m->last_query_ms = nev_now_ms();
    m->queries_sent = 1;
    return true;
}

bool nev_mdns_poll(nev_mdns_t *m, nev_mdns_result_t *out) {
    if (!m || m->sock == NEV_SOCKET_INVALID || !out) return false;

    for (;;) {
        int n = nev_net_recvfrom(m->sock, m->buf, sizeof(m->buf), NULL);
        if (n <= 0) break;
        /* A packet that is not for us, or is malformed, is ordinary on a busy
         * network: every Chromecast and printer answers on this group. */
        if (nev_mdns_parse(m->buf, (size_t)n, out)) return true;
    }

    if (m->queries_sent < MAX_QUERIES && nev_elapsed_ms(m->last_query_ms) >= QUERY_INTERVAL_MS) {
        uint8_t query[128];
        size_t len = nev_mdns_build_query(query, sizeof(query));
        if (len > 0) (void)nev_net_sendto(m->sock, NEV_MDNS_GROUP, NEV_MDNS_PORT, query, len);
        m->last_query_ms = nev_now_ms();
        m->queries_sent++;
    }
    return false;
}

void nev_mdns_stop(nev_mdns_t *m) {
    if (!m) return;
    if (m->sock != NEV_SOCKET_INVALID) {
        nev_net_close(m->sock);
        m->sock = NEV_SOCKET_INVALID;
    }
}
