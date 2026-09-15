/*
 * Design B — "character": eyes with pupils, brows, and a mouth.
 *
 * The literal reading. It costs more primitives and more places to get the
 * proportions wrong, but it buys a wider emotional vocabulary: surprise,
 * worry and scepticism all live in the brows, and this is the only one of the
 * three designs that has them.
 */
#include "face_common.h"
#include <math.h>

#define EYE_W   104
#define EYE_H   118
#define EYE_GAP 92
#define EYE_Y   212
#define BROW_W  92
#define BROW_H  13
#define MOUTH_Y 338
#define MOUTH_R 72

typedef struct {
    lv_obj_t *sclera;
    lv_obj_t *pupil;
    lv_obj_t *lid;
    lv_obj_t *brow;
    lv_obj_t *blush;
} side_t;

static lv_obj_t *s_root;
static side_t s_side[2];
static lv_obj_t *s_mouth;

static lv_obj_t *make_block(lv_obj_t *parent, lv_color_t color, int32_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static nev_err_t full_create(lv_obj_t *parent) {
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_center(s_root);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 2; i++) {
        s_side[i].blush = make_block(s_root, FACE_BLUSH, LV_RADIUS_CIRCLE);
        s_side[i].sclera = make_block(s_root, FACE_SCLERA, LV_RADIUS_CIRCLE);
        s_side[i].pupil = make_block(s_root, FACE_PUPIL, LV_RADIUS_CIRCLE);
        s_side[i].lid = make_block(s_root, FACE_BG, 20);
        s_side[i].brow = make_block(s_root, FACE_SCLERA, BROW_H / 2);
    }

    s_mouth = lv_arc_create(s_root);
    lv_obj_remove_style(s_mouth, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(s_mouth, LV_OPA_TRANSP, LV_PART_MAIN); /* hide the track */
    lv_obj_set_style_arc_rounded(s_mouth, true, LV_PART_INDICATOR);
    return NEV_OK;
}

static void full_destroy(void) {
    if (s_root) lv_obj_delete(s_root);
    s_root = NULL;
    s_mouth = NULL;
    lv_memzero(s_side, sizeof(s_side));
}

static void full_apply(const nev_face_params_t *p) {
    if (!s_root) return;

    const lv_color_t accent = face_accent(p);
    const float open = face_clampf(p->eye_open, 0.0f, 1.0f);
    const int32_t ox = (int32_t)(p->head_x * 34.0f);
    const int32_t oy = (int32_t)(p->head_y * 34.0f);
    const int32_t tilt = (int32_t)(p->head_tilt * 10.0f);

    const int32_t w = (int32_t)(EYE_W * face_clampf(p->eye_scale, 0.4f, 1.8f));
    const int32_t h = (int32_t)(EYE_H * face_clampf(p->eye_scale, 0.4f, 1.8f));
    const int32_t gap = (int32_t)(EYE_GAP * face_clampf(p->eye_spread, 0.5f, 1.6f));

    for (int i = 0; i < 2; i++) {
        const int sign = (i == 0) ? -1 : 1;
        const int32_t ex = FACE_CX + sign * gap - w / 2 + ox;
        const int32_t ey = EYE_Y - h / 2 + oy;

        lv_obj_set_size(s_side[i].sclera, w, h);
        lv_obj_set_pos(s_side[i].sclera, ex, ey);

        /* Pupil size is dilation; it is the cheapest cue for alertness. */
        const int32_t pd = (int32_t)(w * 0.44f * face_clampf(p->pupil_size, 0.4f, 1.6f));
        lv_obj_set_size(s_side[i].pupil, pd, pd);
        lv_obj_set_pos(s_side[i].pupil,
                       ex + w / 2 - pd / 2 + (int32_t)(p->pupil_x * (w - pd) * 0.5f),
                       ey + h / 2 - pd / 2 + (int32_t)(p->pupil_y * (h - pd) * 0.5f));

        /* Lid closes from the top and carries the slant, so a lowered outer
         * corner reads as sadness and a lowered inner corner as sternness. */
        const int32_t cover = (int32_t)(h * (1.0f - open)) + 6;
        lv_obj_set_size(s_side[i].lid, w + 24, cover);
        lv_obj_set_pos(s_side[i].lid, ex - 12, ey - 6);
        lv_obj_set_style_transform_pivot_x(s_side[i].lid, (w + 24) / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(s_side[i].lid, cover, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(
            s_side[i].lid, ((int32_t)(p->eye_slant * 18.0f) * sign * -1 + tilt) * 10, LV_PART_MAIN);

        /* Inner-end raised is the classic worry cue; it does most of the work
         * in CONCERNED and is the reason this design has brows at all. */
        const int32_t bw = (int32_t)(BROW_W * face_clampf(p->eye_scale, 0.5f, 1.5f));
        const int32_t by = ey - 34 - (int32_t)(p->brow_raise * 22.0f);
        lv_obj_set_size(s_side[i].brow, bw, BROW_H);
        lv_obj_set_pos(s_side[i].brow, ex + w / 2 - bw / 2, by);
        lv_obj_set_style_bg_color(s_side[i].brow, accent, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(s_side[i].brow, bw / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(s_side[i].brow, BROW_H / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(
            s_side[i].brow, ((int32_t)(p->brow_angle * 20.0f) * sign + tilt) * 10, LV_PART_MAIN);

        const int32_t bd = 54;
        lv_obj_set_size(s_side[i].blush, bd, (int32_t)(bd * 0.6f));
        lv_obj_set_pos(s_side[i].blush, ex + w / 2 - bd / 2 + sign * 28, ey + h + 22);
        lv_obj_set_style_bg_opa(s_side[i].blush, face_opa(p->blush * 0.8f), LV_PART_MAIN);
    }

    /*
     * The mouth is one arc whose centre moves above or below the mouth line, so
     * that the visible portion bulges the right way. A smile and a frown are
     * then the same primitive with different numbers, which is what lets the
     * tween pass smoothly through a flat mouth instead of popping.
     */
    const float curve = face_clampf(p->mouth_curve, -1.0f, 1.0f);
    const int32_t r = (int32_t)(MOUTH_R * face_clampf(p->mouth_scale, 0.5f, 1.6f));
    const int32_t span = 34 + (int32_t)(fabsf(curve) * 62.0f);
    const int32_t mouth_y = MOUTH_Y + oy;

    lv_obj_set_size(s_mouth, r * 2, r * 2);
    lv_obj_set_style_arc_width(s_mouth, 10 + (int32_t)(p->mouth_open * 20.0f), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_mouth, accent, LV_PART_INDICATOR);

    if (curve >= 0.0f) {
        lv_obj_set_pos(s_mouth, FACE_CX - r + ox, mouth_y - r - (int32_t)(r * 0.45f));
        lv_arc_set_angles(s_mouth, (lv_value_precise_t)(90 - span),
                          (lv_value_precise_t)(90 + span));
    } else {
        lv_obj_set_pos(s_mouth, FACE_CX - r + ox, mouth_y - r + (int32_t)(r * 0.45f));
        lv_arc_set_angles(s_mouth, (lv_value_precise_t)(270 - span),
                          (lv_value_precise_t)(270 + span));
    }
    lv_obj_set_style_opa(s_mouth, face_opa(0.35f + 0.65f * face_clampf(p->glow, 0, 1)),
                         LV_PART_MAIN);
}

const nev_face_renderer_t nev_face_renderer_full = {
    .id = "character",
    .name = "Character face",
    .summary = "Eyes, pupils, brows and a mouth. The widest emotional range.",
    .create = full_create,
    .destroy = full_destroy,
    .apply = full_apply,
};
