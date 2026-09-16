/* The idle policy. See power_core.h. */
#include "nev_services/power_core.h"

/* Dimming at two thirds of the sleep timeout gives a warning that the screen is
 * about to go dark, from the single number the user actually set. */
#define DIM_NUMERATOR       2
#define DIM_DENOMINATOR     3

/* Below this, with no charger, the timeouts halve. The device is a desk object;
 * the last 15% is better spent staying alive than staying lit. */
#define LOW_BATTERY_PERCENT 15

static uint32_t sleep_after(const nev_power_core_t *c) {
    uint32_t ms = c->cfg.sleep_after_ms;
    if (c->battery_low && !c->charging) ms /= 2;
    return ms;
}

static uint32_t dim_after(const nev_power_core_t *c) {
    return sleep_after(c) * DIM_NUMERATOR / DIM_DENOMINATOR;
}

static void set_phase(nev_power_core_t *c, nev_power_phase_t phase) {
    if (c->phase == phase) return;
    c->phase = phase;
    c->phase_changed = true;

    switch (phase) {
        case NEV_POWER_ACTIVE:
            c->brightness = c->cfg.active_percent;
            break;
        case NEV_POWER_DIM:
            c->brightness = (uint8_t)((uint32_t)c->cfg.active_percent * c->cfg.dim_percent / 100u);
            /* Never all the way to zero here: DIM must stay readable, or it is
             * just a slower way of turning the screen off. */
            if (c->brightness < 5) c->brightness = 5;
            break;
        case NEV_POWER_ASLEEP:
            c->brightness = 0;
            break;
    }
}

void nev_power_core_init(nev_power_core_t *c, const nev_power_cfg_t *cfg, uint32_t now_ms) {
    if (!c || !cfg) return;
    c->cfg = *cfg;
    c->phase = NEV_POWER_ACTIVE;
    c->brightness = cfg->active_percent;
    c->last_activity_ms = now_ms;
    c->charging = false;
    c->battery_low = false;
    c->phase_changed = false;
}

void nev_power_core_configure(nev_power_core_t *c, const nev_power_cfg_t *cfg) {
    if (!c || !cfg) return;
    c->cfg = *cfg;
    /* Re-apply the new brightness to the phase we are already in, rather than
     * waiting for the next transition: dragging the slider should be visible
     * immediately, which is the only way to judge where to leave it. */
    nev_power_phase_t phase = c->phase;
    c->phase = (nev_power_phase_t)0xFF;
    c->phase_changed = false;
    set_phase(c, phase);
    c->phase_changed = false;
}

bool nev_power_core_activity(nev_power_core_t *c, uint32_t now_ms) {
    if (!c) return true;

    const bool was_asleep = (c->phase == NEV_POWER_ASLEEP);
    c->last_activity_ms = now_ms;
    set_phase(c, NEV_POWER_ACTIVE);
    return !was_asleep;
}

void nev_power_core_supply(nev_power_core_t *c, bool charging, uint8_t battery_percent) {
    if (!c) return;
    c->charging = charging;
    c->battery_low = battery_percent <= LOW_BATTERY_PERCENT;
}

nev_power_phase_t nev_power_core_tick(nev_power_core_t *c, uint32_t now_ms) {
    if (!c) return NEV_POWER_ACTIVE;
    c->phase_changed = false;

    /* Unsigned subtraction, so this stays correct across the 49-day wrap of a
     * 32-bit millisecond counter. nev_elapsed_ms would read the real clock,
     * which is exactly the dependency this file exists without. */
    const uint32_t since = now_ms - c->last_activity_ms;

    if (since >= sleep_after(c)) {
        /*
         * A device on a charger stops at DIM and never goes dark.
         *
         * It is a thing on a desk with a face and a clock on it, and the reason
         * to leave it plugged in is to be able to glance at it. Turning the
         * screen off to save power that is arriving down a cable would be
         * saving the wrong thing.
         */
        set_phase(c, c->charging ? NEV_POWER_DIM : NEV_POWER_ASLEEP);
    } else if (since >= dim_after(c)) {
        set_phase(c, NEV_POWER_DIM);
    } else {
        set_phase(c, NEV_POWER_ACTIVE);
    }
    return c->phase;
}

uint8_t nev_power_core_brightness(const nev_power_core_t *c) {
    return c ? c->brightness : 100;
}

bool nev_power_core_may_idle_cpu(const nev_power_core_t *c) {
    return c && c->phase == NEV_POWER_ASLEEP && !c->charging;
}

const char *nev_power_phase_name(nev_power_phase_t phase) {
    switch (phase) {
        case NEV_POWER_ACTIVE:
            return "active";
        case NEV_POWER_DIM:
            return "dim";
        case NEV_POWER_ASLEEP:
            return "asleep";
        default:
            return "?";
    }
}
