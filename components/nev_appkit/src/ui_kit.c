#include "nev_appkit/ui_kit.h"
#include "nev_port/nev_log.h"
#include <stdio.h>

#define TAG   "ui"

#define ROW_H 56

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;
static lv_obj_t *s_modal;

/* ------------------------------------------------------------------ basics */

lv_obj_t *nev_ui_panel(lv_obj_t *parent, int32_t w, int32_t h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    nev_theme_apply_surface(o, NEV_RADIUS_MD);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *nev_ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       lv_color_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font ? font : NEV_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    return l;
}

static lv_obj_t *button_base(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                             void *user_data) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, NEV_TOUCH_MIN);
    lv_obj_set_style_radius(b, NEV_RADIUS_MD, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, NEV_SP_4, LV_PART_MAIN);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, NEV_FONT_BODY, LV_PART_MAIN);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

lv_obj_t *nev_ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data) {
    lv_obj_t *b = button_base(parent, text, cb, user_data);
    lv_obj_set_style_bg_color(b, NEV_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, NEV_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, NEV_COL_BG, LV_PART_MAIN);
    return b;
}

lv_obj_t *nev_ui_button_ghost(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                              void *user_data) {
    lv_obj_t *b = button_base(parent, text, cb, user_data);
    lv_obj_set_style_bg_color(b, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_text_color(b, NEV_COL_INK, LV_PART_MAIN);
    return b;
}

/* -------------------------------------------------------------------- list */

lv_obj_t *nev_ui_list(lv_obj_t *parent) {
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, NEV_SP_2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, NEV_SP_4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, LV_PART_MAIN);
    return l;
}

lv_obj_t *nev_ui_row(lv_obj_t *list, const char *label) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    nev_theme_apply_surface(row, NEV_RADIUS_MD);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_pad_hor(row, NEV_SP_4, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *l = nev_ui_label(row, label, NEV_FONT_BODY, NEV_COL_INK);
    lv_obj_set_flex_grow(l, 1);
    return row;
}

lv_obj_t *nev_ui_row_value(lv_obj_t *row, const char *text) {
    return nev_ui_label(row, text, NEV_FONT_BODY, NEV_COL_INK_MUTED);
}

lv_obj_t *nev_ui_slider_row(lv_obj_t *list, const char *label, int32_t min, int32_t max,
                            int32_t value, lv_event_cb_t cb, void *user_data) {
    /* Two lines: a slider squeezed beside its label on a 480px screen is a
     * slider you cannot aim at. */
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    nev_theme_apply_surface(row, NEV_RADIUS_MD);
    lv_obj_set_size(row, lv_pct(100), ROW_H + NEV_SP_5);
    lv_obj_set_style_pad_all(row, NEV_SP_3, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, NEV_SP_2, LV_PART_MAIN);

    nev_ui_label(row, label, NEV_FONT_BODY, NEV_COL_INK);

    lv_obj_t *s = lv_slider_create(row);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, 10);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, NEV_COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, NEV_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, NEV_COL_INK, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 8, LV_PART_KNOB); /* a knob you can actually hit */
    if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, user_data);
    return s;
}

lv_obj_t *nev_ui_toggle_row(lv_obj_t *list, const char *label, bool on, lv_event_cb_t cb,
                            void *user_data) {
    lv_obj_t *row = nev_ui_row(list, label);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 52, 28);
    lv_obj_set_style_bg_color(sw, NEV_COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, NEV_COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, NEV_COL_INK, LV_PART_KNOB);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);
    return sw;
}

lv_obj_t *nev_ui_action_row(lv_obj_t *list, const char *label, const char *icon, lv_event_cb_t cb,
                            void *user_data) {
    lv_obj_t *row = nev_ui_row(list, label);
    nev_ui_label(row, icon ? icon : LV_SYMBOL_RIGHT, NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, NEV_COL_SURFACE_ALT, LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    return row;
}

/* ---------------------------------------------------------- progress ring */

lv_obj_t *nev_ui_progress_ring(lv_obj_t *parent, int32_t size) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, NEV_COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, NEV_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

void nev_ui_progress_ring_set(lv_obj_t *ring, int32_t percent) {
    if (ring) lv_arc_set_value(ring, percent);
}

/* -------------------------------------------------------------- transient */

static void toast_expire(lv_timer_t *t) {
    (void)t;
    if (s_toast) lv_obj_delete(s_toast);
    s_toast = NULL;
    if (s_toast_timer) lv_timer_delete(s_toast_timer);
    s_toast_timer = NULL;
}

void nev_ui_toast(const char *text, uint32_t ms) {
    /* One at a time. A stack of toasts on a 480px screen is a wall. */
    toast_expire(NULL);

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    nev_theme_apply_surface(s_toast, NEV_RADIUS_FULL);
    lv_obj_set_style_bg_color(s_toast, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_toast, NEV_SP_5, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_toast, NEV_SP_3, LV_PART_MAIN);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_IGNORE_LAYOUT);

    nev_ui_label(s_toast, text, NEV_FONT_BODY, NEV_COL_INK);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -NEV_SP_6);

    s_toast_timer = lv_timer_create(toast_expire, ms ? ms : 2000, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* ------------------------------------------------------------------ modal */

typedef struct {
    lv_event_cb_t on_confirm;
    void *user_data;
} modal_ctx_t;

static modal_ctx_t s_modal_ctx;

static void modal_dismiss_cb(lv_event_t *e) {
    (void)e;
    nev_ui_modal_close();
}

static void modal_confirm_cb(lv_event_t *e) {
    const lv_event_cb_t cb = s_modal_ctx.on_confirm;
    void *ud = s_modal_ctx.user_data;
    nev_ui_modal_close();
    /* Close first: a confirm handler that navigates away should not leave a
     * modal behind it on the top layer. */
    if (cb) {
        lv_obj_set_user_data(lv_event_get_target(e), ud);
        cb(e);
    }
}

void nev_ui_modal(const char *title, const char *body, const char *confirm_text, bool destructive,
                  lv_event_cb_t on_confirm, void *user_data) {
    nev_ui_modal_close();
    s_modal_ctx.on_confirm = on_confirm;
    s_modal_ctx.user_data = user_data;

    /* A full-screen scrim, so nothing behind the modal can be tapped. */
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, NEV_SCREEN_W, NEV_SCREEN_H);
    lv_obj_set_style_bg_color(s_modal, NEV_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_modal);

    lv_obj_t *card = nev_ui_panel(s_modal, 380, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, NEV_SP_5, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, NEV_SP_3, LV_PART_MAIN);
    lv_obj_center(card);

    nev_ui_label(card, title, NEV_FONT_TITLE, NEV_COL_INK);
    lv_obj_t *b = nev_ui_label(card, body, NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, lv_pct(100));

    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, NEV_SP_2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(actions, NEV_SP_2, LV_PART_MAIN);

    nev_ui_button_ghost(actions, "Cancel", modal_dismiss_cb, NULL);
    lv_obj_t *ok =
        nev_ui_button(actions, confirm_text ? confirm_text : "OK", modal_confirm_cb, user_data);
    if (destructive) {
        lv_obj_set_style_bg_color(ok, NEV_COL_DANGER, LV_PART_MAIN);
        lv_obj_set_style_text_color(ok, NEV_COL_INK, LV_PART_MAIN);
    }
}

void nev_ui_modal_close(void) {
    if (s_modal) lv_obj_delete(s_modal);
    s_modal = NULL;
}

bool nev_ui_modal_is_open(void) {
    return s_modal != NULL;
}
