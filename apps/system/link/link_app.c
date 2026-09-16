/*
 * NEVOS — link.
 *
 * Everything about the companion daemon that the user has to see: whether it is
 * connected, which machine it is, and the six digits to type when pairing.
 *
 * Pairing lives here rather than inside the agent app because it is a property
 * of the device, not of one feature. Two apps both explaining how to pair would
 * be two places to keep in step, and the first time they disagreed the user
 * would be the one to find out.
 */
#include <stdio.h>
#include <string.h>

#include "nev_appkit/app.h"
#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_version.h"

static lv_obj_t *s_status;
static lv_obj_t *s_detail;
static lv_obj_t *s_code;
static lv_obj_t *s_hint;
static lv_obj_t *s_device;
static nev_bridge_state_t s_painted = (nev_bridge_state_t)0xFF;
static char s_painted_code[8];

static void repaint(void) {
    const nev_bridge_state_t state = nev_bridge_state();
    const char *code = nev_bridge_pairing_code();

    lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
        case NEV_BRIDGE_OFFLINE:
            lv_label_set_text(s_status, "Not connected");
            lv_obj_set_style_text_color(s_status, NEV_COL_INK_MUTED, 0);
            /* Said plainly, because it is true and it is the whole point of
             * ADR 0008: nothing else on this device stops working. */
            lv_label_set_text(s_detail, "Everything else works without it.");
            break;

        case NEV_BRIDGE_SEARCHING:
            lv_label_set_text(s_status, "Looking for a computer");
            lv_obj_set_style_text_color(s_status, NEV_COL_INK_MUTED, 0);
            lv_label_set_text(s_detail, "Start NEVOS on your computer.");
            break;

        case NEV_BRIDGE_CONNECTING:
            lv_label_set_text(s_status, "Connecting");
            lv_obj_set_style_text_color(s_status, NEV_COL_WARN, 0);
            lv_label_set_text_fmt(s_detail, "%s", nev_bridge_daemon_name());
            break;

        case NEV_BRIDGE_PAIRING:
            lv_label_set_text(s_status, "Pair with");
            lv_obj_set_style_text_color(s_status, NEV_COL_ACCENT, 0);
            lv_label_set_text_fmt(s_detail, "%s", nev_bridge_daemon_name());
            if (code) {
                lv_label_set_text(s_code, code);
                lv_obj_remove_flag(s_code, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
            }
            break;

        case NEV_BRIDGE_READY:
            lv_label_set_text(s_status, "Connected");
            lv_obj_set_style_text_color(s_status, NEV_COL_SUCCESS, 0);
            lv_label_set_text_fmt(s_detail, "%s", nev_bridge_daemon_name());
            break;
    }
}

static nev_err_t link_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, NEV_SP_3, 0);

    s_status = nev_ui_label(root, "", NEV_FONT_TITLE, NEV_COL_INK);
    s_detail = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);

    /*
     * The code is the largest thing on the screen on purpose: it is read from
     * across a desk, typed on a keyboard that is not here, and it expires.
     */
    s_code = nev_ui_label(root, "000000", NEV_FONT_DISPLAY, NEV_COL_ACCENT);
    lv_obj_set_style_pad_top(s_code, NEV_SP_4, 0);
    lv_obj_set_style_pad_bottom(s_code, NEV_SP_2, 0);

    s_hint = nev_ui_label(root, "Type this on your computer", NEV_FONT_BODY, NEV_COL_INK_FAINT);

    s_device = nev_ui_label(root, "", NEV_FONT_CAPTION, NEV_COL_INK_FAINT);
    lv_label_set_text_fmt(s_device, "%s   NEVOS %s", nev_bridge_device_id(), NEVOS_VERSION);
    lv_obj_set_style_pad_top(s_device, NEV_SP_5, 0);

    s_painted = (nev_bridge_state_t)0xFF;
    s_painted_code[0] = '\0';
    repaint();
    return NEV_OK;
}

static void link_tick(uint32_t now_ms) {
    (void)now_ms;
    /* Repaint on change only. The state is a poll rather than an event because
     * the bridge runs on another task and this app is on the render task; a
     * comparison of two enums per frame is cheaper than a subscription. */
    const nev_bridge_state_t state = nev_bridge_state();
    const char *code = nev_bridge_pairing_code();
    const bool code_changed =
        (code == NULL) ? (s_painted_code[0] != '\0') : (strcmp(code, s_painted_code) != 0);

    if (state == s_painted && !code_changed) return;
    s_painted = state;
    snprintf(s_painted_code, sizeof(s_painted_code), "%s", code ? code : "");
    repaint();
}

static void link_close(void) {
    s_status = s_detail = s_code = s_hint = s_device = NULL;
}

static const nev_app_desc_t kLinkApp = {
    .id = "link",
    .name = "Computer",
    .icon = LV_SYMBOL_WIFI,
    .category = NEV_APP_CAT_SYSTEM,
    .memory_budget_kb = 8,
    /* Not marked as requiring the bridge: this is the screen you open *because*
     * the bridge is not connected. */
    .requires_bridge = false,
    .on_launch = link_launch,
    .on_tick = link_tick,
    .on_close = link_close,
};

NEV_APP_REGISTER(kLinkApp);
