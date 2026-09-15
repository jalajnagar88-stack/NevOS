/*
 * NEVOS L3 — the face.
 *
 * A mood is not an animation clip. It is a target set of the scalars below, and
 * a transition is interpolation between two such sets. That is why transitions
 * are tweened for free, why a mood change mid-transition does not glitch, and
 * why adding a mood costs one row of numbers rather than a new animation.
 *
 * This header is deliberately free of LVGL: the mood machine, the presets and
 * the tween are pure data and are unit-tested without a UI toolkit. Rendering
 * lives in face.h.
 */
#ifndef NEV_PERSONA_FACE_PARAMS_H
#define NEV_PERSONA_FACE_PARAMS_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_MOOD_IDLE = 0,
    NEV_MOOD_CURIOUS,
    NEV_MOOD_HAPPY,
    NEV_MOOD_FOCUSED,
    NEV_MOOD_SLEEPY,
    NEV_MOOD_CELEBRATING,
    NEV_MOOD_CONCERNED,
    NEV_MOOD_THINKING,
    NEV_MOOD_COUNT
} nev_mood_t;

const char *nev_mood_name(nev_mood_t mood);
bool nev_mood_from_name(const char *name, nev_mood_t *out);

/*
 * The whole expressive vocabulary. Every value is normalised so that a renderer
 * can interpret it in its own idiom: a design with no mouth simply ignores the
 * mouth fields rather than needing a different parameter set.
 *
 * Neutral is 0.0 for signed fields and 1.0 for scale fields.
 */
typedef struct {
    /* eyes */
    float eye_open;   /* 0 shut .. 1 wide                      */
    float eye_slant;  /* -1 outer-down (sad) .. +1 inner-down (stern) */
    float eye_scale;  /* 0.5 .. 1.5                            */
    float eye_spread; /* 0.7 .. 1.3, distance apart            */

    /* gaze */
    float pupil_x;    /* -1 left .. +1 right                   */
    float pupil_y;    /* -1 up .. +1 down                      */
    float pupil_size; /* 0.5 .. 1.5, dilation                  */

    /* brows */
    float brow_raise; /* -1 lowered .. +1 raised               */
    float brow_angle; /* -1 outer-up .. +1 inner-up (worry)    */

    /* mouth */
    float mouth_curve; /* -1 frown .. +1 smile                 */
    float mouth_open;  /* 0 closed .. 1 open                   */
    float mouth_scale; /* 0.5 .. 1.5                           */

    /* whole head */
    float head_tilt; /* -1 .. +1, roughly -15..+15 degrees     */
    float head_x;    /* -1 .. +1, in units of the offset range */
    float head_y;

    /* accents */
    float blush; /* 0 .. 1 */
    float glow;  /* 0 .. 1, accent intensity */
} nev_face_params_t;

/* The eight moods as parameter targets. Never mutated; tween between copies. */
const nev_face_params_t *nev_face_preset(nev_mood_t mood);

/* out = a + (b - a) * t, component-wise. t is clamped to 0..1. */
void nev_face_lerp(nev_face_params_t *out, const nev_face_params_t *a, const nev_face_params_t *b,
                   float t);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PERSONA_FACE_PARAMS_H */
