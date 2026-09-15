/*
 * NEVOS L4 — theme tokens.
 *
 * THE ONLY PLACE IN NEVOS THAT MAY CONTAIN A COLOUR LITERAL.
 * tools/ci/lint_layers.py fails the build on a hex colour found under apps/.
 *
 * Tokens are named for their role, not their value — NEV_COL_DANGER, not
 * NEV_COL_RED — so that restyling the device is editing this file rather than
 * grepping for a shade of blue across twelve apps.
 */
#ifndef NEV_APPKIT_THEME_H
#define NEV_APPKIT_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ colour */
/* A dark ground is not a style choice: this thing sits on a desk in the corner
 * of your eye all day, and a bright 480x480 panel there is a lamp. */
#define NEV_COL_BG          lv_color_hex(0x0B0E14)
#define NEV_COL_SURFACE     lv_color_hex(0x141A23)
#define NEV_COL_SURFACE_ALT lv_color_hex(0x1D2633)
#define NEV_COL_LINE        lv_color_hex(0x263041)

#define NEV_COL_INK         lv_color_hex(0xE6EDF3)
#define NEV_COL_INK_MUTED   lv_color_hex(0x8B949E)
#define NEV_COL_INK_FAINT   lv_color_hex(0x5A6472)

#define NEV_COL_ACCENT      lv_color_hex(0x4C8DFF)
#define NEV_COL_SUCCESS     lv_color_hex(0x35D0BA)
#define NEV_COL_WARN        lv_color_hex(0xE8A33D)
#define NEV_COL_DANGER      lv_color_hex(0xF2545B)

/* ----------------------------------------------------------------- spacing */
/* A 4px base step. Every gap in NEVOS is one of these; arbitrary padding is
 * how a UI stops looking like one thing. */
#define NEV_SP_1            4
#define NEV_SP_2            8
#define NEV_SP_3            12
#define NEV_SP_4            16
#define NEV_SP_5            24
#define NEV_SP_6            32
#define NEV_SP_7            48

/* ------------------------------------------------------------------ radius */
#define NEV_RADIUS_SM       6
#define NEV_RADIUS_MD       12
#define NEV_RADIUS_LG       20
#define NEV_RADIUS_FULL     LV_RADIUS_CIRCLE

/* -------------------------------------------------------------- type scale */
#define NEV_FONT_DISPLAY    (&lv_font_montserrat_48)
#define NEV_FONT_TITLE      (&lv_font_montserrat_28)
#define NEV_FONT_BODY       (&lv_font_montserrat_14)
#define NEV_FONT_CAPTION    (&lv_font_montserrat_14)

/* ------------------------------------------------------------------ layout */
#define NEV_STATUS_BAR_H    34
#define NEV_SCREEN_W        480
#define NEV_SCREEN_H        480
#define NEV_CONTENT_H       (NEV_SCREEN_H - NEV_STATUS_BAR_H)

/* A touch target smaller than this is a target you miss. */
#define NEV_TOUCH_MIN       44

/* ------------------------------------------------------------------ motion */
#define NEV_ANIM_FAST       140
#define NEV_ANIM_NORMAL     220
#define NEV_ANIM_SLOW       360

/* Applies the ground colour and removes scrolling from a full-screen object. */
void nev_theme_apply_screen(lv_obj_t *obj);

/* A surface panel: background, radius, no border, no scroll. */
void nev_theme_apply_surface(lv_obj_t *obj, int32_t radius);

#ifdef __cplusplus
}
#endif
#endif /* NEV_APPKIT_THEME_H */
