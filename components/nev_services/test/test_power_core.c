/*
 * The idle policy.
 *
 * Every one of these is a case you would otherwise verify by leaving a device
 * on a desk and watching it, which is both slow and unreliable — the
 * interesting moments are a charger unplugged while the screen is dark, and a
 * touch that lands in the same millisecond as a timeout.
 */
#include "nev_services/power_core.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define SLEEP_MS 60000u
#define DIM_MS   40000u /* two thirds of the above */

static nev_power_core_t make(void) {
    const nev_power_cfg_t cfg = {
        .sleep_after_ms = SLEEP_MS,
        .active_percent = 70,
        .dim_percent = 30,
    };
    nev_power_core_t c;
    nev_power_core_init(&c, &cfg, 0);
    return c;
}

static void test_it_starts_awake_at_the_users_brightness(void) {
    nev_power_core_t c = make();
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, c.phase);
    TEST_ASSERT_EQUAL_UINT8(70, nev_power_core_brightness(&c));
}

static void test_it_dims_before_it_sleeps(void) {
    /* The dim is a warning. Going straight from full to dark makes a device
     * feel broken rather than asleep. */
    nev_power_core_t c = make();
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, nev_power_core_tick(&c, DIM_MS - 1));
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, nev_power_core_tick(&c, DIM_MS));
    TEST_ASSERT_EQUAL_UINT8(21, nev_power_core_brightness(&c)); /* 30% of 70 */
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, nev_power_core_tick(&c, SLEEP_MS));
    TEST_ASSERT_EQUAL_UINT8(0, nev_power_core_brightness(&c));
}

static void test_a_dimmed_screen_stays_readable(void) {
    /* With a low brightness setting, 30% of it rounds to nothing. A dim that
     * is indistinguishable from off is just a slower way of turning it off. */
    const nev_power_cfg_t cfg = {
        .sleep_after_ms = SLEEP_MS, .active_percent = 5, .dim_percent = 30};
    nev_power_core_t c;
    nev_power_core_init(&c, &cfg, 0);
    nev_power_core_tick(&c, DIM_MS);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, c.phase);
    TEST_ASSERT_TRUE_MESSAGE(nev_power_core_brightness(&c) >= 5, "dim went dark");
}

static void test_the_phase_change_is_reported_exactly_once(void) {
    /* The caller publishes on this edge. Reporting it twice would put the
     * persona to sleep twice and log two lines for one event. */
    nev_power_core_t c = make();
    nev_power_core_tick(&c, DIM_MS);
    TEST_ASSERT_TRUE(c.phase_changed);
    nev_power_core_tick(&c, DIM_MS + 1);
    TEST_ASSERT_FALSE(c.phase_changed);
}

static void test_a_touch_wakes_it_and_is_swallowed(void) {
    /* Reaching for a dark object, you cannot aim at a button you cannot see.
     * The tap that wakes it must not also press whatever was underneath. */
    nev_power_core_t c = make();
    nev_power_core_tick(&c, SLEEP_MS);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, c.phase);

    TEST_ASSERT_FALSE_MESSAGE(nev_power_core_activity(&c, SLEEP_MS + 10),
                              "the waking touch was delivered as well");
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, c.phase);
    TEST_ASSERT_EQUAL_UINT8(70, nev_power_core_brightness(&c));

    /* And the next one is a real touch. */
    TEST_ASSERT_TRUE(nev_power_core_activity(&c, SLEEP_MS + 20));
}

static void test_a_touch_while_dimmed_is_delivered(void) {
    /* The screen is still readable, so the user aimed at what they hit. */
    nev_power_core_t c = make();
    nev_power_core_tick(&c, DIM_MS);
    TEST_ASSERT_TRUE(nev_power_core_activity(&c, DIM_MS + 1));
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, c.phase);
}

static void test_activity_restarts_the_clock(void) {
    nev_power_core_t c = make();
    nev_power_core_tick(&c, DIM_MS - 1);
    nev_power_core_activity(&c, DIM_MS - 1);
    /* Without the restart this would already be asleep. */
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, nev_power_core_tick(&c, DIM_MS + 1000));
}

static void test_a_charging_device_dims_but_never_goes_dark(void) {
    /*
     * It is an object on a desk with a clock and a face on it, and the reason
     * to leave it plugged in is to be able to glance at it. Turning the screen
     * off to save power that is arriving down a cable saves the wrong thing.
     */
    nev_power_core_t c = make();
    nev_power_core_supply(&c, true, 100);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, nev_power_core_tick(&c, SLEEP_MS * 10));
    TEST_ASSERT_TRUE(nev_power_core_brightness(&c) > 0);
    TEST_ASSERT_FALSE_MESSAGE(nev_power_core_may_idle_cpu(&c),
                              "a plugged-in device should stay quick to wake");
}

static void test_unplugging_a_sleeping_device_lets_it_go_dark(void) {
    /* The charger comes out while it is sitting there dimmed. Nothing touches
     * it, so nothing would re-evaluate the phase unless the tick does. */
    nev_power_core_t c = make();
    nev_power_core_supply(&c, true, 80);
    nev_power_core_tick(&c, SLEEP_MS);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, c.phase);

    nev_power_core_supply(&c, false, 80);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, nev_power_core_tick(&c, SLEEP_MS + 1));
    TEST_ASSERT_TRUE(nev_power_core_may_idle_cpu(&c));
}

static void test_a_low_battery_sleeps_sooner(void) {
    nev_power_core_t c = make();
    nev_power_core_supply(&c, false, 9);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, nev_power_core_tick(&c, SLEEP_MS / 2));
}

static void test_a_low_battery_on_charge_keeps_the_full_timeout(void) {
    /* Plugged in at 9% is a device filling up, not one that is dying. At the
     * point where the test above has already gone dark, this one is still
     * fully awake. */
    nev_power_core_t c = make();
    nev_power_core_supply(&c, true, 9);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, nev_power_core_tick(&c, SLEEP_MS / 2 + 1));
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, nev_power_core_tick(&c, DIM_MS));
}

static void test_changing_brightness_is_visible_immediately(void) {
    /* Dragging the slider has to show its effect, or there is no way to judge
     * where to leave it. */
    nev_power_core_t c = make();
    nev_power_cfg_t cfg = c.cfg;
    cfg.active_percent = 100;
    nev_power_core_configure(&c, &cfg);
    TEST_ASSERT_EQUAL_UINT8(100, nev_power_core_brightness(&c));
    TEST_ASSERT_FALSE_MESSAGE(c.phase_changed, "a settings change is not a phase change");

    /* And while dimmed, the new brightness applies at the dim ratio. */
    nev_power_core_tick(&c, DIM_MS);
    cfg.active_percent = 50;
    nev_power_core_configure(&c, &cfg);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, c.phase);
    TEST_ASSERT_EQUAL_UINT8(15, nev_power_core_brightness(&c));
}

static void test_a_shorter_timeout_can_sleep_it_immediately(void) {
    /* Changing the setting must not need another touch to take effect. */
    nev_power_core_t c = make();
    nev_power_core_tick(&c, 30000);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, c.phase);

    nev_power_cfg_t cfg = c.cfg;
    cfg.sleep_after_ms = 10000;
    nev_power_core_configure(&c, &cfg);
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, nev_power_core_tick(&c, 30001));
}

static void test_the_millisecond_counter_may_wrap(void) {
    /* 32-bit milliseconds run out after 49 days. A device left on a shelf will
     * cross it, and the arithmetic has to survive that without the screen
     * either sticking on or never waking. */
    const nev_power_cfg_t cfg = {
        .sleep_after_ms = SLEEP_MS, .active_percent = 70, .dim_percent = 30};
    nev_power_core_t c;
    const uint32_t before_wrap = 0xFFFFFFFFu - 1000u;
    nev_power_core_init(&c, &cfg, before_wrap);

    TEST_ASSERT_EQUAL_INT(NEV_POWER_ACTIVE, nev_power_core_tick(&c, before_wrap + 500u));
    /* Now past the wrap: 0xFFFFFFFF + 1 == 0. */
    TEST_ASSERT_EQUAL_INT(NEV_POWER_DIM, nev_power_core_tick(&c, before_wrap + DIM_MS));
    TEST_ASSERT_EQUAL_INT(NEV_POWER_ASLEEP, nev_power_core_tick(&c, before_wrap + SLEEP_MS));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_it_starts_awake_at_the_users_brightness);
    RUN_TEST(test_it_dims_before_it_sleeps);
    RUN_TEST(test_a_dimmed_screen_stays_readable);
    RUN_TEST(test_the_phase_change_is_reported_exactly_once);
    RUN_TEST(test_a_touch_wakes_it_and_is_swallowed);
    RUN_TEST(test_a_touch_while_dimmed_is_delivered);
    RUN_TEST(test_activity_restarts_the_clock);
    RUN_TEST(test_a_charging_device_dims_but_never_goes_dark);
    RUN_TEST(test_unplugging_a_sleeping_device_lets_it_go_dark);
    RUN_TEST(test_a_low_battery_sleeps_sooner);
    RUN_TEST(test_a_low_battery_on_charge_keeps_the_full_timeout);
    RUN_TEST(test_changing_brightness_is_visible_immediately);
    RUN_TEST(test_a_shorter_timeout_can_sleep_it_immediately);
    RUN_TEST(test_the_millisecond_counter_may_wrap);
    return UNITY_END();
}
