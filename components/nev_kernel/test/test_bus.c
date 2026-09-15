/*
 * The event bus contract. Every property docs/event-bus.md promises should
 * either be asserted here or be visibly absent with a reason.
 */
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_atomic.h"
#include "nev_port/nev_task.h"
#include "nev_port/nev_time.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_bus_init());
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_blob_pool_init());
}

void tearDown(void) {
    nev_bus_deinit();
    /* Any blob still held here was leaked by the bus or by a handler. */
    TEST_ASSERT_TRUE_MESSAGE(nev_blob_all_free(), "blob leaked across teardown");
    nev_blob_pool_deinit();
}

static nev_sub_t *sub_named(const char *name, uint32_t domains, uint8_t depth,
                            nev_full_policy_t policy, bool coalesce) {
    nev_sub_cfg_t cfg = {
        .name = name, .domains = domains, .depth = depth, .full_policy = policy,
        .coalesce = coalesce};
    nev_sub_t *s = nev_bus_subscribe(&cfg);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

static void publish_touch(int16_t x, int16_t y) {
    nev_event_t ev = nev_event_make(NEV_EVT_INPUT_TOUCH, NEV_SRC_INPUT);
    ev.p.touch.x = x;
    ev.p.touch.y = y;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_bus_publish(&ev));
}

/* ------------------------------------------------------------ basic delivery */

static void test_publish_then_receive_round_trips_the_payload(void) {
    nev_sub_t  *s = sub_named("ui", NEV_DOM(INPUT), 8, NEV_FULL_DROP_NEWEST, false);
    publish_touch(120, 240);

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
    TEST_ASSERT_EQUAL_HEX16(NEV_EVT_INPUT_TOUCH, got.type);
    TEST_ASSERT_EQUAL_INT16(120, got.p.touch.x);
    TEST_ASSERT_EQUAL_INT16(240, got.p.touch.y);
    TEST_ASSERT_EQUAL_UINT8(NEV_SRC_INPUT, got.source);
}

/* The bus, not the publisher, stamps identity — that is what makes traces replayable. */
static void test_bus_stamps_sequence_and_timestamp(void) {
    nev_sub_t *s = sub_named("ui", NEV_DOM(INPUT), 8, NEV_FULL_DROP_NEWEST, false);

    nev_event_t a, b;
    publish_touch(1, 1);
    publish_touch(2, 2);
    TEST_ASSERT_TRUE(nev_bus_recv(s, &a, NEV_NO_WAIT));
    TEST_ASSERT_TRUE(nev_bus_recv(s, &b, NEV_NO_WAIT));

    TEST_ASSERT_GREATER_THAN_UINT32(0, a.seq);
    TEST_ASSERT_EQUAL_UINT32(a.seq + 1, b.seq);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(a.ts_us, b.ts_us);
}

static void test_recv_on_empty_queue_times_out_without_blocking_forever(void) {
    nev_sub_t  *s = sub_named("ui", NEV_DOM(INPUT), 4, NEV_FULL_DROP_NEWEST, false);
    nev_event_t got;

    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, NEV_NO_WAIT));

    uint64_t t0 = nev_now_us();
    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, 20));
    uint64_t elapsed = nev_now_us() - t0;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(15000, elapsed); /* waited roughly as asked */
    TEST_ASSERT_LESS_THAN_UINT64(500000, elapsed);       /* and did not hang */
}

/* --------------------------------------------------------------- filtering */

static void test_subscriber_only_receives_its_domains(void) {
    nev_sub_t *input_only = sub_named("in", NEV_DOM(INPUT), 8, NEV_FULL_DROP_NEWEST, false);
    nev_sub_t *power_only = sub_named("pwr", NEV_DOM(POWER), 8, NEV_FULL_DROP_NEWEST, false);

    publish_touch(5, 5);

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(input_only, &got, NEV_NO_WAIT));
    TEST_ASSERT_FALSE(nev_bus_recv(power_only, &got, NEV_NO_WAIT));
}

static void test_one_event_fans_out_to_every_matching_subscriber(void) {
    nev_sub_t *a = sub_named("a", NEV_DOM(INPUT), 4, NEV_FULL_DROP_NEWEST, false);
    nev_sub_t *b = sub_named("b", NEV_DOM(INPUT) | NEV_DOM(POWER), 4, NEV_FULL_DROP_NEWEST, false);
    nev_sub_t *c = sub_named("c", NEV_DOM_ALL, 4, NEV_FULL_DROP_NEWEST, false);

    publish_touch(7, 7);

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(a, &got, NEV_NO_WAIT));
    TEST_ASSERT_TRUE(nev_bus_recv(b, &got, NEV_NO_WAIT));
    TEST_ASSERT_TRUE(nev_bus_recv(c, &got, NEV_NO_WAIT));

    nev_bus_stats_t st;
    nev_bus_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1, st.published);
    TEST_ASSERT_EQUAL_UINT32(3, st.delivered);
}

/* FIFO per publisher-subscriber pair is the ordering guarantee handlers rely on. */
static void test_delivery_is_fifo_for_a_single_publisher(void) {
    nev_sub_t *s = sub_named("ui", NEV_DOM(INPUT), 16, NEV_FULL_DROP_NEWEST, false);
    for (int16_t i = 0; i < 10; i++) publish_touch(i, 0);

    nev_event_t got;
    for (int16_t i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
        TEST_ASSERT_EQUAL_INT16(i, got.p.touch.x);
    }
}

/* ---------------------------------------------------------- overflow policy */

static void test_drop_newest_keeps_the_oldest_events(void) {
    nev_sub_t *s = sub_named("slow", NEV_DOM(INPUT), 4, NEV_FULL_DROP_NEWEST, false);
    for (int16_t i = 0; i < 4; i++) publish_touch(i, 0);

    /* Publishing into a full ring reports the drop but does not fail the caller. */
    nev_event_t ev = nev_event_make(NEV_EVT_INPUT_TOUCH, NEV_SRC_INPUT);
    ev.p.touch.x = 99;
    TEST_ASSERT_EQUAL_INT(NEV_ERR_DROPPED, nev_bus_publish(&ev));

    nev_event_t got;
    for (int16_t i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
        TEST_ASSERT_EQUAL_INT16(i, got.p.touch.x); /* 99 never arrived */
    }
    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, NEV_NO_WAIT));

    nev_sub_stats_t ss;
    nev_sub_stats(s, &ss);
    TEST_ASSERT_EQUAL_UINT32(4, ss.received);
    TEST_ASSERT_EQUAL_UINT32(1, ss.dropped);
    TEST_ASSERT_EQUAL_UINT8(4, ss.high_water);
}

static void test_drop_oldest_keeps_the_latest_events(void) {
    nev_sub_t *s = sub_named("state", NEV_DOM(INPUT), 4, NEV_FULL_DROP_OLDEST, false);
    for (int16_t i = 0; i < 7; i++) publish_touch(i, 0);

    /* Three evicted; the ring should hold 3,4,5,6. */
    nev_event_t got;
    for (int16_t i = 3; i < 7; i++) {
        TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
        TEST_ASSERT_EQUAL_INT16(i, got.p.touch.x);
    }
    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, NEV_NO_WAIT));

    nev_sub_stats_t ss;
    nev_sub_stats(s, &ss);
    TEST_ASSERT_EQUAL_UINT32(3, ss.dropped);
}

/* The 100 Hz IMU feeding a 30 Hz consumer: latest wins, queue does not grow. */
static void test_coalescing_replaces_rather_than_queues(void) {
    nev_sub_t *s = sub_named("ui", NEV_DOM(INPUT), 8, NEV_FULL_DROP_NEWEST, true);

    for (int16_t i = 0; i < 20; i++) publish_touch(i, 0);

    nev_sub_stats_t ss;
    nev_sub_stats(s, &ss);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, ss.queued, "coalescing should keep exactly one");
    TEST_ASSERT_EQUAL_UINT32(19, ss.coalesced);
    TEST_ASSERT_EQUAL_UINT32(0, ss.dropped);

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
    TEST_ASSERT_EQUAL_INT16(19, got.p.touch.x); /* newest value survived */
    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, NEV_NO_WAIT));
}

static void test_coalescing_does_not_merge_different_types(void) {
    nev_sub_t *s = sub_named("ui", NEV_DOM(INPUT), 8, NEV_FULL_DROP_NEWEST, true);
    publish_touch(1, 1);
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_bus_publish_type(NEV_EVT_INPUT_BUTTON_DOWN, NEV_SRC_INPUT));
    publish_touch(2, 2);

    nev_sub_stats_t ss;
    nev_sub_stats(s, &ss);
    TEST_ASSERT_EQUAL_UINT8(2, ss.queued);
}

/* ------------------------------------------------------- blob ownership */

static nev_blob_t make_blob(const char *text) {
    uint8_t   *buf = NULL;
    nev_blob_t h = nev_blob_alloc(strlen(text) + 1, &buf);
    TEST_ASSERT_NOT_EQUAL(NEV_BLOB_NONE, h);
    memcpy(buf, text, strlen(text) + 1);
    return h;
}

static nev_err_t publish_blob(nev_blob_t h) {
    nev_event_t ev = nev_event_make(NEV_EVT_BRIDGE_AGENT_TOKEN, NEV_SRC_BRIDGE);
    ev.flags = NEV_EVF_BLOB;
    ev.p.blob.handle = h;
    ev.p.blob.len = (uint32_t)nev_blob_len(h);
    return nev_bus_publish(&ev);
}

static void test_bus_retains_one_reference_per_accepted_delivery(void) {
    nev_sub_t *a = sub_named("a", NEV_DOM(BRIDGE), 4, NEV_FULL_DROP_NEWEST, false);
    nev_sub_t *b = sub_named("b", NEV_DOM(BRIDGE), 4, NEV_FULL_DROP_NEWEST, false);

    nev_blob_t h = make_blob("hello");
    TEST_ASSERT_EQUAL_UINT32(1, nev_blob_refcount(h));

    TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(h));
    TEST_ASSERT_EQUAL_UINT32(3, nev_blob_refcount(h)); /* publisher + two subscribers */

    nev_blob_release(h); /* publisher gives up its reference */
    TEST_ASSERT_EQUAL_UINT32(2, nev_blob_refcount(h));

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(a, &got, NEV_NO_WAIT));
    TEST_ASSERT_EQUAL_STRING("hello", (const char *)nev_blob_data(got.p.blob.handle));
    nev_blob_release(got.p.blob.handle);
    TEST_ASSERT_EQUAL_UINT32(1, nev_blob_refcount(h));

    TEST_ASSERT_TRUE(nev_bus_recv(b, &got, NEV_NO_WAIT));
    nev_blob_release(got.p.blob.handle);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

static void test_blob_is_recycled_when_no_subscriber_matched(void) {
    nev_blob_t h = make_blob("nobody wants this");
    TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(h));
    TEST_ASSERT_EQUAL_UINT32(1, nev_blob_refcount(h));
    nev_blob_release(h);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

/* An evicted event's reference must die with it, or DROP_OLDEST leaks the pool. */
static void test_eviction_releases_the_evicted_events_blob(void) {
    nev_sub_t *s = sub_named("state", NEV_DOM(BRIDGE), 2, NEV_FULL_DROP_OLDEST, false);

    /*
     * The test holds its own reference to `first`. Without it the handle would
     * be recycled by a later alloc the moment the bus released it, and the
     * refcount check below would be reading a different blob.
     */
    nev_blob_t first = make_blob("one");
    nev_blob_retain(first);
    TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(first));
    nev_blob_release(first); /* publisher's own reference */
    TEST_ASSERT_EQUAL_UINT32(2, nev_blob_refcount(first)); /* test + queued event */

    for (int i = 0; i < 3; i++) {
        nev_blob_t h = make_blob("more");
        TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(h));
        nev_blob_release(h);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, nev_blob_refcount(first),
                                     "bus kept a reference to an evicted event");

    /* Four blobs published into a depth-2 ring: only the last two are still held. */
    nev_blob_stats_t st;
    nev_blob_stats(&st);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, st.in_use[0], "queued 2 + the one this test holds");

    nev_blob_release(first);
    nev_event_t got;
    while (nev_bus_recv(s, &got, NEV_NO_WAIT)) nev_blob_release(got.p.blob.handle);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

static void test_flush_releases_queued_blobs(void) {
    nev_sub_t *s = sub_named("a", NEV_DOM(BRIDGE), 8, NEV_FULL_DROP_NEWEST, false);
    for (int i = 0; i < 5; i++) {
        nev_blob_t h = make_blob("x");
        TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(h));
        nev_blob_release(h);
    }
    TEST_ASSERT_FALSE(nev_blob_all_free());

    nev_bus_flush(s);
    TEST_ASSERT_TRUE(nev_blob_all_free());

    nev_event_t got;
    TEST_ASSERT_FALSE(nev_bus_recv(s, &got, NEV_NO_WAIT));
}

static void test_coalescing_releases_the_replaced_events_blob(void) {
    nev_sub_t *s = sub_named("a", NEV_DOM(BRIDGE), 4, NEV_FULL_DROP_NEWEST, true);

    nev_blob_t first = make_blob("stale");
    TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(first));
    nev_blob_release(first);
    TEST_ASSERT_EQUAL_UINT32(1, nev_blob_refcount(first));

    nev_blob_t second = make_blob("fresh");
    TEST_ASSERT_EQUAL_INT(NEV_OK, publish_blob(second));
    nev_blob_release(second);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nev_blob_refcount(first), "replaced blob not released");

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(s, &got, NEV_NO_WAIT));
    nev_blob_release(got.p.blob.handle);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

/* ----------------------------------------------------- overflow reporting */

static void test_dropping_subscriber_is_reported_on_the_bus(void) {
    nev_sub_t *victim = sub_named("victim", NEV_DOM(INPUT), 2, NEV_FULL_DROP_NEWEST, false);
    nev_sub_t *watcher = sub_named("sys", NEV_DOM(SYS), 8, NEV_FULL_DROP_NEWEST, false);

    for (int16_t i = 0; i < 6; i++) {
        nev_event_t ev = nev_event_make(NEV_EVT_INPUT_TOUCH, NEV_SRC_INPUT);
        ev.p.touch.x = i;
        (void)nev_bus_publish(&ev); /* drops are expected here */
    }

    nev_bus_report_overflows();

    nev_event_t got;
    TEST_ASSERT_TRUE(nev_bus_recv(watcher, &got, NEV_NO_WAIT));
    TEST_ASSERT_EQUAL_HEX16(NEV_EVT_SYS_BUS_OVERFLOW, got.type);
    TEST_ASSERT_EQUAL_UINT32(4, got.p.overflow.dropped_total);
    TEST_ASSERT_EQUAL_HEX16(NEV_EVT_INPUT_TOUCH, got.p.overflow.event_type);

    /* Reported once per new drop, not once per sweep. */
    nev_bus_report_overflows();
    TEST_ASSERT_FALSE(nev_bus_recv(watcher, &got, NEV_NO_WAIT));
    nev_bus_flush(victim);
}

/* -------------------------------------------------- subscription validation */

static void test_invalid_subscriptions_are_refused(void) {
    nev_sub_cfg_t bad_depth = {.name = "x", .domains = NEV_DOM(SYS), .depth = 0};
    TEST_ASSERT_NULL(nev_bus_subscribe(&bad_depth));

    nev_sub_cfg_t too_deep = {
        .name = "x", .domains = NEV_DOM(SYS), .depth = NEV_BUS_MAX_DEPTH + 1};
    TEST_ASSERT_NULL(nev_bus_subscribe(&too_deep));

    nev_sub_cfg_t no_domains = {.name = "x", .domains = NEV_DOM_NONE, .depth = 4};
    TEST_ASSERT_NULL(nev_bus_subscribe(&no_domains));

    TEST_ASSERT_NULL(nev_bus_subscribe(NULL));
}

static void test_slot_exhaustion_is_refused_not_overrun(void) {
    for (int i = 0; i < NEV_BUS_MAX_SUBS; i++) {
        nev_sub_cfg_t cfg = {.name = "s", .domains = NEV_DOM(SYS), .depth = 1};
        TEST_ASSERT_NOT_NULL(nev_bus_subscribe(&cfg));
    }
    nev_sub_cfg_t one_too_many = {.name = "s", .domains = NEV_DOM(SYS), .depth = 1};
    TEST_ASSERT_NULL(nev_bus_subscribe(&one_too_many));
}

static void test_long_subscriber_name_is_truncated_not_overflowed(void) {
    nev_sub_t *s = sub_named("a-very-long-subscriber-name-indeed", NEV_DOM(SYS), 2,
                             NEV_FULL_DROP_NEWEST, false);
    TEST_ASSERT_EQUAL_UINT32(NEV_SUB_NAME_MAX - 1, (uint32_t)strlen(nev_sub_name(s)));
}

/* -------------------------------------------------------------- concurrency */

#define PUBLISHERS       4
#define EVENTS_PER_TASK  500

static nev_atomic_u32_t s_publishers_done;

static void publisher_task(void *arg) {
    (void)arg;
    for (int i = 0; i < EVENTS_PER_TASK; i++) {
        nev_event_t ev = nev_event_make(NEV_EVT_INPUT_TOUCH, NEV_SRC_INPUT);
        ev.p.touch.x = (int16_t)i;
        (void)nev_bus_publish(&ev);
    }
    nev_atomic_inc(&s_publishers_done);
}

/*
 * Concurrent publishers must not corrupt the ring. The invariant that proves it:
 * every event offered to a subscriber was either accepted or counted as dropped,
 * and the two must add up exactly.
 */
static void test_concurrent_publishers_lose_nothing_unaccounted(void) {
    nev_sub_t *s = sub_named("ui", NEV_DOM(INPUT), NEV_BUS_MAX_DEPTH, NEV_FULL_DROP_NEWEST, false);
    nev_atomic_store(&s_publishers_done, 0);

    nev_task_t tasks[PUBLISHERS];
    for (int i = 0; i < PUBLISHERS; i++) {
        nev_task_cfg_t cfg = {.name = "pub", .fn = publisher_task, .stack_bytes = 65536};
        TEST_ASSERT_EQUAL_INT(NEV_OK, nev_task_create(&tasks[i], &cfg));
    }

    uint32_t    drained = 0;
    nev_event_t got;
    while (nev_atomic_load(&s_publishers_done) < PUBLISHERS) {
        while (nev_bus_recv(s, &got, 1)) drained++;
    }
    while (nev_bus_recv(s, &got, NEV_NO_WAIT)) drained++;

    nev_sub_stats_t ss;
    nev_sub_stats(s, &ss);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(PUBLISHERS * EVENTS_PER_TASK, ss.received + ss.dropped,
                                     "events went missing without being counted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ss.received, drained + ss.queued,
                                     "accepted events did not all reach the receiver");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_then_receive_round_trips_the_payload);
    RUN_TEST(test_bus_stamps_sequence_and_timestamp);
    RUN_TEST(test_recv_on_empty_queue_times_out_without_blocking_forever);
    RUN_TEST(test_subscriber_only_receives_its_domains);
    RUN_TEST(test_one_event_fans_out_to_every_matching_subscriber);
    RUN_TEST(test_delivery_is_fifo_for_a_single_publisher);
    RUN_TEST(test_drop_newest_keeps_the_oldest_events);
    RUN_TEST(test_drop_oldest_keeps_the_latest_events);
    RUN_TEST(test_coalescing_replaces_rather_than_queues);
    RUN_TEST(test_coalescing_does_not_merge_different_types);
    RUN_TEST(test_bus_retains_one_reference_per_accepted_delivery);
    RUN_TEST(test_blob_is_recycled_when_no_subscriber_matched);
    RUN_TEST(test_eviction_releases_the_evicted_events_blob);
    RUN_TEST(test_flush_releases_queued_blobs);
    RUN_TEST(test_coalescing_releases_the_replaced_events_blob);
    RUN_TEST(test_dropping_subscriber_is_reported_on_the_bus);
    RUN_TEST(test_invalid_subscriptions_are_refused);
    RUN_TEST(test_slot_exhaustion_is_refused_not_overrun);
    RUN_TEST(test_long_subscriber_name_is_truncated_not_overflowed);
    RUN_TEST(test_concurrent_publishers_lose_nothing_unaccounted);
    return UNITY_END();
}
