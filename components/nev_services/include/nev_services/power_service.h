/* NEVOS L2 — idle, brightness and battery, on the bus. */
#ifndef NEV_SERVICES_POWER_SERVICE_H
#define NEV_SERVICES_POWER_SERVICE_H

#include "nev_port/nev_types.h"
#include "nev_services/power_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The thin half. It reads the settings and the battery, drives the backlight,
 * and publishes POWER.* — every decision it makes comes from power_core.
 */
nev_err_t power_service_init(uint32_t now_ms);
void power_service_deinit(void);

/* Call once per frame. Cheap: it reads the battery once every few seconds. */
void power_service_tick(uint32_t now_ms);

/*
 * Called by input_service before it publishes.
 *
 * Returns false when the event was consumed to wake the device, in which case
 * it must not be published. The gate lives here because the power service is
 * the only thing that knows whether the screen was dark, and it is called
 * rather than subscribed to because an event cannot be un-published.
 */
bool power_service_gate_input(uint32_t now_ms);

nev_power_phase_t power_service_phase(void);

/* True when the board may idle the CPU between frames. */
bool power_service_may_idle_cpu(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_POWER_SERVICE_H */
