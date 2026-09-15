/*
 * NEVOS L2 — input.
 *
 * The ONLY producer of INPUT events (docs/event-bus.md §3). Touch, buttons and
 * — from M5 — IMU gestures are fused here into one stream, so that no app ever
 * parses a raw accelerometer sample or debounces a GPIO.
 *
 * It also owns LVGL's pointer input device, so touch reaches widgets through
 * the same board call that produces the bus events. One source of truth for
 * "where is the finger".
 */
#ifndef NEV_SERVICES_INPUT_SERVICE_H
#define NEV_SERVICES_INPUT_SERVICE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

nev_err_t input_service_init(void);
void input_service_deinit(void);

/* Polls buttons and publishes their edges. Touch is pulled by LVGL itself on
 * its own schedule, so it is not polled here. Called once per frame. */
void input_service_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_INPUT_SERVICE_H */
