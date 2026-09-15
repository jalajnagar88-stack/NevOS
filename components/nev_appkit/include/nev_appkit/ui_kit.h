/*
 * NEVOS L4 — the shared widget set.
 *
 * Every app builds from these. An app that defines its own colours, its own row
 * heights and its own button shape produces a device that looks like twelve
 * different devices, so the tokens in theme.h are the only palette and these
 * are the only primitives.
 *
 * Everything here returns a plain lv_obj_t*, so an app is never boxed in: the
 * kit sets up the common case and normal LVGL calls handle the rest.
 */
#ifndef NEV_APPKIT_UI_KIT_H
#define NEV_APPKIT_UI_KIT_H

#include "nev_appkit/theme.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A surface panel. */
lv_obj_t *nev_ui_panel(lv_obj_t *parent, int32_t w, int32_t h);

lv_obj_t *nev_ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);

/* A filled button at the minimum comfortable touch size. */
lv_obj_t *nev_ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data);
/* Quieter: outline only. For secondary and destructive-adjacent actions. */
lv_obj_t *nev_ui_button_ghost(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                              void *user_data);

/* A vertical scrolling list to hang rows on. */
lv_obj_t *nev_ui_list(lv_obj_t *parent);

/* A row with a label; returns the row so a trailing control can be added. */
lv_obj_t *nev_ui_row(lv_obj_t *list, const char *label);
/* A right-aligned value on an existing row. */
lv_obj_t *nev_ui_row_value(lv_obj_t *row, const char *text);

/*
 * Rows that own a control. The callback receives LV_EVENT_VALUE_CHANGED; read
 * the value from lv_event_get_target().
 */
lv_obj_t *nev_ui_slider_row(lv_obj_t *list, const char *label, int32_t min, int32_t max,
                            int32_t value, lv_event_cb_t cb, void *user_data);
lv_obj_t *nev_ui_toggle_row(lv_obj_t *list, const char *label, bool on, lv_event_cb_t cb,
                            void *user_data);
lv_obj_t *nev_ui_action_row(lv_obj_t *list, const char *label, const char *icon, lv_event_cb_t cb,
                            void *user_data);

/* Progress ring, 0..100. */
lv_obj_t *nev_ui_progress_ring(lv_obj_t *parent, int32_t size);
void nev_ui_progress_ring_set(lv_obj_t *ring, int32_t percent);

/*
 * A transient message. Lives on LVGL's top layer, so it floats over whatever
 * app is running and needs no cooperation from it. Only one at a time: a stack
 * of toasts on a 480px screen is a wall.
 */
void nev_ui_toast(const char *text, uint32_t ms);

/*
 * A blocking confirmation. `on_confirm` fires only on the confirm button; the
 * modal closes itself either way. `destructive` colours the confirm button as
 * a warning, for things like factory reset.
 */
void nev_ui_modal(const char *title, const char *body, const char *confirm_text, bool destructive,
                  lv_event_cb_t on_confirm, void *user_data);
void nev_ui_modal_close(void);
bool nev_ui_modal_is_open(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_APPKIT_UI_KIT_H */
