/* Shared geometry and palette for the face renderers. Not a public header. */
#ifndef NEV_PERSONA_FACE_COMMON_H
#define NEV_PERSONA_FACE_COMMON_H

#include "nev_persona/face.h"

#define FACE_CX     240
#define FACE_CY     240

/* Provisional palette. Moves into the appkit theme at M3, at which point the
 * renderers take tokens instead of literals. */
#define FACE_BG     lv_color_hex(0x0B0E14)
#define FACE_CALM   lv_color_hex(0x4C8DFF) /* neutral, curious, focused, thinking */
#define FACE_WARM   lv_color_hex(0x35D0BA) /* happy, celebrating                  */
#define FACE_ALERT  lv_color_hex(0xE8A33D) /* concerned                           */
#define FACE_SCLERA lv_color_hex(0xF2F6FC)
#define FACE_PUPIL  lv_color_hex(0x121721)
#define FACE_BLUSH  lv_color_hex(0xFF6B8A)

static inline float face_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int32_t face_lerpi(int32_t a, int32_t b, float t) {
    return a + (int32_t)((float)(b - a) * face_clampf(t, 0.0f, 1.0f));
}

/*
 * Mood colour is derived rather than stored: the renderers read the same
 * parameters as everything else, so a new mood does not need a colour added to
 * a table somewhere it can be forgotten.
 */
static inline lv_color_t face_accent(const nev_face_params_t *p) {
    lv_color_t base = FACE_CALM;
    if (p->mouth_curve > 0.5f)
        base = FACE_WARM;
    else if (p->mouth_curve < -0.4f)
        base = FACE_ALERT;

    /* Glow dims by mixing toward the background, but only so far: below about
     * 55% the amber reads as muddy brown rather than as a dimmed amber. */
    uint8_t mix = (uint8_t)(140 + 115 * face_clampf(p->glow, 0.0f, 1.0f));
    return lv_color_mix(base, FACE_BG, mix);
}

static inline lv_opa_t face_opa(float v) {
    return (lv_opa_t)(255.0f * face_clampf(v, 0.0f, 1.0f));
}

#endif /* NEV_PERSONA_FACE_COMMON_H */
