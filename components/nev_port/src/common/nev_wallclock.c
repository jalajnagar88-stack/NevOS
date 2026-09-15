/*
 * Wall clock as an offset from the monotonic clock.
 *
 * Keeping it as an offset rather than a free-running counter means the wall
 * clock inherits the monotonic clock's properties: it cannot jump backwards,
 * and it cannot drift relative to the timers everything else is scheduled on.
 * Setting it is a single assignment, which is also what makes it safe to update
 * from the network without disturbing anything mid-frame.
 */
#include "nev_port/nev_time.h"

static uint64_t s_epoch_at_boot; /* unix seconds corresponding to monotonic 0 */
static bool s_set;

void nev_wallclock_set(uint64_t unix_seconds) {
    if (unix_seconds == 0) return;
    const uint64_t uptime_s = nev_now_us() / 1000000u;
    s_epoch_at_boot = unix_seconds > uptime_s ? unix_seconds - uptime_s : 0;
    s_set = true;
}

uint64_t nev_wallclock_now(void) {
    if (!s_set) return 0;
    return s_epoch_at_boot + nev_now_us() / 1000000u;
}

bool nev_wallclock_is_set(void) {
    return s_set;
}
