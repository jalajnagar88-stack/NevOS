/* Idle, brightness and battery. The decisions are all in power_core. */
#include "nev_services/power_service.h"

#include "nev_board/board.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"

#define TAG            "power"

/* The battery is read over I2C or ADC; once every five seconds is plenty for a
 * number that moves over hours, and it keeps the poll off the frame budget. */
#define SUPPLY_POLL_MS 5000u

/* And reported on the bus less often still. The status bar redraws when this
 * arrives, and a percentage that flickers between two values is worse than one
 * that updates slowly. */
#define REPORT_MS      30000u

static nev_power_core_t s_core;
static uint32_t s_last_supply_ms;
static uint32_t s_last_report_ms;
static uint8_t s_last_percent;
static bool s_last_charging;
static bool s_ready;
static nev_sub_t *s_sub;

static nev_power_cfg_t cfg_from_settings(void) {
    nev_power_cfg_t cfg = {
        .sleep_after_ms = nev_store_num(NEV_SET_SLEEP_TIMEOUT_S) * 1000u,
        .active_percent = (uint8_t)nev_store_num(NEV_SET_BRIGHTNESS),
        /* Not a setting: one brightness slider is enough for anyone, and this
         * ratio is what makes a dim look like a warning rather than a fault. */
        .dim_percent = 30,
    };
    return cfg;
}

static void apply_brightness(void) {
    nev_board_backlight_set(nev_power_core_brightness(&s_core));
}

nev_err_t power_service_init(uint32_t now_ms) {
    const nev_power_cfg_t cfg = cfg_from_settings();
    nev_power_core_init(&s_core, &cfg, now_ms);

    /* Settings only: the power service must not react to input events, because
     * input is gated through it before it ever reaches the bus. */
    const nev_sub_cfg_t sub = {
        .name = "power",
        .domains = NEV_DOM(STORAGE),
        .depth = 4,
        .full_policy = NEV_FULL_DROP_OLDEST,
    };
    s_sub = nev_bus_subscribe(&sub);
    if (!s_sub) return NEV_ERR_NO_MEM;

    s_last_supply_ms = now_ms;
    s_last_report_ms = now_ms;
    s_last_percent = 0xFF;
    s_ready = true;
    apply_brightness();

    NEV_LOGI(TAG, "sleep after %us, brightness %u%%", (unsigned)(cfg.sleep_after_ms / 1000u),
             (unsigned)cfg.active_percent);
    return NEV_OK;
}

void power_service_deinit(void) {
    s_ready = false;
    s_sub = NULL;
}

bool power_service_gate_input(uint32_t now_ms) {
    if (!s_ready) return true;

    const bool deliver = nev_power_core_activity(&s_core, now_ms);
    if (s_core.phase_changed) {
        apply_brightness();
        NEV_LOGI(TAG, "awake");
        (void)nev_bus_publish_type(NEV_EVT_POWER_IDLE_EXIT, NEV_SRC_POWER);
    }
    return deliver;
}

static void drain_settings(void) {
    nev_event_t ev;
    bool changed = false;
    while (nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        if (ev.type == NEV_EVT_STORAGE_SETTING_CHANGED) changed = true;
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
    if (!changed) return;

    const nev_power_cfg_t cfg = cfg_from_settings();
    nev_power_core_configure(&s_core, &cfg);
    apply_brightness();
}

static void poll_supply(uint32_t now_ms) {
    nev_power_state_t state;
    nev_board_power_state(&state);
    nev_power_core_supply(&s_core, state.charging, state.percent);

    const bool charging_changed = state.charging != s_last_charging;
    /* Every percent would be a redraw for a number that moves over hours. */
    const bool worth_reporting =
        charging_changed || s_last_percent == 0xFF || (now_ms - s_last_report_ms) >= REPORT_MS;
    if (!worth_reporting) return;

    nev_event_t ev = nev_event_make(NEV_EVT_POWER_BATTERY, NEV_SRC_POWER);
    ev.p.battery.millivolts = state.millivolts;
    ev.p.battery.percent = state.percent;
    ev.p.battery.charging = state.charging ? 1 : 0;
    (void)nev_bus_publish(&ev);

    if (charging_changed) {
        (void)nev_bus_publish_type(NEV_EVT_POWER_CHARGING, NEV_SRC_POWER);
        NEV_LOGI(TAG, "%s", state.charging ? "on charge" : "on battery");
    }
    s_last_charging = state.charging;
    s_last_percent = state.percent;
    s_last_report_ms = now_ms;
}

void power_service_tick(uint32_t now_ms) {
    if (!s_ready) return;

    drain_settings();

    if ((now_ms - s_last_supply_ms) >= SUPPLY_POLL_MS) {
        s_last_supply_ms = now_ms;
        poll_supply(now_ms);
    }

    const nev_power_phase_t before = s_core.phase;
    nev_power_core_tick(&s_core, now_ms);
    if (!s_core.phase_changed) return;

    apply_brightness();
    NEV_LOGI(TAG, "%s", nev_power_phase_name(s_core.phase));

    /*
     * IDLE_ENTER means "nobody is here": the persona goes sleepy and the shell
     * stops animating. It is published on the way into ASLEEP only — dimming is
     * a warning, not an absence, and a face that dozed off every time someone
     * paused to read the screen would be exhausting.
     */
    if (s_core.phase == NEV_POWER_ASLEEP) {
        (void)nev_bus_publish_type(NEV_EVT_POWER_IDLE_ENTER, NEV_SRC_POWER);
    } else if (before == NEV_POWER_ASLEEP) {
        (void)nev_bus_publish_type(NEV_EVT_POWER_IDLE_EXIT, NEV_SRC_POWER);
    }
}

nev_power_phase_t power_service_phase(void) {
    return s_core.phase;
}

bool power_service_may_idle_cpu(void) {
    return s_ready && nev_power_core_may_idle_cpu(&s_core);
}
