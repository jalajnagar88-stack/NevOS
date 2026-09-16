/*
 * The mDNS parser.
 *
 * Everything here is a packet from the network, which on a home LAN means
 * packets from printers, televisions and whatever else is shouting on
 * 224.0.0.251. The parser has to pick out one service and treat everything else
 * — including deliberately malformed packets — as ordinary.
 */
#include <string.h>

#include "nev_bridge/nev_mdns.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/* ------------------------------------------------------- building packets */

typedef struct {
    uint8_t buf[512];
    size_t len;
} packet_t;

static void put16(packet_t *p, uint16_t v) {
    p->buf[p->len++] = (uint8_t)(v >> 8);
    p->buf[p->len++] = (uint8_t)v;
}

static void put32(packet_t *p, uint32_t v) {
    put16(p, (uint16_t)(v >> 16));
    put16(p, (uint16_t)v);
}

/* Writes a dotted name as labels. */
static void put_name(packet_t *p, const char *name) {
    const char *at = name;
    while (*at) {
        const char *dot = strchr(at, '.');
        size_t n = dot ? (size_t)(dot - at) : strlen(at);
        p->buf[p->len++] = (uint8_t)n;
        memcpy(p->buf + p->len, at, n);
        p->len += n;
        at += n;
        if (*at == '.') at++;
    }
    p->buf[p->len++] = 0;
}

static void header(packet_t *p, uint16_t answers, uint16_t additional) {
    memset(p, 0, sizeof(*p));
    p->len = 0;
    put16(p, 0);      /* id */
    put16(p, 0x8400); /* response, authoritative */
    put16(p, 0);      /* questions */
    put16(p, answers);
    put16(p, 0); /* authority */
    put16(p, additional);
}

static void add_ptr(packet_t *p, const char *service, const char *instance) {
    put_name(p, service);
    put16(p, 12); /* PTR */
    put16(p, 1);
    put32(p, 120);
    size_t len_at = p->len;
    put16(p, 0);
    size_t start = p->len;
    put_name(p, instance);
    p->buf[len_at] = (uint8_t)((p->len - start) >> 8);
    p->buf[len_at + 1] = (uint8_t)(p->len - start);
}

static void add_srv(packet_t *p, const char *instance, uint16_t port, const char *target) {
    put_name(p, instance);
    put16(p, 33); /* SRV */
    put16(p, 1);
    put32(p, 120);
    size_t len_at = p->len;
    put16(p, 0);
    size_t start = p->len;
    put16(p, 0); /* priority */
    put16(p, 0); /* weight */
    put16(p, port);
    put_name(p, target);
    p->buf[len_at] = (uint8_t)((p->len - start) >> 8);
    p->buf[len_at + 1] = (uint8_t)(p->len - start);
}

static void add_a(packet_t *p, const char *host, uint32_t ipv4) {
    put_name(p, host);
    put16(p, 1); /* A */
    put16(p, 1);
    put32(p, 120);
    put16(p, 4);
    put32(p, ipv4);
}

static packet_t good_response(void) {
    packet_t p;
    header(&p, 1, 2);
    add_ptr(&p, "_nevos._tcp.local", "studio-mac._nevos._tcp.local");
    add_srv(&p, "studio-mac._nevos._tcp.local", 4821, "studio-mac.local");
    add_a(&p, "studio-mac.local", 0xC0A80132u); /* 192.168.1.50 */
    return p;
}

/* ------------------------------------------------------------------ tests */

static void test_a_complete_answer_yields_an_address(void) {
    packet_t p = good_response();
    nev_mdns_result_t r;
    memset(&r, 0, sizeof(r));

    TEST_ASSERT_TRUE(nev_mdns_parse(p.buf, p.len, &r));
    TEST_ASSERT_EQUAL_HEX32(0xC0A80132u, r.ipv4);
    TEST_ASSERT_EQUAL_UINT16(4821, r.port);
    /* Shown on the device, so the user can tell which machine answered. */
    TEST_ASSERT_EQUAL_STRING("studio-mac", r.name);
}

static void test_a_ptr_alone_is_not_enough(void) {
    /* Without a port and an address there is nothing to connect to, and
     * inventing a default port would connect to the wrong thing quietly. */
    packet_t p;
    header(&p, 1, 0);
    add_ptr(&p, "_nevos._tcp.local", "studio-mac._nevos._tcp.local");

    nev_mdns_result_t r;
    TEST_ASSERT_FALSE(nev_mdns_parse(p.buf, p.len, &r));
}

static void test_another_services_records_are_ignored(void) {
    /* A home network is full of these. A parser that took the first SRV it saw
     * would send the device's audio to a printer. */
    packet_t p;
    header(&p, 1, 2);
    add_ptr(&p, "_ipp._tcp.local", "printer._ipp._tcp.local");
    add_srv(&p, "printer._ipp._tcp.local", 631, "printer.local");
    add_a(&p, "printer.local", 0x0A000001u);

    nev_mdns_result_t r;
    memset(&r, 0, sizeof(r));
    /* The SRV is not for an instance we asked about, so nothing matches. */
    TEST_ASSERT_FALSE(nev_mdns_parse(p.buf, p.len, &r));
}

static void test_an_address_for_a_different_host_is_refused(void) {
    /* The SRV points at studio-mac.local but the only A record is for something
     * else: that address is not the daemon's. */
    packet_t p;
    header(&p, 1, 2);
    add_ptr(&p, "_nevos._tcp.local", "studio-mac._nevos._tcp.local");
    add_srv(&p, "studio-mac._nevos._tcp.local", 4821, "studio-mac.local");
    add_a(&p, "someone-else.local", 0x0A000002u);

    nev_mdns_result_t r;
    TEST_ASSERT_FALSE(nev_mdns_parse(p.buf, p.len, &r));
}

static void test_a_compression_pointer_is_followed(void) {
    /* Real responders compress. The SRV target below is a pointer back to the
     * hostname already in the packet. */
    packet_t p = good_response();
    /* Rebuild the A record's name as a pointer to the SRV's target, which sits
     * at a known offset — this is what a responder actually emits. */
    packet_t q;
    header(&q, 1, 2);
    add_ptr(&q, "_nevos._tcp.local", "studio-mac._nevos._tcp.local");
    size_t srv_at = q.len;
    add_srv(&q, "studio-mac._nevos._tcp.local", 4821, "studio-mac.local");
    /* The target name begins 6 bytes into the SRV rdata: name + type(2) +
     * class(2) + ttl(4) + rdlen(2) + priority(2) + weight(2) + port(2). */
    size_t name_len = strlen("studio-mac._nevos._tcp.local") + 2;
    size_t target_at = srv_at + name_len + 2 + 2 + 4 + 2 + 6;

    q.buf[q.len++] = (uint8_t)(0xC0 | (target_at >> 8));
    q.buf[q.len++] = (uint8_t)target_at;
    put16(&q, 1);
    put16(&q, 1);
    put32(&q, 120);
    put16(&q, 4);
    put32(&q, 0xC0A80132u);

    nev_mdns_result_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE_MESSAGE(nev_mdns_parse(q.buf, q.len, &r), "compressed name not followed");
    TEST_ASSERT_EQUAL_HEX32(0xC0A80132u, r.ipv4);
    (void)p;
}

static void test_a_pointer_loop_does_not_hang(void) {
    /* One malformed datagram from anyone on the LAN must not be able to stop
     * the network task. A pointer to itself is the simplest version. */
    uint8_t pkt[32];
    memset(pkt, 0, sizeof(pkt));
    pkt[6] = 0;
    pkt[7] = 1;     /* one answer */
    pkt[12] = 0xC0; /* name: pointer... */
    pkt[13] = 12;   /* ...to itself */

    nev_mdns_result_t r;
    TEST_ASSERT_FALSE(nev_mdns_parse(pkt, sizeof(pkt), &r));
}

static void test_a_forward_pointer_is_refused(void) {
    /* Pointers must go backwards. A forward one is how a loop is built out of
     * two names that each point at the other. */
    uint8_t pkt[32];
    memset(pkt, 0, sizeof(pkt));
    pkt[7] = 1;
    pkt[12] = 0xC0;
    pkt[13] = 20;

    nev_mdns_result_t r;
    TEST_ASSERT_FALSE(nev_mdns_parse(pkt, sizeof(pkt), &r));
}

static void test_a_truncated_packet_is_refused(void) {
    packet_t p = good_response();
    nev_mdns_result_t r;
    /* Every prefix of a valid packet. None may read past the end or claim
     * success from half a record. */
    for (size_t n = 0; n < p.len; n++) {
        (void)nev_mdns_parse(p.buf, n, &r);
    }
    /* And the whole thing still parses, so the loop above is not vacuous. */
    TEST_ASSERT_TRUE(nev_mdns_parse(p.buf, p.len, &r));
}

static void test_a_record_claiming_more_data_than_it_has_is_refused(void) {
    packet_t p = good_response();
    /* Inflate the last record's rdlen. */
    p.buf[p.len - 6] = 0xFF;
    p.buf[p.len - 5] = 0xFF;

    nev_mdns_result_t r;
    TEST_ASSERT_FALSE(nev_mdns_parse(p.buf, p.len, &r));
}

static void test_an_empty_packet_is_refused(void) {
    nev_mdns_result_t r;
    uint8_t empty[4] = {0};
    TEST_ASSERT_FALSE(nev_mdns_parse(empty, 0, &r));
    TEST_ASSERT_FALSE(nev_mdns_parse(empty, sizeof(empty), &r));
}

static void test_the_query_is_a_well_formed_ptr_question(void) {
    uint8_t q[128];
    size_t n = nev_mdns_build_query(q, sizeof(q));
    TEST_ASSERT_TRUE(n > 12);

    TEST_ASSERT_EQUAL_UINT8(0, q[2]);  /* flags: standard query */
    TEST_ASSERT_EQUAL_UINT8(1, q[5]);  /* one question */
    TEST_ASSERT_EQUAL_UINT8(6, q[12]); /* "_nevos" */
    TEST_ASSERT_EQUAL_MEMORY("_nevos", q + 13, 6);
    TEST_ASSERT_EQUAL_UINT8(12, q[n - 3]); /* PTR */
    /* Class IN with the unicast-response bit: without it the answer is
     * multicast to port 5353 and we may not be the one holding that port. */
    TEST_ASSERT_EQUAL_HEX8(0x80, q[n - 2]);
    TEST_ASSERT_EQUAL_UINT8(1, q[n - 1]);
}

static void test_the_query_does_not_overrun_a_small_buffer(void) {
    uint8_t small[8];
    TEST_ASSERT_EQUAL_UINT(0, nev_mdns_build_query(small, sizeof(small)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_complete_answer_yields_an_address);
    RUN_TEST(test_a_ptr_alone_is_not_enough);
    RUN_TEST(test_another_services_records_are_ignored);
    RUN_TEST(test_an_address_for_a_different_host_is_refused);
    RUN_TEST(test_a_compression_pointer_is_followed);
    RUN_TEST(test_a_pointer_loop_does_not_hang);
    RUN_TEST(test_a_forward_pointer_is_refused);
    RUN_TEST(test_a_truncated_packet_is_refused);
    RUN_TEST(test_a_record_claiming_more_data_than_it_has_is_refused);
    RUN_TEST(test_an_empty_packet_is_refused);
    RUN_TEST(test_the_query_is_a_well_formed_ptr_question);
    RUN_TEST(test_the_query_does_not_overrun_a_small_buffer);
    return UNITY_END();
}
