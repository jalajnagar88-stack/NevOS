/* Wi-Fi. Every decision here comes from net_core. */
#include "nev_services/net_service.h"

#include "nev_board/board.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"

#define TAG     "net"

/* Once a frame is far more often than a radio changes state, and the poll is a
 * register read. Every 250 ms is plenty and keeps it off the frame budget. */
#define POLL_MS 250u

static nev_net_core_t s_core;
static nev_sub_t *s_sub;
static uint32_t s_last_poll_ms;
static nev_wifi_status_t s_last_status;
static bool s_ready;

static bool have_credentials(void) {
    const char *ssid = nev_store_str(NEV_SET_WIFI_SSID);
    return ssid && ssid[0] != '\0';
}

static void act(nev_net_action_t action) {
    switch (action) {
        case NEV_NET_DO_CONNECT:
            (void)nev_board_wifi_connect(nev_store_str(NEV_SET_WIFI_SSID), "");
            break;

        case NEV_NET_DO_DISCONNECT:
            nev_board_wifi_disconnect();
            break;

        case NEV_NET_DO_UP:
            NEV_LOGI(TAG, "online");
            (void)nev_bus_publish_type(NEV_EVT_NET_WIFI_UP, NEV_SRC_NET);
            break;

        case NEV_NET_DO_DOWN:
            NEV_LOGW(TAG, "offline");
            (void)nev_bus_publish_type(NEV_EVT_NET_WIFI_DOWN, NEV_SRC_NET);
            break;

        case NEV_NET_DO_NOTHING:
        default:
            break;
    }
}

nev_err_t net_service_init(uint32_t now_ms) {
    const nev_net_cfg_t cfg = {
        .backoff_min_ms = 1000,
        .backoff_max_ms = 30000,
        .dhcp_timeout_ms = 8000,
    };
    nev_net_core_init(&s_core, &cfg, now_ms);

    const nev_sub_cfg_t sub = {
        .name = "net",
        .domains = NEV_DOM(STORAGE),
        .depth = 4,
        .full_policy = NEV_FULL_DROP_OLDEST,
    };
    s_sub = nev_bus_subscribe(&sub);
    if (!s_sub) return NEV_ERR_NO_MEM;

    s_last_poll_ms = now_ms;
    s_last_status = NEV_WIFI_DOWN;
    s_ready = true;

    act(nev_net_core_credentials(&s_core, have_credentials(), now_ms));
    return NEV_OK;
}

void net_service_deinit(void) {
    if (!s_ready) return;
    nev_board_wifi_disconnect();
    s_ready = false;
    s_sub = NULL;
}

void net_service_tick(uint32_t now_ms) {
    if (!s_ready) return;

    nev_event_t ev;
    bool settings_changed = false;
    while (nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        if (ev.type == NEV_EVT_STORAGE_SETTING_CHANGED) settings_changed = true;
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
    if (settings_changed) {
        act(nev_net_core_credentials(&s_core, have_credentials(), now_ms));
    }

    if ((now_ms - s_last_poll_ms) >= POLL_MS) {
        s_last_poll_ms = now_ms;

        const nev_wifi_status_t status = nev_board_wifi_status();
        if (status != s_last_status) {
            s_last_status = status;
            switch (status) {
                case NEV_WIFI_ASSOCIATED:
                    act(nev_net_core_associated(&s_core, now_ms));
                    break;
                case NEV_WIFI_ONLINE:
                    act(nev_net_core_got_address(&s_core, now_ms));
                    break;
                case NEV_WIFI_DOWN:
                case NEV_WIFI_REFUSED:
                    act(nev_net_core_lost(&s_core, now_ms));
                    break;
            }
        }
    }

    act(nev_net_core_tick(&s_core, now_ms));
}

nev_net_state_t net_service_state(void) {
    return nev_net_core_state(&s_core);
}
