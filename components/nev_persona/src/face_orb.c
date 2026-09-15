/*
 * Design C — "orb": one abstract form, no anthropomorphism at all.
 *
 * A ring whose arc span, thickness and colour carry mood; a core whose size is
 * arousal and whose offset is attention; and three satellites that only appear
 * when the device is thinking.
 *
 * The argument for it: nothing about it will look dated or twee in two years,
 * it costs almost nothing to render, and it never lands in the uncanny valley
 * because it never claims to be a face. The argument against it, stated
 * honestly: "sleepy" and "concerned" are genuinely harder to read here than in
 * either of the other two. Abstraction buys timelessness and spends legibility.
 */
#include "face_common.h"
#include <math.h>

#define RING_R    168
#define CORE_R    64
#define SAT_COUNT 3
#define SAT_R     13

static lv_obj_t *s_root;
static lv_obj_t *s_ring;
static lv_obj_t *s_core;
static lv_obj_t *s_halo;
static lv_obj_t *s_sat[SAT_COUNT];

static lv_obj_t *make_circle(lv_obj_t *parent, lv_color_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static nev_err_t orb_create(lv_obj_t *parent) {
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_center(s_root);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_ring = lv_arc_create(s_root);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ring, RING_R * 2, RING_R * 2);
    lv_obj_center(s_ring);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_obj_set_style_arc_rounded(s_ring, true, LV_PART_INDICATOR);

    s_halo = make_circle(s_root, FACE_CALM);
    s_core = make_circle(s_root, FACE_CALM);
    for (int i = 0; i < SAT_COUNT; i++)
        s_sat[i] = make_circle(s_root, FACE_CALM);
    return NEV_OK;
}

static void orb_destroy(void) {
    if (s_root) lv_obj_delete(s_root);
    s_root = NULL;
    s_ring = s_core = s_halo = NULL;
    lv_memzero(s_sat, sizeof(s_sat));
}

static void orb_apply(const nev_face_params_t *p) {
    if (!s_root) return;

    const lv_color_t accent = face_accent(p);
    const int32_t ox = (int32_t)(p->head_x * 30.0f);
    const int32_t oy = (int32_t)(p->head_y * 30.0f);

    /*
     * Ring span is openness: wide awake is nearly a closed circle, sleepy is a
     * thin sliver at the bottom. Rotation follows head tilt so the whole form
     * leans the way a head would.
     */
    const float open = face_clampf(p->eye_open, 0.0f, 1.0f);
    const int32_t span = 40 + (int32_t)(open * 300.0f);
    const int32_t rot = 90 - span / 2 + (int32_t)(p->head_tilt * 26.0f);

    lv_arc_set_rotation(s_ring, rot);
    lv_arc_set_angles(s_ring, 0, (lv_value_precise_t)span);
    lv_obj_set_style_arc_width(s_ring, 6 + (int32_t)(face_clampf(p->glow, 0, 1) * 9.0f),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x1C2128), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 3, LV_PART_MAIN);
    lv_obj_set_pos(s_ring, 240 - RING_R + ox, 240 - RING_R + oy);

    /* Core size is arousal; its offset is where attention is pointed. */
    const float energy = face_clampf(0.45f + p->glow * 0.4f + p->mouth_curve * 0.25f, 0.2f, 1.5f);
    const int32_t cd = (int32_t)(CORE_R * 2 * energy * face_clampf(p->pupil_size, 0.5f, 1.5f));
    const int32_t cx = FACE_CX + ox + (int32_t)(p->pupil_x * 54.0f);
    const int32_t cy = FACE_CY + oy + (int32_t)(p->pupil_y * 54.0f);

    lv_obj_set_size(s_core, cd, cd);
    lv_obj_set_pos(s_core, cx - cd / 2, cy - cd / 2);
    lv_obj_set_style_bg_color(s_core, accent, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_core, accent, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_core, (int32_t)(60 * face_clampf(p->glow, 0, 1)), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_core, face_opa(p->glow * 0.7f), LV_PART_MAIN);

    /* A slack halo behind the core: the form breathes rather than sitting still. */
    const int32_t hd = (int32_t)(cd * 1.7f);
    lv_obj_set_size(s_halo, hd, hd);
    lv_obj_set_pos(s_halo, cx - hd / 2, cy - hd / 2);
    lv_obj_set_style_bg_color(s_halo, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_halo, face_opa(p->glow * 0.13f), LV_PART_MAIN);
    lv_obj_move_background(s_halo);

    /*
     * Satellites appear only when gaze is far off-axis, which in the preset
     * table means THINKING and nothing else. Giving the abstract form one
     * behaviour it does exclusively is what keeps "thinking" legible without a
     * face to make a thinking expression with.
     */
    const float thinking =
        face_clampf((fabsf(p->pupil_x) + fabsf(p->pupil_y) - 0.6f) * 1.6f, 0.0f, 1.0f);
    for (int i = 0; i < SAT_COUNT; i++) {
        const float angle = (float)i * (6.2831853f / SAT_COUNT) + p->head_tilt * 1.2f;
        const int32_t sx = cx + (int32_t)(cosf(angle) * (float)(RING_R - 34));
        const int32_t sy = cy + (int32_t)(sinf(angle) * (float)(RING_R - 34));
        const int32_t sd = (int32_t)(SAT_R * 2 * (0.6f + 0.4f * thinking));
        lv_obj_set_size(s_sat[i], sd, sd);
        lv_obj_set_pos(s_sat[i], sx - sd / 2, sy - sd / 2);
        lv_obj_set_style_bg_color(s_sat[i], accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_sat[i], face_opa(thinking * 0.85f), LV_PART_MAIN);
    }
}

const nev_face_renderer_t nev_face_renderer_orb = {
    .id = "orb",
    .name = "Abstract orb",
    .summary = "One form. Ring span, core size and offset carry everything.",
    .create = orb_create,
    .destroy = orb_destroy,
    .apply = orb_apply,
};
