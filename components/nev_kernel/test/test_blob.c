/* The blob pool: size classes, the reference-count protocol, and exhaustion. */
#include "nev_kernel/nev_blob.h"
#include "unity.h"
#include <string.h>

void setUp(void) { TEST_ASSERT_EQUAL_INT(NEV_OK, nev_blob_pool_init()); }
void tearDown(void) { nev_blob_pool_deinit(); }

static void test_alloc_gives_a_writable_buffer_with_one_reference(void) {
    uint8_t   *buf = NULL;
    nev_blob_t h = nev_blob_alloc(100, &buf);
    TEST_ASSERT_NOT_EQUAL(NEV_BLOB_NONE, h);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL_UINT32(1, nev_blob_refcount(h));
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)nev_blob_len(h));
    TEST_ASSERT_EQUAL_PTR(buf, nev_blob_data(h));

    memset(buf, 0xAB, 100);
    TEST_ASSERT_EQUAL_UINT8(0xAB, nev_blob_data(h)[99]);
    nev_blob_release(h);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

/* A 600-byte payload must not consume a 64 KB block. */
static void test_smallest_fitting_class_is_chosen(void) {
    nev_blob_stats_t st;

    nev_blob_t small = nev_blob_alloc(NEV_BLOB_SMALL_SIZE, NULL);
    nev_blob_stats(&st);
    TEST_ASSERT_EQUAL_UINT16(1, st.in_use[0]);
    TEST_ASSERT_EQUAL_UINT16(0, st.in_use[1]);

    nev_blob_t medium = nev_blob_alloc(NEV_BLOB_SMALL_SIZE + 1, NULL);
    nev_blob_stats(&st);
    TEST_ASSERT_EQUAL_UINT16(1, st.in_use[1]);
    TEST_ASSERT_EQUAL_UINT16(0, st.in_use[2]);

    nev_blob_t large = nev_blob_alloc(NEV_BLOB_MEDIUM_SIZE + 1, NULL);
    nev_blob_stats(&st);
    TEST_ASSERT_EQUAL_UINT16(1, st.in_use[2]);

    nev_blob_release(small);
    nev_blob_release(medium);
    nev_blob_release(large);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

static void test_buffer_returns_to_the_pool_only_at_zero_references(void) {
    nev_blob_t h = nev_blob_alloc(64, NULL);
    nev_blob_retain(h);
    nev_blob_retain(h);
    TEST_ASSERT_EQUAL_UINT32(3, nev_blob_refcount(h));

    nev_blob_release(h);
    TEST_ASSERT_FALSE(nev_blob_all_free());
    nev_blob_release(h);
    TEST_ASSERT_FALSE(nev_blob_all_free());
    nev_blob_release(h);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

static void test_exhaustion_returns_none_rather_than_blocking(void) {
    nev_blob_t held[NEV_BLOB_SMALL_COUNT];
    for (unsigned i = 0; i < NEV_BLOB_SMALL_COUNT; i++) {
        held[i] = nev_blob_alloc(16, NULL);
        TEST_ASSERT_NOT_EQUAL(NEV_BLOB_NONE, held[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(NEV_BLOB_NONE, nev_blob_alloc(16, NULL));

    nev_blob_stats_t st;
    nev_blob_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1, st.alloc_failures);
    TEST_ASSERT_EQUAL_UINT16(NEV_BLOB_SMALL_COUNT, st.high_water[0]);

    /* Freeing one makes the pool usable again immediately. */
    nev_blob_release(held[0]);
    held[0] = nev_blob_alloc(16, NULL);
    TEST_ASSERT_NOT_EQUAL(NEV_BLOB_NONE, held[0]);

    for (unsigned i = 0; i < NEV_BLOB_SMALL_COUNT; i++) nev_blob_release(held[i]);
    TEST_ASSERT_TRUE(nev_blob_all_free());
}

static void test_requests_beyond_the_largest_class_are_refused(void) {
    TEST_ASSERT_EQUAL_UINT16(NEV_BLOB_NONE, nev_blob_alloc(NEV_BLOB_MAX_LEN + 1, NULL));
    TEST_ASSERT_EQUAL_UINT16(NEV_BLOB_NONE, nev_blob_alloc(0, NULL));
}

static void test_invalid_handles_are_inert(void) {
    TEST_ASSERT_NULL(nev_blob_data(NEV_BLOB_NONE));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)nev_blob_len(NEV_BLOB_NONE));
    TEST_ASSERT_EQUAL_UINT32(0, nev_blob_refcount(NEV_BLOB_NONE));
    nev_blob_retain(NEV_BLOB_NONE); /* must not crash */
    nev_blob_release(NEV_BLOB_NONE);
}

static void test_recycled_handle_does_not_alias_stale_data(void) {
    uint8_t   *a = NULL;
    nev_blob_t h1 = nev_blob_alloc(32, &a);
    memset(a, 0x11, 32);
    nev_blob_release(h1);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)nev_blob_len(h1)); /* length cleared on release */

    uint8_t   *b = NULL;
    nev_blob_t h2 = nev_blob_alloc(8, &b);
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)nev_blob_len(h2));
    nev_blob_release(h2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alloc_gives_a_writable_buffer_with_one_reference);
    RUN_TEST(test_smallest_fitting_class_is_chosen);
    RUN_TEST(test_buffer_returns_to_the_pool_only_at_zero_references);
    RUN_TEST(test_exhaustion_returns_none_rather_than_blocking);
    RUN_TEST(test_requests_beyond_the_largest_class_are_refused);
    RUN_TEST(test_invalid_handles_are_inert);
    RUN_TEST(test_recycled_handle_does_not_alias_stale_data);
    return UNITY_END();
}
