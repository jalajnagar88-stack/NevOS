/* The host allocator's simulated ESP32-S3 budgets. */
#include "nev_port/nev_mem.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_alloc_and_free_balance_the_budget(void) {
    size_t before = nev_mem_used_bytes(NEV_MEM_PSRAM);
    void  *p = nev_malloc(4096, NEV_MEM_PSRAM);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(before + 4096, nev_mem_used_bytes(NEV_MEM_PSRAM));
    nev_free(p);
    TEST_ASSERT_EQUAL_UINT32(before, nev_mem_used_bytes(NEV_MEM_PSRAM));
}

static void test_internal_and_psram_are_separate_budgets(void) {
    size_t psram_before = nev_mem_used_bytes(NEV_MEM_PSRAM);
    void  *p = nev_malloc(1024, NEV_MEM_INTERNAL);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(psram_before, nev_mem_used_bytes(NEV_MEM_PSRAM));
    nev_free(p);
}

/* The whole point of the accountant: exhaustion is reproducible on a laptop. */
static void test_oversized_request_is_refused_not_satisfied(void) {
    void *p = nev_malloc(NEV_SIM_PSRAM_BYTES + 1, NEV_MEM_PSRAM);
    TEST_ASSERT_NULL(p);
    void *q = nev_malloc(NEV_SIM_INTERNAL_BYTES + 1, NEV_MEM_INTERNAL);
    TEST_ASSERT_NULL(q);
}

static void test_calloc_zeroes(void) {
    uint8_t *p = nev_calloc(64, 4, NEV_MEM_PSRAM);
    TEST_ASSERT_NOT_NULL(p);
    for (int i = 0; i < 256; i++) TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    nev_free(p);
}

static void test_free_null_is_a_noop(void) {
    size_t before = nev_mem_used_bytes(NEV_MEM_PSRAM);
    nev_free(NULL);
    TEST_ASSERT_EQUAL_UINT32(before, nev_mem_used_bytes(NEV_MEM_PSRAM));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alloc_and_free_balance_the_budget);
    RUN_TEST(test_internal_and_psram_are_separate_budgets);
    RUN_TEST(test_oversized_request_is_refused_not_satisfied);
    RUN_TEST(test_calloc_zeroes);
    RUN_TEST(test_free_null_is_a_noop);
    return UNITY_END();
}
