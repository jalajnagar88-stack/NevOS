#include "nev_appkit/theme.h"

void nev_theme_apply_screen(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, NEV_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void nev_theme_apply_surface(lv_obj_t *obj, int32_t radius) {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, NEV_COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_color_t nev_theme_play_color(uint8_t index) {
    /*
     * Ordered so that adjacent entries differ in lightness, not only in hue.
     * A board whose pieces are distinguishable only by hue is unplayable for
     * roughly one man in twelve.
     */
    static const uint32_t kPlay[NEV_PLAY_COLORS] = {
        0x4C8DFF, /* blue      */
        0xFFD166, /* sand      */
        0x35D0BA, /* teal      */
        0xF2545B, /* red       */
        0xB07CFF, /* violet    */
        0xE8E8EC, /* near-white*/
    };
    return lv_color_hex(kPlay[index % NEV_PLAY_COLORS]);
}
