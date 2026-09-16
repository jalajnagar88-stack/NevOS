/* NEVOS L2 — the idle and brightness policy, with no clock and no hardware. */
#ifndef NEV_SERVICES_POWER_CORE_H
#define NEV_SERVICES_POWER_CORE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When the screen dims, when it goes dark, and what happens to the first touch
 * afterwards. All of it decided here, against a clock that is an argument.
 *
 * This is the same split the games use. Power behaviour is otherwise miserable
 * to verify — "leave it on a desk for four minutes and see" is not a test, and
 * the interesting cases (a charger unplugged while asleep, a touch arriving in
 * the same millisecond as the timeout) are the ones you would never catch that
 * way.
 */

typedef enum {
    NEV_POWER_ACTIVE = 0, /* someone is using it                          */
    NEV_POWER_DIM,        /* nobody for a while; still readable           */
    NEV_POWER_ASLEEP,     /* backlight off                                */
} nev_power_phase_t;

typedef struct {
    /* The one number the user controls, from settings. Dimming happens at two
     * thirds of it, so a single slider still produces a two-stage fade. */
    uint32_t sleep_after_ms;
    /* The user's brightness, 5..100. */
    uint8_t active_percent;
    /* What DIM means, as a fraction of the above. */
    uint8_t dim_percent;
} nev_power_cfg_t;

typedef struct {
    nev_power_cfg_t cfg;
    nev_power_phase_t phase;
    uint32_t last_activity_ms;
    uint8_t brightness;
    bool charging;
    bool battery_low;
    /* Set for one tick when the phase changed, so the caller knows to publish. */
    bool phase_changed;
} nev_power_core_t;

void nev_power_core_init(nev_power_core_t *c, const nev_power_cfg_t *cfg, uint32_t now_ms);

/* Settings changed while running. Takes effect without resetting the timer. */
void nev_power_core_configure(nev_power_core_t *c, const nev_power_cfg_t *cfg);

/*
 * Someone touched it, pressed a button, or shook it.
 *
 * Returns true if the event should be delivered to the rest of the system, and
 * false if it was consumed to wake the device. Waking from a dark screen should
 * not also launch whatever was under the finger — a person reaching for a dark
 * object cannot aim at a button they cannot see.
 *
 * A tap while merely dimmed is delivered: the screen is still readable, so the
 * user meant to press what they pressed.
 */
bool nev_power_core_activity(nev_power_core_t *c, uint32_t now_ms);

/* Battery and charger state, from the board. */
void nev_power_core_supply(nev_power_core_t *c, bool charging, uint8_t battery_percent);

/* Advances time. Returns the phase; check phase_changed for an edge. */
nev_power_phase_t nev_power_core_tick(nev_power_core_t *c, uint32_t now_ms);

/* Backlight percentage the board should be set to right now. */
uint8_t nev_power_core_brightness(const nev_power_core_t *c);

/*
 * True when the board may idle the CPU between frames.
 *
 * Only when asleep and on battery: a device on a charger has nothing to save
 * and everything to lose by being slow to wake.
 */
bool nev_power_core_may_idle_cpu(const nev_power_core_t *c);

const char *nev_power_phase_name(nev_power_phase_t phase);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_POWER_CORE_H */
