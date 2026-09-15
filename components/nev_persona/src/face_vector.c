/*
 * Design A — "vector": two eyes, no mouth, no brows.
 *
 * Expression comes entirely from the shape of two rounded rectangles: how open
 * they are, which edge the lids close from, their tilt, and a highlight that
 * carries gaze. Anki's Cozmo and Vector are the reference point for why this
 * works — a shape that is clearly not a face reads as more alive than a
 * literal one, because the viewer does the work.
 *
 * The idiom's one demand: with no mouth, positive mouth_curve has to land
 * somewhere. It closes the lids from the BOTTOM instead of the top, turning
 * the eye into an upward crescent. That single mapping is what makes happiness
 * legible in a design that has no mouth to smile with.
 */
#include "face_common.h"

#define EYE_W      126
#define EYE_H      156
#define EYE_GAP    104
#define EYE_RADIUS 44

typedef struct {
    lv_obj_t *eye; /* open state: rounded rect with lids   */
    lv_obj_t *lid_top;
    lv_obj_t *lid_bottom;
    lv_obj_t *glint;
    lv_obj_t *squint; /* happy state: an upward arc           */
} eye_t;

static lv_obj_t *s_root;
static eye_t s_eye[2];

static lv_obj_t *make_block(lv_obj_t *parent, lv_color_t color, int32_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static nev_err_t vector_create(lv_obj_t *parent) {
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_center(s_root);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 2; i++) {
        s_eye[i].eye = make_block(s_root, FACE_CALM, EYE_RADIUS);

        /*
         * Lids are background-coloured shapes that slide over the eye, which is
         * cheaper and sharper than redrawing a clipped path and tweens exactly.
         *
         * Two things about them matter.
         *
         * They are wide, fully-rounded ellipses rather than rectangles: a
         * rectangle carves a straight edge with hard corners, turning the happy
         * eye into a flat-bottomed square instead of a crescent, whereas an
         * ellipse far wider than the eye presents a shallow arc where it meets
         * the eye, so closure reads as a curve from either direction.
         *
         * And they are CHILDREN of the eye, clipped to it. The first version
         * made them siblings and rotated each one separately to follow the
         * slant, which forced LVGL to allocate a transform layer the size of
         * each ellipse — larger than LV_MEM_SIZE, which sent it into a
         * pathological subdivision path that took minutes per frame. As
         * children they inherit the eye's rotation for free and the only
         * transform layer is eye-sized.
         */
        s_eye[i].lid_top = make_block(s_eye[i].eye, FACE_BG, LV_RADIUS_CIRCLE);
        s_eye[i].lid_bottom = make_block(s_eye[i].eye, FACE_BG, LV_RADIUS_CIRCLE);
        lv_obj_set_style_clip_corner(s_eye[i].eye, true, LV_PART_MAIN);

        s_eye[i].glint = make_block(s_eye[i].eye, FACE_SCLERA, 12);
        lv_obj_set_style_bg_opa(s_eye[i].glint, LV_OPA_40, LV_PART_MAIN);

        /*
         * The happy eye is a separate primitive, cross-faded with the rect.
         *
         * Subtracting a lid from a rounded rectangle can only ever cut the
         * bottom edge upward, which leaves a band that is THIN in the middle
         * and thick at the sides — a moustache, not a squint. A happy eye is an
         * upward arc, and an arc is what LVGL draws when you ask for one. The
         * two shapes cross-fade over a narrow band of closure, so the
         * transition still tweens rather than popping.
         */
        s_eye[i].squint = lv_arc_create(s_root);
        lv_obj_remove_style(s_eye[i].squint, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(s_eye[i].squint, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_opa(s_eye[i].squint, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_eye[i].squint, true, LV_PART_INDICATOR);
    }
    return NEV_OK;
}

static void vector_destroy(void) {
    if (s_root) lv_obj_delete(s_root);
    s_root = NULL;
    lv_memzero(s_eye, sizeof(s_eye));
}

static void vector_apply(const nev_face_params_t *p) {
    if (!s_root) return;

    const lv_color_t accent = face_accent(p);
    const float open = face_clampf(p->eye_open, 0.0f, 1.0f);
    const float closure = 1.0f - open;

    /* With no mouth, a smile has to be carried by the lids. */
    const float bottom_bias = face_clampf(p->mouth_curve, 0.0f, 1.0f);
    const int32_t w = (int32_t)(EYE_W * face_clampf(p->eye_scale, 0.4f, 1.8f));
    const int32_t h = (int32_t)(EYE_H * face_clampf(p->eye_scale, 0.4f, 1.8f));
    const int32_t gap = (int32_t)(EYE_GAP * face_clampf(p->eye_spread, 0.5f, 1.6f));

    const int32_t ox = (int32_t)(p->head_x * 40.0f);
    const int32_t oy = (int32_t)(p->head_y * 40.0f);
    const int32_t tilt_deg = (int32_t)(p->head_tilt * 12.0f);

    for (int i = 0; i < 2; i++) {
        const int sign = (i == 0) ? -1 : 1;
        const int32_t ex = FACE_CX + sign * gap - w / 2 + ox;
        const int32_t ey = FACE_CY - h / 2 + oy;

        lv_obj_set_size(s_eye[i].eye, w, h);
        lv_obj_set_pos(s_eye[i].eye, ex, ey);
        lv_obj_set_style_bg_color(s_eye[i].eye, accent, LV_PART_MAIN);

        /* Slant tilts each eye's inner corner; mirrored so the pair reads as one
         * expression rather than two independent shapes. The range is wider than
         * the other designs use because in a face with no mouth and no brows,
         * this is the only cue sadness and sternness have. */
        const int32_t slant = (int32_t)(p->eye_slant * 24.0f) * sign * -1;
        lv_obj_set_style_transform_pivot_x(s_eye[i].eye, w / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(s_eye[i].eye, h / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(s_eye[i].eye, (slant + tilt_deg) * 10, LV_PART_MAIN);

        /*
         * No shadow on the eye. A bloom traces the eye's FULL rounded-rect
         * silhouette, including the part a lid has carved away, so it ghosts an
         * outline around a shape that is no longer there — clearly visible as a
         * faint rectangle around the squinting eyes. Glow for this design has to
         * come from a separate object shaped like the visible eye, which is
         * follow-up work if this direction is chosen.
         */

        /* Lid geometry is in eye-local coordinates; the eye's own rotation
         * carries them, so neither lid sets a transform of its own. */
        const int32_t top_cover = (int32_t)(h * closure * (1.0f - bottom_bias));
        const int32_t bot_cover = (int32_t)(h * closure * bottom_bias);

        /*
         * The lid must be a CIRCLE, not a wide rounded rectangle. LVGL clamps
         * LV_RADIUS_CIRCLE to half the shorter side, so a 2.4:1 rectangle is a
         * stadium: rounded ends with a flat edge between them, and the eye only
         * ever sees the flat part.
         */
        /*
         * The two lids want different curvatures. The upper lid is a soft line
         * across the whole eye, so it needs a large, gentle circle — a tight one
         * only covers the middle and cuts a V-notch instead of a lid. The lower
         * lid only handles mild closure now that the happy squint is an arc, so
         * it can be gentle too.
         */
        const int32_t ld_top = (int32_t)(w * 2.4f);
        const int32_t ld_bot = (int32_t)(w * 2.0f);

        lv_obj_set_size(s_eye[i].lid_top, ld_top, ld_top);
        lv_obj_set_pos(s_eye[i].lid_top, w / 2 - ld_top / 2, top_cover - ld_top);
        lv_obj_set_style_opa(s_eye[i].lid_top, top_cover > 0 ? LV_OPA_COVER : LV_OPA_TRANSP,
                             LV_PART_MAIN);

        lv_obj_set_size(s_eye[i].lid_bottom, ld_bot, ld_bot);
        lv_obj_set_pos(s_eye[i].lid_bottom, w / 2 - ld_bot / 2, h - bot_cover);
        lv_obj_set_style_opa(s_eye[i].lid_bottom, bot_cover > 0 ? LV_OPA_COVER : LV_OPA_TRANSP,
                             LV_PART_MAIN);

        /* The glint is the only thing carrying gaze in this design. */
        const int32_t gw = (int32_t)(26 * face_clampf(p->pupil_size, 0.4f, 1.6f));
        lv_obj_set_size(s_eye[i].glint, gw, gw);
        lv_obj_set_pos(s_eye[i].glint, w / 2 - gw / 2 + (int32_t)(p->pupil_x * w * 0.22f),
                       h / 2 - gw / 2 + (int32_t)(p->pupil_y * h * 0.22f) - h / 5);
        lv_obj_set_style_opa(s_eye[i].glint, open > 0.25f ? LV_OPA_40 : LV_OPA_TRANSP,
                             LV_PART_MAIN);
        lv_obj_move_foreground(s_eye[i].glint);

        /*
         * Cross-fade to the arc. `squint` ramps 0 to 1 as the bottom-closing
         * portion passes about a quarter of the eye's height, which is the
         * point where the carved rect stops reading as an eye.
         */
        const float squint = face_clampf((closure * bottom_bias - 0.22f) * 4.5f, 0.0f, 1.0f);
        lv_obj_set_style_opa(s_eye[i].eye, face_opa(1.0f - squint), LV_PART_MAIN);

        const int32_t ad = (int32_t)(w * 1.45f);
        const int32_t stroke = (int32_t)(h * 0.30f * (0.7f + 0.3f * open));
        lv_obj_set_size(s_eye[i].squint, ad, ad);
        lv_obj_set_pos(s_eye[i].squint, ex + w / 2 - ad / 2, ey + h / 2 - ad / 2 + ad / 5);
        lv_obj_set_style_arc_width(s_eye[i].squint, stroke, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_eye[i].squint, accent, LV_PART_INDICATOR);
        lv_obj_set_style_opa(s_eye[i].squint, face_opa(squint), LV_PART_MAIN);

        /* 270 degrees is the top of the circle, so this span is a dome. */
        lv_arc_set_rotation(s_eye[i].squint, 0);
        lv_arc_set_angles(s_eye[i].squint, 226, 314);
    }
}

const nev_face_renderer_t nev_face_renderer_vector = {
    .id = "vector",
    .name = "Vector eyes",
    .summary = "Two shapes, no mouth. Expression from lid geometry alone.",
    .create = vector_create,
    .destroy = vector_destroy,
    .apply = vector_apply,
};
