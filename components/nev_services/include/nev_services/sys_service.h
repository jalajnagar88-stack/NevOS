/* NEVOS L2 — the housekeeping the rest of the system assumes someone does. */
#ifndef NEV_SERVICES_SYS_SERVICE_H
#define NEV_SERVICES_SYS_SERVICE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The once-a-second job: publish the tick that the clock and the status bar
 * count on, report memory, and say out loud when a queue has been dropping
 * events or a buffer has been held too long.
 *
 * It exists because every one of those was previously either nobody's job or
 * the simulator's, and "the simulator does it" is how a blob leak survived a
 * whole milestone on the host.
 */
nev_err_t sys_service_init(uint32_t now_ms);
void sys_service_deinit(void);
void sys_service_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_SYS_SERVICE_H */
