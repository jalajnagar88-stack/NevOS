/*
 * NEVOS — Wi-Fi.
 *
 * Type a network name and a password, and the radio does the rest.
 *
 * An on-screen keyboard on a 480x480 panel is not a nice way to enter a 63
 * character passphrase, and nobody should pretend otherwise. It is here anyway,
 * because every alternative is worse for the first five minutes a person owns
 * the device: a phone app is a second thing to install before the first thing
 * works, and SoftAP provisioning means joining a network called NEVOS-A3F2 from
 * a laptop and finding a captive portal, which is a support burden with no
 * upside on a device that is already sitting in front of you with a screen and
 * a touch panel.
 *
 * So: typing works, always, with nothing else installed. Provisioning from the
 * companion daemon over the paired link is the better path for a long password
 * and it is a protocol change — it belongs next to the OTA download, which has
 * the same shape and the same reason for not being here yet.
 *
 * Nothing in this file talks to a radio. It writes two settings; net_service
 * notices STORAGE.SETTING_CHANGED and decides what to do about it, which is why
 * this screen works identically on a simulator with no radio at all.
 */
#include <string.h>

#include "nev_appkit/app.h"
#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_store.h"
#include "nev_services/net_service.h"

static lv_obj_t *s_ssid;
static lv_obj_t *s_pass;
static lv_obj_t *s_status;
static lv_obj_t *s_keyboard;
static nev_net_state_t s_painted = (nev_net_state_t)0xFF;

/* Plain words for a state. "BACKOFF" is true and tells a person nothing. */
static const char *state_words(nev_net_state_t state, lv_color_t *colour) {
    switch (state) {
        case NEV_NET_CONNECTING:
            *colour = NEV_COL_INK_MUTED;
            return "Connecting";
        case NEV_NET_WAITING_IP:
            /* Named precisely: this is the state a captive portal parks you in,
             * and "connecting" for thirty seconds tells you nothing about it. */
            *colour = NEV_COL_INK_MUTED;
            return "Joined. Waiting for an address";
        case NEV_NET_ONLINE:
            *colour = NEV_COL_SUCCESS;
            return "Online";
        case NEV_NET_BACKOFF:
            *colour = NEV_COL_WARN;
            return "That did not work. Trying again shortly.";
        case NEV_NET_IDLE:
        default:
            *colour = NEV_COL_INK_FAINT;
            return "No network set";
    }
}

static void paint_status(bool force) {
    const nev_net_state_t state = net_service_state();
    if (!force && state == s_painted) return;
    s_painted = state;

    lv_color_t colour;
    const char *words = state_words(state, &colour);
    lv_label_set_text(s_status, words);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

static void keyboard_show(lv_obj_t *target) {
    lv_keyboard_set_textarea(s_keyboard, target);
    lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_hide(void) {
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* The keyboard's own tick and cross. Without these the only way off the
 * keyboard is tapping a field behind it, and half of it is behind the
 * keyboard. */
static void keyboard_cb(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) keyboard_hide();
}

static void field_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    switch (lv_event_get_code(e)) {
        case LV_EVENT_FOCUSED:
        case LV_EVENT_CLICKED:
            keyboard_show(ta);
            break;
        case LV_EVENT_DEFOCUSED:
            keyboard_hide();
            break;
        default:
            break;
    }
}

/*
 * Saving is what starts a connection, because net_service watches the store.
 *
 * Written in this order on purpose: the password first, then the SSID. Both
 * writes publish STORAGE.SETTING_CHANGED, and net_service reads both settings
 * when it sees one — so writing the SSID first means there is a moment where
 * the service can decide it has credentials and try to associate with the old
 * password, fail, and start backing off before the new one lands.
 */
static void save_and_connect(lv_event_t *e) {
    (void)e;
    keyboard_hide();

    const char *ssid = lv_textarea_get_text(s_ssid);
    const char *pass = lv_textarea_get_text(s_pass);

    if (nev_store_set_str(NEV_SET_WIFI_PASS, pass) != NEV_OK) {
        /* Refused rather than truncated, which is the store doing its job: a
         * key silently cut to 63 characters fails to associate forever. */
        nev_ui_toast_alert("That password is too long to store", 3000);
        return;
    }
    if (nev_store_set_str(NEV_SET_WIFI_SSID, ssid) != NEV_OK) {
        nev_ui_toast_alert("That network name is too long", 3000);
        return;
    }
    (void)nev_store_commit();

    if (ssid[0] == '\0') {
        nev_ui_toast("Network cleared", 2000);
    } else {
        nev_ui_toast("Saved. Connecting...", 2500);
    }
    paint_status(true);
}

static void forget_confirmed(lv_event_t *e) {
    (void)e;
    (void)nev_store_set_str(NEV_SET_WIFI_SSID, "");
    (void)nev_store_set_str(NEV_SET_WIFI_PASS, "");
    (void)nev_store_commit();
    lv_textarea_set_text(s_ssid, "");
    lv_textarea_set_text(s_pass, "");
    paint_status(true);
    nev_ui_toast("Network forgotten", 2000);
}

static void forget_cb(lv_event_t *e) {
    (void)e;
    nev_ui_modal("Forget this network", "The device will stop connecting to it.", "Forget", true,
                 forget_confirmed, NULL);
}

static lv_obj_t *field(lv_obj_t *parent, const char *label, bool secret, const char *value,
                       uint32_t max_chars) {
    nev_ui_label(parent, label, NEV_FONT_CAPTION, NEV_COL_INK_FAINT);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, NEV_TOUCH_MIN);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_chars);
    lv_textarea_set_password_mode(ta, secret);
    lv_textarea_set_text(ta, value);
    lv_obj_set_style_bg_color(ta, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, NEV_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, NEV_COL_INK, LV_PART_MAIN);
    lv_obj_add_event_cb(ta, field_cb, LV_EVENT_ALL, NULL);
    return ta;
}

static nev_err_t wifi_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, NEV_SP_4, 0);
    lv_obj_set_style_pad_row(root, NEV_SP_2, 0);

    s_ssid = field(root, "Network", false, nev_store_str(NEV_SET_WIFI_SSID), 32);
    /*
     * The stored password is never put back on screen, not even as dots that
     * would reveal its length. A device on a desk is looked at by whoever walks
     * past it, and there is no reason for the shoulder-surfing case to be
     * possible at all: leaving the field empty and re-typing is a small cost
     * paid once, by the owner.
     */
    s_pass = field(root, "Password", true, "", 64);

    s_status = nev_ui_label(root, "", NEV_FONT_CAPTION, NEV_COL_INK_FAINT);

    lv_obj_t *connect = nev_ui_button(root, "Save and connect", save_and_connect, NULL);
    lv_obj_set_width(connect, LV_PCT(100));

    lv_obj_t *forget = nev_ui_button_ghost(root, "Forget this network", forget_cb, NULL);
    lv_obj_set_width(forget, LV_PCT(100));

    /*
     * On the top layer rather than in the app's own tree: the keyboard is half
     * the screen and would otherwise push the fields it is meant to be filling
     * off the bottom of a flex column.
     */
    s_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_keyboard, LV_PCT(100), LV_PCT(45));
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_ALL, NULL);

    /*
     * Dressed in the device's own tokens. LVGL's keyboard arrives in the
     * default light theme, and a white keyboard sliding up over a black screen
     * is the single most obvious way to make one device look like two.
     */
    lv_obj_set_style_bg_color(s_keyboard, NEV_COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_keyboard, NEV_SP_1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_keyboard, NEV_COL_SURFACE_ALT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, NEV_COL_INK, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, NEV_RADIUS_SM, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keyboard, NEV_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    /* The control keys — shift, backspace, the mode switches, the tick — carry
     * LV_BUTTONMATRIX_CTRL_CHECKED, so LVGL styles them from the checked state
     * and they stay in the default theme unless this is said as well. */
    lv_obj_set_style_bg_color(s_keyboard, NEV_COL_SURFACE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_keyboard, NEV_COL_INK_MUTED, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(s_keyboard, 0, LV_PART_ITEMS | LV_STATE_CHECKED);

    s_painted = (nev_net_state_t)0xFF;
    paint_status(true);
    return NEV_OK;
}

static void wifi_tick(uint32_t now_ms) {
    (void)now_ms;
    /* The radio's state is polled, not pushed: NET events say up and down, and
     * the interesting part of this screen is the two states in between. */
    paint_status(false);
}

static void wifi_close(void) {
    /* The keyboard lives on the top layer, which the shell does not clean up
     * with the app's root. Leaving it there would put a keyboard over the home
     * grid, which is exactly the bug that made it worth writing down. */
    if (s_keyboard) lv_obj_delete(s_keyboard);
    s_ssid = s_pass = s_status = s_keyboard = NULL;
}

static const nev_app_desc_t kWifiApp = {
    .id = "wifi",
    .name = "Wi-Fi",
    .icon = LV_SYMBOL_WIFI,
    .category = NEV_APP_CAT_SYSTEM,
    .memory_budget_kb = 28,
    .requires_bridge = false,
    .on_launch = wifi_launch,
    .on_tick = wifi_tick,
    .on_close = wifi_close,
};

NEV_APP_REGISTER(kWifiApp);
