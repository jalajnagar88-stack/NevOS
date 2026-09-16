/* NEVOS L2 — Wi-Fi, on the bus. */
#ifndef NEV_SERVICES_NET_SERVICE_H
#define NEV_SERVICES_NET_SERVICE_H

#include "nev_port/nev_types.h"
#include "nev_services/net_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The only producer of NET events. It reads the credentials from the store,
 * drives the radio through nev_board, and tells nev_port whether the link is
 * up — which is what the bridge waits on before it looks for a daemon.
 *
 * Every decision it makes comes from net_core, which has no radio in it.
 */
nev_err_t net_service_init(uint32_t now_ms);
void net_service_deinit(void);
void net_service_tick(uint32_t now_ms);

nev_net_state_t net_service_state(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_NET_SERVICE_H */
