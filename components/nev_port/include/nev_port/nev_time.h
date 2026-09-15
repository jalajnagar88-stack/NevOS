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

/*
 * Wall clock, as distinct from the monotonic clock above.
 *
 * NEVOS has no RTC and no battery-backed time. Until something authoritative
 * says what time it is — the companion daemon at M6, or NTP — the wall clock is
 * UNSET, and nev_wallclock_is_set() returns false. The status bar shows "--:--"
 * rather than a plausible lie, because a clock that is confidently wrong is
 * worse than one that admits it does not know.
 *
 * Implemented as an offset from the monotonic clock, so it does not drift
 * relative to timers and cannot jump backwards.
 */
void nev_wallclock_set(uint64_t unix_seconds);
uint64_t nev_wallclock_now(void); /* 0 when unset */
bool nev_wallclock_is_set(void);

/* Wrap-safe elapsed helper for 32-bit ms stamps. */
static inline uint32_t nev_elapsed_ms(uint32_t since_ms) {
    return nev_now_ms() - since_ms;
}

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_TIME_H */
