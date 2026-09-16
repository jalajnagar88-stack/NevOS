/* NEVOS L-1 — randomness. */
#ifndef NEV_PORT_NEV_RAND_H
#define NEV_PORT_NEV_RAND_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fills `out` with random bytes from the platform.
 *
 * Two callers, and the second is why this is not a PRNG seeded from the clock:
 * the WebSocket mask (where any bytes would do) and the pairing code shown on
 * the device's screen. A pairing code that a nearby machine can predict from
 * the time of day defeats the entire out-of-band step, and a device's clock at
 * boot is very predictable — it starts at zero.
 *
 * On the ESP32-S3 this is esp_fill_random, which draws on the hardware RNG.
 * On the host it is /dev/urandom.
 */
void nev_rand_fill(uint8_t *out, size_t len);

/* Convenience for the common cases. */
uint32_t nev_rand_u32(void);

/* Uniform in [0, bound). Returns 0 when bound is 0. */
uint32_t nev_rand_below(uint32_t bound);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_RAND_H */
