/* The event vocabulary: encoding, naming, and the uniqueness the X-macro must guarantee. */
#include "nev_kernel/nev_event.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
}
void tearDown(void) {
}

static const uint16_t kAllTypes[] = {
#define NEV_X(domain, name, code) NEV_EVT_##domain##_##name,
    NEV_EVENT_LIST(NEV_X)
#undef NEV_X
};

static void test_type_encoding_round_trips(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0201, NEV_EVT_INPUT_TOUCH);
    TEST_ASSERT_EQUAL_UINT8(NEV_DOM_ID_INPUT, NEV_TYPE_DOMAIN_ID(NEV_EVT_INPUT_TOUCH));
    TEST_ASSERT_EQUAL_UINT8(0x01, NEV_TYPE_CODE(NEV_EVT_INPUT_TOUCH));
    TEST_ASSERT_EQUAL_UINT32(NEV_DOM(INPUT), NEV_TYPE_DOMAIN_MASK(NEV_EVT_INPUT_TOUCH));
}

static void test_domain_masks_are_distinct_bits(void) {
    uint32_t seen = 0;
#define NEV_X(name, value)                                                                         \
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, seen &NEV_DOM(name), "duplicate domain bit: " #name);      \
    seen |= NEV_DOM(name);
    NEV_DOMAIN_LIST(NEV_X)
#undef NEV_X
    TEST_ASSERT_TRUE(seen != 0);
}

/* A duplicated (domain, code) pair would silently alias two events. */
static void test_every_event_type_is_unique(void) {
    const size_t n = sizeof(kAllTypes) / sizeof(kAllTypes[0]);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, kAllTypes[i], "event type 0 collides with NEV_EVT_NONE");
        for (size_t j = i + 1; j < n; j++) {
            if (kAllTypes[i] == kAllTypes[j]) {
                char msg[64];
                snprintf(msg, sizeof(msg), "duplicate type 0x%04X (%s)", kAllTypes[i],
                         nev_evt_name(kAllTypes[i]));
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_names_are_present_for_every_type(void) {
    for (size_t i = 0; i < sizeof(kAllTypes) / sizeof(kAllTypes[0]); i++) {
        const char *name = nev_evt_name(kAllTypes[i]);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE_MESSAGE(strchr(name, '.') != NULL, "name should be DOMAIN.EVENT");
        TEST_ASSERT_TRUE_MESSAGE(strstr(name, "0x") == NULL, "known type rendered as hex");
    }
    TEST_ASSERT_EQUAL_STRING("INPUT.TOUCH", nev_evt_name(NEV_EVT_INPUT_TOUCH));
}

/* A trace from newer firmware must stay readable by an older tool. */
static void test_unknown_type_renders_rather_than_returning_null(void) {
    const char *name = nev_evt_name(NEV_TYPE(INPUT, 0xFE));
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("INPUT.0xFE", name);
}

static void test_event_layout_is_frozen(void) {
    TEST_ASSERT_EQUAL_UINT32(32, (uint32_t)sizeof(nev_event_t));
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)sizeof(nev_payload_t));

    nev_event_t ev;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)((char *)&ev.type - (char *)&ev));
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)((char *)&ev.seq - (char *)&ev));
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)((char *)&ev.ts_us - (char *)&ev));
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)((char *)&ev.p - (char *)&ev));
}

static void test_event_make_leaves_bus_fields_zero(void) {
    nev_event_t ev = nev_event_make(NEV_EVT_SYS_TICK_1S, NEV_SRC_KERNEL);
    TEST_ASSERT_EQUAL_HEX16(NEV_EVT_SYS_TICK_1S, ev.type);
    TEST_ASSERT_EQUAL_UINT8(NEV_SRC_KERNEL, ev.source);
    TEST_ASSERT_EQUAL_UINT32(0, ev.seq);
    TEST_ASSERT_EQUAL_UINT64(0, ev.ts_us);
    TEST_ASSERT_EQUAL_UINT8(0, ev.flags);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_type_encoding_round_trips);
    RUN_TEST(test_domain_masks_are_distinct_bits);
    RUN_TEST(test_every_event_type_is_unique);
    RUN_TEST(test_names_are_present_for_every_type);
    RUN_TEST(test_unknown_type_renders_rather_than_returning_null);
    RUN_TEST(test_event_layout_is_frozen);
    RUN_TEST(test_event_make_leaves_bus_fields_zero);
    return UNITY_END();
}
