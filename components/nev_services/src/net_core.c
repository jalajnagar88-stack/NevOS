/* The Wi-Fi state machine. See net_core.h. */
#include "nev_services/net_core.h"

static nev_net_action_t enter(nev_net_core_t *c, nev_net_state_t state, uint32_t now_ms,
                              nev_net_action_t action) {
    c->state = state;
    c->since_ms = now_ms;
    return action;
}

static nev_net_action_t fail(nev_net_core_t *c, uint32_t now_ms) {
    const bool was_online = (c->state == NEV_NET_ONLINE);

    c->attempts++;
    if (c->backoff_ms == 0) {
        c->backoff_ms = c->cfg.backoff_min_ms;
    } else {
        c->backoff_ms *= 2;
        if (c->backoff_ms > c->cfg.backoff_max_ms) c->backoff_ms = c->cfg.backoff_max_ms;
    }
    /* DOWN only when something was up. A failed attempt while already offline
     * is not news, and publishing it would make the status bar flicker through
     * every retry. */
    return enter(c, NEV_NET_BACKOFF, now_ms, was_online ? NEV_NET_DO_DOWN : NEV_NET_DO_NOTHING);
}

void nev_net_core_init(nev_net_core_t *c, const nev_net_cfg_t *cfg, uint32_t now_ms) {
    if (!c || !cfg) return;
    c->cfg = *cfg;
    c->state = NEV_NET_IDLE;
    c->have_credentials = false;
    c->since_ms = now_ms;
    c->backoff_ms = 0;
    c->attempts = 0;
    c->connections = 0;
}

nev_net_action_t nev_net_core_credentials(nev_net_core_t *c, bool have, uint32_t now_ms) {
    if (!c) return NEV_NET_DO_NOTHING;
    c->have_credentials = have;

    if (!have) {
        const bool was_online = (c->state == NEV_NET_ONLINE);
        enter(c, NEV_NET_IDLE, now_ms, NEV_NET_DO_NOTHING);
        return was_online ? NEV_NET_DO_DOWN : NEV_NET_DO_DISCONNECT;
    }
    if (c->state != NEV_NET_IDLE) return NEV_NET_DO_NOTHING;

    /* New credentials clear the backoff. The user has just told us something
     * changed, and making them wait out a 30-second timer set by the old
     * password would be the device sulking. */
    c->backoff_ms = 0;
    c->attempts = 0;
    return enter(c, NEV_NET_CONNECTING, now_ms, NEV_NET_DO_CONNECT);
}

nev_net_action_t nev_net_core_associated(nev_net_core_t *c, uint32_t now_ms) {
    if (!c || c->state != NEV_NET_CONNECTING) return NEV_NET_DO_NOTHING;
    /* Associated is not online. An access point that accepts the association
     * and never hands out an address is the most common broken network there
     * is — a captive portal — and treating this as success is how a device
     * ends up permanently "connected" to nothing. */
    return enter(c, NEV_NET_WAITING_IP, now_ms, NEV_NET_DO_NOTHING);
}

nev_net_action_t nev_net_core_got_address(nev_net_core_t *c, uint32_t now_ms) {
    if (!c || c->state == NEV_NET_ONLINE) return NEV_NET_DO_NOTHING;

    c->backoff_ms = 0;
    c->attempts = 0;
    c->connections++;
    return enter(c, NEV_NET_ONLINE, now_ms, NEV_NET_DO_UP);
}

nev_net_action_t nev_net_core_lost(nev_net_core_t *c, uint32_t now_ms) {
    if (!c || c->state == NEV_NET_IDLE) return NEV_NET_DO_NOTHING;
    return fail(c, now_ms);
}

nev_net_action_t nev_net_core_tick(nev_net_core_t *c, uint32_t now_ms) {
    if (!c) return NEV_NET_DO_NOTHING;
    const uint32_t since = now_ms - c->since_ms;

    switch (c->state) {
        case NEV_NET_WAITING_IP:
            if (since >= c->cfg.dhcp_timeout_ms) return fail(c, now_ms);
            return NEV_NET_DO_NOTHING;

        case NEV_NET_CONNECTING:
            /* The driver reports association failures itself; this catches the
             * case where it reports nothing at all. */
            if (since >= c->cfg.dhcp_timeout_ms * 2) return fail(c, now_ms);
            return NEV_NET_DO_NOTHING;

        case NEV_NET_BACKOFF:
            if (since < c->backoff_ms) return NEV_NET_DO_NOTHING;
            if (!c->have_credentials) return enter(c, NEV_NET_IDLE, now_ms, NEV_NET_DO_NOTHING);
            return enter(c, NEV_NET_CONNECTING, now_ms, NEV_NET_DO_CONNECT);

        case NEV_NET_IDLE:
        case NEV_NET_ONLINE:
        default:
            return NEV_NET_DO_NOTHING;
    }
}

nev_net_state_t nev_net_core_state(const nev_net_core_t *c) {
    return c ? c->state : NEV_NET_IDLE;
}

const char *nev_net_state_name(nev_net_state_t state) {
    switch (state) {
        case NEV_NET_IDLE:
            return "idle";
        case NEV_NET_CONNECTING:
            return "connecting";
        case NEV_NET_WAITING_IP:
            return "waiting for an address";
        case NEV_NET_ONLINE:
            return "online";
        case NEV_NET_BACKOFF:
            return "backing off";
        default:
            return "?";
    }
}
