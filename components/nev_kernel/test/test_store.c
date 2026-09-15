/* Settings: the schema, clamping, persistence, write-back and factory reset. */
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

/* Relative to the test binary's working directory, which ctest sets to the
 * build tree — not to the repository root. */
#define STORE_PATH "nevos-test-settings.txt"

void setUp(void) {
    remove(STORE_PATH);
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_bus_init());
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_init(STORE_PATH));
}

void tearDown(void) {
    nev_store_deinit();
    nev_bus_deinit();
    remove(STORE_PATH);
}

static void test_every_setting_has_a_usable_descriptor(void) {
    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        const nev_setting_desc_t *d = nev_store_describe((nev_setting_t)i);
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_NOT_NULL(d->key);
        TEST_ASSERT_TRUE_MESSAGE(strlen(d->key) > 0, "empty key");
        TEST_ASSERT_TRUE_MESSAGE(strlen(d->key) < NEV_STORE_KEY_MAX, "key too long to persist");
        TEST_ASSERT_TRUE_MESSAGE(d->max >= d->min, "inverted range");

        nev_setting_t back;
        TEST_ASSERT_TRUE_MESSAGE(nev_store_key_from_name(d->key, &back), d->key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, back, "duplicate key in the schema");
    }
}

/* A key is what gets persisted; two settings sharing one would silently alias. */
static void test_keys_are_unique(void) {
    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        for (int j = i + 1; j < NEV_SETTING_COUNT; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0,
                                          strcmp(nev_store_describe((nev_setting_t)i)->key,
                                                 nev_store_describe((nev_setting_t)j)->key),
                                          "two settings share a key");
        }
    }
}

static void test_defaults_are_inside_their_declared_range(void) {
    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        const nev_setting_desc_t *d = nev_store_describe((nev_setting_t)i);
        if (d->type == NEV_SETTING_STR) {
            TEST_ASSERT_TRUE_MESSAGE(strlen(d->default_text) <= d->max, d->key);
        } else {
            const uint32_t v = nev_store_num((nev_setting_t)i);
            TEST_ASSERT_TRUE_MESSAGE(v >= d->min && v <= d->max, d->key);
        }
    }
}

static void test_fresh_store_returns_defaults(void) {
    TEST_ASSERT_EQUAL_UINT32(70, nev_store_num(NEV_SET_BRIGHTNESS));
    TEST_ASSERT_EQUAL_UINT32(60, nev_store_num(NEV_SET_VOLUME));
    TEST_ASSERT_EQUAL_STRING("NEVOS", nev_store_str(NEV_SET_DEVICE_NAME));
    TEST_ASSERT_FALSE(nev_store_is_dirty());
}

/* Saturating beats refusing: a setting that silently declines to change is
 * worse than one that goes as far as it can. */
static void test_out_of_range_writes_are_clamped_not_rejected(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_BRIGHTNESS, 900));
    TEST_ASSERT_EQUAL_UINT32(100, nev_store_num(NEV_SET_BRIGHTNESS));

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_BRIGHTNESS, 0));
    TEST_ASSERT_EQUAL_UINT32(5, nev_store_num(NEV_SET_BRIGHTNESS)); /* min is 5 */
}

static void test_long_strings_are_truncated_to_the_declared_length(void) {
    const char *too_long = "a-device-name-considerably-longer-than-the-schema-permits";
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_str(NEV_SET_DEVICE_NAME, too_long));
    TEST_ASSERT_EQUAL_UINT32(24, (uint32_t)strlen(nev_store_str(NEV_SET_DEVICE_NAME)));
}

static void test_writing_the_same_value_does_not_dirty_the_store(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_VOLUME, 60)); /* the default */
    TEST_ASSERT_FALSE_MESSAGE(nev_store_is_dirty(), "no-op write marked the store dirty");

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_VOLUME, 61));
    TEST_ASSERT_TRUE(nev_store_is_dirty());
}

static void test_changes_are_published_on_the_bus(void) {
    const nev_sub_cfg_t cfg = {
        .name = "t", .domains = NEV_DOM(STORAGE), .depth = 8, .full_policy = NEV_FULL_DROP_NEWEST};
    nev_sub_t *sub = nev_bus_subscribe(&cfg);
    TEST_ASSERT_NOT_NULL(sub);

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_BRIGHTNESS, 42));

    nev_event_t ev;
    TEST_ASSERT_TRUE(nev_bus_recv(sub, &ev, NEV_NO_WAIT));
    TEST_ASSERT_EQUAL_HEX16(NEV_EVT_STORAGE_SETTING_CHANGED, ev.type);
    TEST_ASSERT_EQUAL_UINT32(NEV_SET_BRIGHTNESS, ev.p.u32[0]);
    TEST_ASSERT_EQUAL_UINT32(42, ev.p.u32[1]);
}

/* The debounce is the reason a dragged slider is one flash write, not sixty. */
static void test_writeback_waits_for_changes_to_settle(void) {
    uint32_t now = 1000;
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_VOLUME, 33));

    nev_store_tick(now);
    nev_store_tick(now + 100);
    TEST_ASSERT_TRUE_MESSAGE(nev_store_is_dirty(), "wrote back before the value settled");

    nev_store_tick(now + 900);
    TEST_ASSERT_FALSE_MESSAGE(nev_store_is_dirty(), "never wrote back");
}

static void test_values_survive_a_restart(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_BRIGHTNESS, 31));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_str(NEV_SET_DEVICE_NAME, "Desk Robot"));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_commit());
    nev_store_deinit();

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_init(STORE_PATH));
    TEST_ASSERT_EQUAL_UINT32(31, nev_store_num(NEV_SET_BRIGHTNESS));
    TEST_ASSERT_EQUAL_STRING("Desk Robot", nev_store_str(NEV_SET_DEVICE_NAME));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(60, nev_store_num(NEV_SET_VOLUME), "untouched key drifted");
}

/* deinit commits, so closing the device does not lose the last change. */
static void test_deinit_flushes_pending_changes(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_VOLUME, 12));
    nev_store_deinit(); /* no explicit commit */

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_init(STORE_PATH));
    TEST_ASSERT_EQUAL_UINT32(12, nev_store_num(NEV_SET_VOLUME));
}

/* A corrupt settings file must not stop the device booting. */
static void test_a_damaged_file_falls_back_to_defaults(void) {
    nev_store_deinit();
    FILE *f = fopen(STORE_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("this is not a settings file\n\x01\x02garbage\nbrightness=44\n=novalue\n", f);
    fclose(f);

    TEST_ASSERT_EQUAL_INT_MESSAGE(NEV_OK, nev_store_init(STORE_PATH), "refused to start");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(44, nev_store_num(NEV_SET_BRIGHTNESS),
                                     "readable line was not recovered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(60, nev_store_num(NEV_SET_VOLUME), "should be the default");
}

/* Stored values outside the schema's range must be clamped on load, not trusted. */
static void test_out_of_range_stored_values_are_clamped_on_load(void) {
    nev_store_deinit();
    FILE *f = fopen(STORE_PATH, "w");
    fputs("brightness=250\nvolume=9999\n", f);
    fclose(f);

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_init(STORE_PATH));
    TEST_ASSERT_EQUAL_UINT32(100, nev_store_num(NEV_SET_BRIGHTNESS));
    TEST_ASSERT_EQUAL_UINT32(100, nev_store_num(NEV_SET_VOLUME));
}

static void test_factory_reset_restores_every_default(void) {
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_num(NEV_SET_BRIGHTNESS, 10));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_str(NEV_SET_PAIR_TOKEN, "secret-token"));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_commit());

    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_factory_reset());
    TEST_ASSERT_EQUAL_UINT32(70, nev_store_num(NEV_SET_BRIGHTNESS));
    TEST_ASSERT_EQUAL_STRING("", nev_store_str(NEV_SET_PAIR_TOKEN));

    /* And the pairing token must be gone from the backing store, not just RAM. */
    nev_store_deinit();
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_init(STORE_PATH));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", nev_store_str(NEV_SET_PAIR_TOKEN),
                                     "factory reset left the pairing token on disk");
}

static void test_bad_arguments_are_refused(void) {
    TEST_ASSERT_EQUAL_INT(NEV_ERR_INVALID_ARG, nev_store_set_num((nev_setting_t)999, 1));
    TEST_ASSERT_EQUAL_INT(NEV_ERR_INVALID_ARG, nev_store_set_num(NEV_SET_DEVICE_NAME, 1));
    TEST_ASSERT_EQUAL_INT(NEV_ERR_INVALID_ARG, nev_store_set_str(NEV_SET_VOLUME, "loud"));
    TEST_ASSERT_EQUAL_INT(NEV_OK, nev_store_set_str(NEV_SET_WIFI_SSID, NULL));
    TEST_ASSERT_NOT_NULL(nev_store_describe(NEV_SET_VOLUME));
    TEST_ASSERT_NULL(nev_store_describe((nev_setting_t)-1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_setting_has_a_usable_descriptor);
    RUN_TEST(test_keys_are_unique);
    RUN_TEST(test_defaults_are_inside_their_declared_range);
    RUN_TEST(test_fresh_store_returns_defaults);
    RUN_TEST(test_out_of_range_writes_are_clamped_not_rejected);
    RUN_TEST(test_long_strings_are_truncated_to_the_declared_length);
    RUN_TEST(test_writing_the_same_value_does_not_dirty_the_store);
    RUN_TEST(test_changes_are_published_on_the_bus);
    RUN_TEST(test_writeback_waits_for_changes_to_settle);
    RUN_TEST(test_values_survive_a_restart);
    RUN_TEST(test_deinit_flushes_pending_changes);
    RUN_TEST(test_a_damaged_file_falls_back_to_defaults);
    RUN_TEST(test_out_of_range_stored_values_are_clamped_on_load);
    RUN_TEST(test_factory_reset_restores_every_default);
    RUN_TEST(test_bad_arguments_are_refused);
    return UNITY_END();
}
