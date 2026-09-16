/*
 * The Wi-Fi state machine.
 *
 * Every case here is one you cannot produce on demand with a real router: an
 * access point that associates and never hands out an address, a password
 * changed while the device slept, a router rebooting underneath it.
 */
#include "nev_services/net_core.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define DHCP_MS 8000u
#define MIN_MS  1000u
#define MAX_MS  30000u

static nev_net_core_t make(void) {
    const nev_net_cfg_t cfg = {
        .backoff_min_ms = MIN_MS, .backoff_max_ms = MAX_MS, .dhcp_timeout_ms = DHCP_MS};
    nev_net_core_t c;
    nev_net_core_init(&c, &cfg, 0);
    return c;
}

/* Drives a successful connection, returning the time it finished. */
static uint32_t connect_ok(nev_net_core_t *c, uint32_t at) {
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_CONNECT, nev_net_core_credentials(c, true, at));
    nev_net_core_associated(c, at + 100);
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_UP, nev_net_core_got_address(c, at + 200));
    return at + 200;
}

static void test_no_credentials_means_no_radio(void) {
    /* A device with no Wi-Fi configured should not be scanning all day. */
    nev_net_core_t c = make();
    TEST_ASSERT_EQUAL_INT(NEV_NET_IDLE, nev_net_core_state(&c));
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_tick(&c, 60000));
}

static void test_a_connection_reports_up_only_when_addressed(void) {
    nev_net_core_t c = make();
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_CONNECT, nev_net_core_credentials(&c, true, 0));
    /* Associated is not online: nothing may be published yet. */
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_associated(&c, 100));
    TEST_ASSERT_EQUAL_INT(NEV_NET_WAITING_IP, nev_net_core_state(&c));
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_UP, nev_net_core_got_address(&c, 200));
    TEST_ASSERT_EQUAL_INT(NEV_NET_ONLINE, nev_net_core_state(&c));
}

static void test_an_access_point_that_never_gives_an_address_is_a_failure(void) {
    /* A captive portal. The device would otherwise sit "connected" forever,
     * with a bridge patiently retrying a daemon it can never reach. */
    nev_net_core_t c = make();
    nev_net_core_credentials(&c, true, 0);
    nev_net_core_associated(&c, 100);

    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_tick(&c, 100 + DHCP_MS - 1));
    nev_net_core_tick(&c, 100 + DHCP_MS);
    TEST_ASSERT_EQUAL_INT(NEV_NET_BACKOFF, nev_net_core_state(&c));
}

static void test_a_lost_link_is_published_once(void) {
    nev_net_core_t c = make();
    uint32_t t = connect_ok(&c, 0);

    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_DOWN, nev_net_core_lost(&c, t));
    /* The retry that also fails must not publish DOWN again: the status bar
     * would flicker through every attempt. */
    nev_net_core_tick(&c, t + MIN_MS);
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_lost(&c, t + MIN_MS + 10));
}

static void test_the_backoff_doubles_and_stops_at_the_cap(void) {
    nev_net_core_t c = make();
    nev_net_core_credentials(&c, true, 0);

    uint32_t t = 0;
    uint32_t expected = MIN_MS;
    for (int i = 0; i < 10; i++) {
        nev_net_core_lost(&c, t);
        /* Too early. */
        TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_tick(&c, t + expected - 1));
        TEST_ASSERT_EQUAL_INT(NEV_NET_DO_CONNECT, nev_net_core_tick(&c, t + expected));
        t += expected;
        expected = expected * 2 > MAX_MS ? MAX_MS : expected * 2;
    }
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, c.backoff_ms);
}

static void test_a_success_clears_the_backoff(void) {
    /* Otherwise a router that rebooted at lunchtime leaves the device on a
     * thirty-second retry for the rest of the day. */
    nev_net_core_t c = make();
    nev_net_core_credentials(&c, true, 0);
    for (int i = 0; i < 5; i++) {
        nev_net_core_lost(&c, (uint32_t)(i * 60000));
        nev_net_core_tick(&c, (uint32_t)(i * 60000) + MAX_MS);
    }
    TEST_ASSERT_TRUE(c.backoff_ms > MIN_MS);

    nev_net_core_associated(&c, 400000);
    nev_net_core_got_address(&c, 400100);
    TEST_ASSERT_EQUAL_UINT32(0, c.backoff_ms);
    TEST_ASSERT_EQUAL_UINT32(0, c.attempts);
}

static void test_new_credentials_do_not_wait_out_the_old_backoff(void) {
    /* The user has just retyped the password. Making them wait thirty seconds
     * because the old one failed is the device sulking. */
    nev_net_core_t c = make();
    nev_net_core_credentials(&c, true, 0);
    for (int i = 0; i < 6; i++) {
        nev_net_core_lost(&c, (uint32_t)(i * 60000));
        nev_net_core_tick(&c, (uint32_t)(i * 60000) + MAX_MS);
    }

    nev_net_core_credentials(&c, false, 500000);
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_CONNECT, nev_net_core_credentials(&c, true, 500001));
    TEST_ASSERT_EQUAL_UINT32(0, c.backoff_ms);
}

static void test_clearing_credentials_takes_the_link_down(void) {
    nev_net_core_t c = make();
    connect_ok(&c, 0);
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_DOWN, nev_net_core_credentials(&c, false, 1000));
    TEST_ASSERT_EQUAL_INT(NEV_NET_IDLE, nev_net_core_state(&c));
    TEST_ASSERT_EQUAL_INT(NEV_NET_DO_NOTHING, nev_net_core_tick(&c, 100000));
}

static void test_a_driver_that_says_nothing_at_all_still_retries(void) {
    /* Association that neither succeeds nor fails. Without this the device is
     * stuck in CONNECTING until someone reboots it. */
    nev_net_core_t c = make();
    nev_net_core_credentials(&c, true, 0);
    nev_net_core_tick(&c, DHCP_MS * 2);
    TEST_ASSERT_EQUAL_INT(NEV_NET_BACKOFF, nev_net_core_state(&c));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_credentials_means_no_radio);
    RUN_TEST(test_a_connection_reports_up_only_when_addressed);
    RUN_TEST(test_an_access_point_that_never_gives_an_address_is_a_failure);
    RUN_TEST(test_a_lost_link_is_published_once);
    RUN_TEST(test_the_backoff_doubles_and_stops_at_the_cap);
    RUN_TEST(test_a_success_clears_the_backoff);
    RUN_TEST(test_new_credentials_do_not_wait_out_the_old_backoff);
    RUN_TEST(test_clearing_credentials_takes_the_link_down);
    RUN_TEST(test_a_driver_that_says_nothing_at_all_still_retries);
    return UNITY_END();
}
