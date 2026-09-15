#include "nev_appkit/shell.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_time.h"
#include <stdio.h>
#include <string.h>

#define TAG             "shell"

#define EVENTS_PER_TICK 8
#define TILE_W          132
#define TILE_H          124

typedef struct {
    const nev_app_desc_t *desc;
    lv_obj_t *root;
    uint32_t last_used_ms;
    bool suspended;
} live_app_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_content;
static lv_obj_t *s_home;

static lv_obj_t *s_time_label;
static lv_obj_t *s_net_label;
static lv_obj_t *s_bridge_label;
static lv_obj_t *s_battery_label;

static live_app_t s_live[NEV_SHELL_MAX_LIVE];
static int s_foreground = -1; /* index into s_live, -1 at home */
static nev_sub_t *s_sub;
static bool s_ready;
static uint32_t s_now_ms;

/* Status state, so the bar can be rebuilt from what was last heard. */
static bool s_wifi_up;
static bool s_bridge_up;
static uint8_t s_battery_pct = 100;
static bool s_charging = true;
static uint32_t s_last_clock_paint_min = 0xFFFFFFFFu;

/* ------------------------------------------------------------- status bar */

static void paint_clock(bool force) {
    if (!s_time_label) return;

    if (!nev_wallclock_is_set()) {
        /* A clock that is confidently wrong is worse than one that admits it
         * does not know. Something authoritative sets this at M6. */
        if (force) lv_label_set_text(s_time_label, "--:--");
        return;
    }
    const uint64_t now = nev_wallclock_now();
    const uint32_t minutes = (uint32_t)((now / 60) % (24 * 60));
    if (!force && minutes == s_last_clock_paint_min) return; /* repaint once a minute */
    s_last_clock_paint_min = minutes;

    uint32_t h = minutes / 60;
    const uint32_t m = minutes % 60;

    if (nev_store_is_ready() && !nev_store_num(NEV_SET_TIME_24H)) {
        const char *suffix = h < 12 ? "AM" : "PM";
        uint32_t h12 = h % 12;
        if (h12 == 0) h12 = 12;
        lv_label_set_text_fmt(s_time_label, "%u:%02u %s", (unsigned)h12, (unsigned)m, suffix);
    } else {
        lv_label_set_text_fmt(s_time_label, "%02u:%02u", (unsigned)h, (unsigned)m);
    }
}

static void paint_status(void) {
    if (!s_status) return;

    lv_label_set_text(s_net_label, s_wifi_up ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_net_label, s_wifi_up ? NEV_COL_INK_MUTED : NEV_COL_INK_FAINT,
                                LV_PART_MAIN);

    /* The agent link gets its own indicator rather than being folded into the
     * Wi-Fi icon: "online but not paired" is a state the user has to be able to
     * see, because it is the one that explains why the agent app is greyed out. */
    lv_label_set_text(s_bridge_label, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_color(s_bridge_label, s_bridge_up ? NEV_COL_SUCCESS : NEV_COL_INK_FAINT,
                                LV_PART_MAIN);

    lv_label_set_text_fmt(s_battery_label, "%s %u%%",
                          s_charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL,
                          (unsigned)s_battery_pct);
    lv_obj_set_style_text_color(
        s_battery_label, (!s_charging && s_battery_pct < 15) ? NEV_COL_DANGER : NEV_COL_INK_MUTED,
        LV_PART_MAIN);
}

static void build_status_bar(void) {
    s_status = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_status);
    lv_obj_set_size(s_status, NEV_SCREEN_W, NEV_STATUS_BAR_H);
    lv_obj_set_pos(s_status, 0, 0);
    lv_obj_set_style_bg_color(s_status, NEV_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_status, NEV_SP_4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_status, NEV_SP_2, LV_PART_MAIN);
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = nev_ui_label(s_status, "--:--", NEV_FONT_BODY, NEV_COL_INK);
    lv_obj_set_flex_grow(s_time_label, 1);

    s_bridge_label = nev_ui_label(s_status, LV_SYMBOL_SHUFFLE, NEV_FONT_BODY, NEV_COL_INK_FAINT);
    s_net_label = nev_ui_label(s_status, LV_SYMBOL_CLOSE, NEV_FONT_BODY, NEV_COL_INK_FAINT);
    s_battery_label = nev_ui_label(s_status, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);

    paint_clock(true);
    paint_status();
}

/* ------------------------------------------------------------- home screen */

static void tile_clicked_cb(lv_event_t *e) {
    const nev_app_desc_t *desc = lv_event_get_user_data(e);
    if (!desc) return;

    if (desc->requires_bridge && !s_bridge_up) {
        /* Say why rather than launching into a dead end. ADR 0008. */
        nev_ui_toast("Needs the companion app", 2200);
        return;
    }
    (void)nev_shell_launch(desc->id);
}

static void build_home(void) {
    s_home = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_home);
    lv_obj_set_size(s_home, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_home, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_home, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_home, NEV_SP_4, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_home, NEV_SP_3, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_home, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_home, LV_SCROLLBAR_MODE_OFF);

    const nev_app_desc_t *ordered[NEV_APP_MAX_ORDERED];
    const size_t n = nev_app_ordered(ordered, NEV_APP_MAX_ORDERED);

    if (n == 0) {
        lv_obj_t *empty =
            nev_ui_label(s_home, "No apps registered", NEV_FONT_BODY, NEV_COL_INK_MUTED);
        lv_obj_center(empty);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        const nev_app_desc_t *d = ordered[i];

        lv_obj_t *tile = lv_obj_create(s_home);
        lv_obj_remove_style_all(tile);
        nev_theme_apply_surface(tile, NEV_RADIUS_LG);
        lv_obj_set_size(tile, TILE_W, TILE_H);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, NEV_SP_2, LV_PART_MAIN);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tile, NEV_COL_SURFACE_ALT, LV_STATE_PRESSED);

        const bool blocked = d->requires_bridge && !s_bridge_up;
        lv_obj_t *icon = nev_ui_label(tile, d->icon ? d->icon : LV_SYMBOL_DIRECTORY, NEV_FONT_TITLE,
                                      blocked ? NEV_COL_INK_FAINT : NEV_COL_ACCENT);
        (void)icon;
        nev_ui_label(tile, d->name, NEV_FONT_BODY, blocked ? NEV_COL_INK_FAINT : NEV_COL_INK);

        lv_obj_add_event_cb(tile, tile_clicked_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)d);
    }
}

/* --------------------------------------------------------------- lifecycle */

static int find_live(const char *id) {
    for (int i = 0; i < NEV_SHELL_MAX_LIVE; i++) {
        if (s_live[i].desc && strcmp(s_live[i].desc->id, id) == 0) return i;
    }
    return -1;
}

static void close_slot(int i) {
    if (i < 0 || i >= NEV_SHELL_MAX_LIVE || !s_live[i].desc) return;
    NEV_LOGI(TAG, "closing '%s'", s_live[i].desc->id);

    if (s_live[i].desc->on_close) s_live[i].desc->on_close();
    if (s_live[i].root) lv_obj_delete(s_live[i].root);
    memset(&s_live[i], 0, sizeof(s_live[i]));
    if (s_foreground == i) s_foreground = -1;
}

/*
 * Evict the least recently used suspended app. Never the foreground one: the
 * user is looking at it.
 */
static int evict_one(void) {
    int victim = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < NEV_SHELL_MAX_LIVE; i++) {
        if (!s_live[i].desc || i == s_foreground) continue;
        if (s_live[i].last_used_ms <= oldest) {
            oldest = s_live[i].last_used_ms;
            victim = i;
        }
    }
    if (victim >= 0) close_slot(victim);
    return victim;
}

static void suspend_foreground(void) {
    if (s_foreground < 0) return;
    live_app_t *a = &s_live[s_foreground];
    if (a->desc->on_suspend) a->desc->on_suspend();
    if (a->root) lv_obj_add_flag(a->root, LV_OBJ_FLAG_HIDDEN);
    a->suspended = true;
    a->last_used_ms = s_now_ms;
    s_foreground = -1;
}

nev_err_t nev_shell_launch(const char *app_id) {
    if (!s_ready) return NEV_ERR_INVALID_STATE;
    const nev_app_desc_t *desc = nev_app_find(app_id);
    if (!desc) {
        NEV_LOGW(TAG, "no app '%s'", app_id ? app_id : "(null)");
        return NEV_ERR_NOT_FOUND;
    }
    if (s_foreground >= 0 && s_live[s_foreground].desc == desc) return NEV_OK;

    suspend_foreground();
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN);

    int slot = find_live(desc->id);
    if (slot >= 0) {
        /* Already warm: show it again rather than rebuilding. That is the whole
         * point of keeping suspended apps around. */
        live_app_t *a = &s_live[slot];
        lv_obj_remove_flag(a->root, LV_OBJ_FLAG_HIDDEN);
        if (a->desc->on_resume) a->desc->on_resume();
        a->suspended = false;
        a->last_used_ms = s_now_ms;
        s_foreground = slot;
        NEV_LOGI(TAG, "resumed '%s'", desc->id);
    } else {
        for (slot = 0; slot < NEV_SHELL_MAX_LIVE && s_live[slot].desc; slot++) {
        }
        if (slot >= NEV_SHELL_MAX_LIVE) {
            slot = evict_one();
            if (slot < 0) {
                lv_obj_remove_flag(s_home, LV_OBJ_FLAG_HIDDEN);
                return NEV_ERR_NO_SPACE;
            }
        }

        lv_obj_t *root = lv_obj_create(s_content);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(root, NEV_COL_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

        s_live[slot].desc = desc;
        s_live[slot].root = root;
        s_live[slot].last_used_ms = s_now_ms;
        s_live[slot].suspended = false;
        s_foreground = slot;

        const nev_err_t rc = desc->on_launch(root);
        if (rc != NEV_OK) {
            NEV_LOGE(TAG, "'%s' failed to launch: %s", desc->id, nev_err_str(rc));
            close_slot(slot);
            lv_obj_remove_flag(s_home, LV_OBJ_FLAG_HIDDEN);
            nev_ui_toast("Could not open that", 2000);
            return rc;
        }
        NEV_LOGI(TAG, "launched '%s'", desc->id);
    }

    nev_event_t ev = nev_event_make(NEV_EVT_APP_LAUNCH, NEV_SRC_APPKIT);
    (void)nev_bus_publish(&ev);
    (void)nev_store_set_str(NEV_SET_LAST_APP, desc->id);
    return NEV_OK;
}

void nev_shell_go_home(void) {
    if (!s_ready) return;
    if (nev_ui_modal_is_open()) {
        nev_ui_modal_close();
        return; /* back dismisses a modal before it leaves the app */
    }
    if (s_foreground < 0) return;

    suspend_foreground();
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_HIDDEN);
    (void)nev_bus_publish_type(NEV_EVT_APP_NAV_HOME, NEV_SRC_APPKIT);
    (void)nev_store_set_str(NEV_SET_LAST_APP, "");
}

const char *nev_shell_foreground_id(void) {
    return s_foreground >= 0 ? s_live[s_foreground].desc->id : NULL;
}

size_t nev_shell_live_count(void) {
    size_t n = 0;
    for (int i = 0; i < NEV_SHELL_MAX_LIVE; i++) {
        if (s_live[i].desc) n++;
    }
    return n;
}

/* -------------------------------------------------------------- navigation */

static void content_gesture_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) nev_shell_go_home();
}

/* ------------------------------------------------------------------- setup */

nev_err_t nev_shell_init(lv_obj_t *screen) {
    if (s_ready) return NEV_OK;
    NEV_REQUIRE(screen != NULL, NEV_ERR_INVALID_ARG);

    s_screen = screen;
    nev_theme_apply_screen(s_screen);

    build_status_bar();

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, NEV_SCREEN_W, NEV_CONTENT_H);
    lv_obj_set_pos(s_content, 0, NEV_STATUS_BAR_H);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_content, content_gesture_cb, LV_EVENT_GESTURE, NULL);

    build_home();

    const nev_sub_cfg_t cfg = {
        .name = "shell",
        .domains = NEV_DOM(INPUT) | NEV_DOM(NET) | NEV_DOM(POWER) | NEV_DOM(BRIDGE) | NEV_DOM(SYS) |
                   NEV_DOM(STORAGE),
        .depth = 12,
        .full_policy = NEV_FULL_DROP_OLDEST,
        .coalesce = true,
    };
    s_sub = nev_bus_subscribe(&cfg);
    if (!s_sub) return NEV_ERR_NO_SPACE;

    s_ready = true;
    NEV_LOGI(TAG, "%u app(s) on the home grid", (unsigned)nev_app_count());

    /* Come back to where the user was. A device that always boots to the home
     * grid makes you re-navigate every time it sleeps. */
    const char *last = nev_store_is_ready() ? nev_store_str(NEV_SET_LAST_APP) : "";
    if (last && *last) {
        if (nev_shell_launch(last) != NEV_OK) (void)nev_store_set_str(NEV_SET_LAST_APP, "");
    }
    return NEV_OK;
}

void nev_shell_deinit(void) {
    if (!s_ready) return;
    for (int i = 0; i < NEV_SHELL_MAX_LIVE; i++)
        close_slot(i);
    s_ready = false;
    s_screen = s_status = s_content = s_home = NULL;
    s_sub = NULL;
    s_foreground = -1;
}

static void handle_event(const nev_event_t *ev) {
    switch (ev->type) {
        case NEV_EVT_INPUT_BUTTON_UP:
            /* Button B is back, everywhere. One consistent escape hatch matters
             * more than giving each app its own idea of what B does. */
            if (ev->p.button.id == 1) nev_shell_go_home();
            break;

        case NEV_EVT_NET_WIFI_UP:
            s_wifi_up = true;
            paint_status();
            break;
        case NEV_EVT_NET_WIFI_DOWN:
            s_wifi_up = false;
            paint_status();
            break;

        case NEV_EVT_BRIDGE_CONNECTED:
        case NEV_EVT_BRIDGE_PAIRED:
            s_bridge_up = true;
            paint_status();
            break;
        case NEV_EVT_BRIDGE_DISCONNECTED:
            s_bridge_up = false;
            paint_status();
            break;

        case NEV_EVT_POWER_BATTERY:
            s_battery_pct = ev->p.battery.percent;
            s_charging = ev->p.battery.charging != 0;
            paint_status();
            break;

        case NEV_EVT_STORAGE_SETTING_CHANGED:
            if (ev->p.u32[0] == NEV_SET_TIME_24H) paint_clock(true);
            break;

        /* Under pressure, give back the memory of an app nobody is looking at. */
        case NEV_EVT_SYS_MEM_PRESSURE:
            if (evict_one() >= 0) NEV_LOGW(TAG, "closed a background app under memory pressure");
            break;

        default:
            break;
    }
}

void nev_shell_tick(uint32_t now_ms) {
    if (!s_ready) return;
    s_now_ms = now_ms;

    nev_event_t ev;
    int budget = EVENTS_PER_TICK;
    while (budget-- > 0 && nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        handle_event(&ev);
        /* The foreground app sees input while it is in front; the shell has
         * already had its say on the back button above. */
        if (s_foreground >= 0 && s_live[s_foreground].desc->on_event) {
            s_live[s_foreground].desc->on_event(&ev);
        }
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }

    paint_clock(false);

    if (s_foreground >= 0) {
        s_live[s_foreground].last_used_ms = now_ms;
        if (s_live[s_foreground].desc->on_tick) s_live[s_foreground].desc->on_tick(now_ms);
    }
}
