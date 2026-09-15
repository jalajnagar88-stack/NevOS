/* NEVOS L-1 — monotonic time and bounded sleeps. */
#ifndef NEV_PORT_NEV_TIME_H
#define NEV_PORT_NEV_TIME_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic since boot. Never wall-clock: not affected by NTP or timezone. */
uint64_t nev_now_us(void);
uint32_t nev_now_ms(void);

/*
 * Blocking sleeps. Legal ONLY inside a task's own loop in L0-L2.
 * Never call these from app code or from any bus handler: see ARCHITECTURE.md R3.
 * CI rejects them under apps/.
 */
void nev_sleep_ms(uint32_t ms);
void nev_sleep_until_us(uint64_t deadline_us);

/* Wrap-safe elapsed helper for 32-bit ms stamps. */
static inline uint32_t nev_elapsed_ms(uint32_t since_ms) { return nev_now_ms() - since_ms; }

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_TIME_H */
