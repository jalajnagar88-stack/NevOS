/*
 * OTA: is this offer worth taking, and did the right bytes arrive.
 *
 * The cases that matter are the ones that are painful to stage on hardware —
 * a truncated download, a flipped bit, a downgrade — and the consequence of
 * getting any of them wrong is a device in someone's house that does not boot.
 */
#include <string.h>

#include "nev_services/ota_core.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define PARTITION (5u * 1024u * 1024u)

static const uint8_t kImage[] = "this is a firmware image, for the purposes of argument";
#define IMAGE_LEN (sizeof(kImage) - 1)

static nev_ota_offer_t offer_for(const char *version, const uint8_t *image, size_t len) {
    nev_ota_offer_t o;
    memset(&o, 0, sizeof(o));
    snprintf(o.version, sizeof(o.version), "%s", version);
    o.size_bytes = (uint32_t)len;
    nev_sha256(image, len, o.sha256);
    return o;
}

static nev_ota_core_t make(void) {
    nev_ota_core_t c;
    nev_ota_core_init(&c, "0.1.0", PARTITION);
    return c;
}

/* ----------------------------------------------------------- version order */

static void test_versions_compare_numerically(void) {
    /* The bug this exists to prevent: lexicographically, "0.10.0" sorts before
     * "0.9.0", so the tenth release would refuse to install. */
    TEST_ASSERT_TRUE(nev_ota_version_compare("0.10.0", "0.9.0") > 0);
    TEST_ASSERT_TRUE(nev_ota_version_compare("1.0.0", "0.99.99") > 0);
    TEST_ASSERT_TRUE(nev_ota_version_compare("0.1.2", "0.1.10") < 0);
    TEST_ASSERT_EQUAL_INT(0, nev_ota_version_compare("1.2.3", "1.2.3"));
}

static void test_a_missing_component_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, nev_ota_version_compare("1.2", "1.2.0"));
    TEST_ASSERT_TRUE(nev_ota_version_compare("1.2.1", "1.2") > 0);
}

static void test_a_suffix_does_not_confuse_it(void) {
    TEST_ASSERT_EQUAL_INT(0, nev_ota_version_compare("1.2.3-rc1", "1.2.3"));
}

/* ------------------------------------------------------------------ offers */

static void test_a_newer_version_is_accepted(void) {
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    TEST_ASSERT_TRUE(nev_ota_core_offer(&c, &o));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_OFFERED, nev_ota_core_state(&c));
}

static void test_the_running_version_is_not_an_update(void) {
    /* And it is not an error either: a daemon offering what is already running
     * is the normal state of affairs, not something to show the user. */
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.1.0", kImage, IMAGE_LEN);
    TEST_ASSERT_FALSE(nev_ota_core_offer(&c, &o));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_REJECT_NOT_NEWER, c.reject);
}

static void test_a_downgrade_is_refused(void) {
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.0.9", kImage, IMAGE_LEN);
    TEST_ASSERT_FALSE(nev_ota_core_offer(&c, &o));
}

static void test_an_image_that_does_not_fit_is_refused_before_anything_is_written(void) {
    nev_ota_core_t c = make();
    nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    o.size_bytes = PARTITION + 1;
    TEST_ASSERT_FALSE(nev_ota_core_offer(&c, &o));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_REJECT_TOO_BIG, c.reject);
}

static void test_a_nonsense_offer_is_refused(void) {
    nev_ota_core_t c = make();
    nev_ota_offer_t o = offer_for("", kImage, IMAGE_LEN);
    TEST_ASSERT_FALSE(nev_ota_core_offer(&c, &o));

    o = offer_for("9.9.9", kImage, IMAGE_LEN);
    o.size_bytes = 0;
    TEST_ASSERT_FALSE(nev_ota_core_offer(&c, &o));
}

/* --------------------------------------------------------------- downloads */

static void test_a_complete_download_verifies(void) {
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    TEST_ASSERT_TRUE(nev_ota_core_offer(&c, &o));
    nev_ota_core_begin(&c);

    /* In pieces, as it arrives over a socket. */
    for (size_t i = 0; i < IMAGE_LEN; i += 7) {
        const size_t n = (IMAGE_LEN - i) < 7 ? (IMAGE_LEN - i) : 7;
        TEST_ASSERT_TRUE(nev_ota_core_feed(&c, kImage + i, n));
    }
    TEST_ASSERT_EQUAL_UINT8(100, nev_ota_core_progress(&c));
    TEST_ASSERT_TRUE(nev_ota_core_finish(&c));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_READY, nev_ota_core_state(&c));
}

static void test_a_single_flipped_bit_is_caught(void) {
    /* The check that must never be skipped: everything upstream is a network
     * the device does not control, and the cost of being wrong is a brick. */
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    nev_ota_core_offer(&c, &o);
    nev_ota_core_begin(&c);

    uint8_t corrupt[IMAGE_LEN];
    memcpy(corrupt, kImage, IMAGE_LEN);
    corrupt[IMAGE_LEN / 2] ^= 0x01;

    TEST_ASSERT_TRUE(nev_ota_core_feed(&c, corrupt, IMAGE_LEN));
    TEST_ASSERT_FALSE(nev_ota_core_finish(&c));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_REJECT_DIGEST, c.reject);
    TEST_ASSERT_EQUAL_INT(NEV_OTA_FAILED, nev_ota_core_state(&c));
}

static void test_a_truncated_download_is_caught(void) {
    /* Wi-Fi drops halfway. The partial image must never be installed, however
     * plausible the first half looked. */
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    nev_ota_core_offer(&c, &o);
    nev_ota_core_begin(&c);

    nev_ota_core_feed(&c, kImage, IMAGE_LEN / 2);
    TEST_ASSERT_TRUE(nev_ota_core_progress(&c) < 100);
    TEST_ASSERT_FALSE(nev_ota_core_finish(&c));
    TEST_ASSERT_EQUAL_INT(NEV_OTA_REJECT_SHORT, c.reject);
}

static void test_an_overrun_stops_the_write_immediately(void) {
    /* The bytes are going into a flash partition as they arrive, so this must
     * fail at the moment it overruns, not at the end. */
    nev_ota_core_t c = make();
    const nev_ota_offer_t o = offer_for("0.2.0", kImage, IMAGE_LEN);
    nev_ota_core_offer(&c, &o);
    nev_ota_core_begin(&c);

    TEST_ASSERT_TRUE(nev_ota_core_feed(&c, kImage, IMAGE_LEN));
    TEST_ASSERT_FALSE_MESSAGE(nev_ota_core_feed(&c, kImage, 1), "wrote past the promised size");
    TEST_ASSERT_EQUAL_INT(NEV_OTA_REJECT_LONG, c.reject);
}

static void test_nothing_can_be_fed_before_the_offer_is_accepted(void) {
    nev_ota_core_t c = make();
    TEST_ASSERT_FALSE(nev_ota_core_feed(&c, kImage, IMAGE_LEN));
    TEST_ASSERT_FALSE(nev_ota_core_finish(&c));
}

static void test_every_refusal_has_something_to_say(void) {
    /* Whatever goes wrong, the device has to be able to tell the user why. */
    for (int i = NEV_OTA_REJECT_NONE; i <= NEV_OTA_REJECT_DIGEST; i++) {
        const char *reason = nev_ota_reject_reason((nev_ota_reject_t)i);
        TEST_ASSERT_NOT_NULL(reason);
        if (i != NEV_OTA_REJECT_NONE) {
            TEST_ASSERT_TRUE_MESSAGE(strlen(reason) > 0, "a refusal with no explanation");
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_versions_compare_numerically);
    RUN_TEST(test_a_missing_component_is_zero);
    RUN_TEST(test_a_suffix_does_not_confuse_it);
    RUN_TEST(test_a_newer_version_is_accepted);
    RUN_TEST(test_the_running_version_is_not_an_update);
    RUN_TEST(test_a_downgrade_is_refused);
    RUN_TEST(test_an_image_that_does_not_fit_is_refused_before_anything_is_written);
    RUN_TEST(test_a_nonsense_offer_is_refused);
    RUN_TEST(test_a_complete_download_verifies);
    RUN_TEST(test_a_single_flipped_bit_is_caught);
    RUN_TEST(test_a_truncated_download_is_caught);
    RUN_TEST(test_an_overrun_stops_the_write_immediately);
    RUN_TEST(test_nothing_can_be_fed_before_the_offer_is_accepted);
    RUN_TEST(test_every_refusal_has_something_to_say);
    return UNITY_END();
}
