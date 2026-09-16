/* NEVOS L2 — the Wi-Fi state machine, with no radio in it. */
#ifndef NEV_SERVICES_NET_CORE_H
#define NEV_SERVICES_NET_CORE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When to connect, when to give up, and how long to wait before trying again.
 *
 * Wi-Fi is the subsystem with the worst ratio of "states that matter" to
 * "states you can produce on demand": an access point that accepts the
 * association and then never hands out an address, a password that changed
 * while the device was asleep, a router rebooting under it. None of those are
 * reachable by holding a real device and hoping. Here they are four function
 * calls.
 */

typedef enum {
    NEV_NET_IDLE = 0,   /* no credentials, or deliberately off */
    NEV_NET_CONNECTING, /* association in progress             */
    NEV_NET_WAITING_IP, /* associated, no address yet          */
    NEV_NET_ONLINE,     /* address in hand                     */
    NEV_NET_BACKOFF,    /* failed; waiting before another go   */
} nev_net_state_t;

/* What the service should do next. */
typedef enum {
    NEV_NET_DO_NOTHING = 0,
    NEV_NET_DO_CONNECT,    /* start associating                    */
    NEV_NET_DO_DISCONNECT, /* tear the link down                   */
    NEV_NET_DO_UP,         /* publish NET.WIFI_UP; link is usable  */
    NEV_NET_DO_DOWN,       /* publish NET.WIFI_DOWN                */
} nev_net_action_t;

typedef struct {
    /* First retry. Doubles to the cap, because the usual cause of a failure is
     * a router that is still booting, and that resolves in seconds. */
    uint32_t backoff_min_ms;
    uint32_t backoff_max_ms;
    /* An association that never produces an address is a captive portal or a
     * DHCP server that is not there. Both look like success until this fires. */
    uint32_t dhcp_timeout_ms;
} nev_net_cfg_t;

typedef struct {
    nev_net_cfg_t cfg;
    nev_net_state_t state;
    bool have_credentials;
    uint32_t since_ms;    /* when the current state began */
    uint32_t backoff_ms;  /* current wait                 */
    uint32_t attempts;    /* consecutive failures         */
    uint32_t connections; /* successful ones, for the log */
} nev_net_core_t;

void nev_net_core_init(nev_net_core_t *c, const nev_net_cfg_t *cfg, uint32_t now_ms);

/* Credentials appeared or were cleared. */
nev_net_action_t nev_net_core_credentials(nev_net_core_t *c, bool have, uint32_t now_ms);

/* The driver associated with an access point. */
nev_net_action_t nev_net_core_associated(nev_net_core_t *c, uint32_t now_ms);

/* DHCP finished. */
nev_net_action_t nev_net_core_got_address(nev_net_core_t *c, uint32_t now_ms);

/* The driver lost the link, or refused it. */
nev_net_action_t nev_net_core_lost(nev_net_core_t *c, uint32_t now_ms);

/* Time passing: drives the backoff and the DHCP timeout. */
nev_net_action_t nev_net_core_tick(nev_net_core_t *c, uint32_t now_ms);

nev_net_state_t nev_net_core_state(const nev_net_core_t *c);
const char *nev_net_state_name(nev_net_state_t state);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_NET_CORE_H */
