/*
 * The eight moods, as numbers.
 *
 * This is the product's emotional vocabulary, and it is written as one table on
 * purpose: tuning a face is a matter of comparing rows, and a base-plus-override
 * form hides exactly the differences you need to see. Values are positional, in
 * struct order, with the column header below.
 *
 * Neutral is 0.0 for signed fields and 1.0 for scale fields.
 */
#include "nev_persona/face_params.h"
#include <string.h>

/* Positional initialisers depend on the field order in nev_face_params_t. If a
 * field is added or reordered, this assert fires and the table must be updated
 * rather than silently shifting every value one column to the left. */
_Static_assert(sizeof(nev_face_params_t) == 17 * sizeof(float),
               "face parameter count changed — update the preset table below");

/* clang-format off */
/*                                eye                        pupil                brow            mouth                   head                  accent
 *                       open  slant  scale spread      x      y   size     raise  angle    curve  open  scale     tilt     x      y      blush  glow   */
static const nev_face_params_t kPresets[NEV_MOOD_COUNT] = {
    /* IDLE        awake, unbothered, faintly warm so it never reads as "off"   */
    [NEV_MOOD_IDLE]        = { 0.92f,  0.00f, 1.00f, 1.00f,  0.00f, 0.00f, 1.00f,   0.00f, 0.00f,   0.15f, 0.00f, 1.00f,   0.00f, 0.00f, 0.00f,  0.00f, 0.45f },

    /* CURIOUS     head cocked, gaze off-axis, pupils opening                   */
    [NEV_MOOD_CURIOUS]     = { 1.00f,  0.00f, 1.08f, 1.00f,  0.45f,-0.15f, 1.15f,   0.50f,-0.30f,   0.20f, 0.15f, 1.00f,   0.55f, 0.00f, 0.00f,  0.00f, 0.65f },

    /* HAPPY       the squint sells it; a smile without one reads as a smirk.
     *             eye_slant stays 0: a real squint is symmetric, and a negative
     *             slant here is the SAD direction — with no mouth to argue
     *             otherwise it made the eyes-only design read as miserable.    */
    [NEV_MOOD_HAPPY]       = { 0.44f,  0.00f, 1.05f, 1.00f,  0.00f, 0.00f, 1.00f,   0.35f, 0.00f,   0.85f, 0.30f, 1.15f,   0.00f, 0.00f,-0.15f,  0.35f, 0.85f },

    /* FOCUSED     narrowed and level. Attention reads as an absence of motion  */
    [NEV_MOOD_FOCUSED]     = { 0.70f,  0.25f, 0.95f, 0.95f,  0.00f, 0.00f, 0.80f,  -0.40f, 0.00f,  -0.05f, 0.00f, 0.85f,   0.00f, 0.00f, 0.00f,  0.00f, 0.60f },

    /* SLEEPY      lids heavy, everything drifting downward                     */
    [NEV_MOOD_SLEEPY]      = { 0.22f, -0.20f, 0.90f, 1.00f,  0.00f, 0.40f, 0.85f,  -0.50f, 0.00f,  -0.10f, 0.00f, 0.80f,  -0.30f, 0.00f, 0.25f,  0.00f, 0.20f },

    /* CELEBRATING everything open and up. The one mood allowed to be loud      */
    [NEV_MOOD_CELEBRATING] = { 0.34f,  0.00f, 1.20f, 1.00f,  0.00f, 0.00f, 1.00f,   1.00f, 0.00f,   1.00f, 0.75f, 1.30f,   0.00f, 0.00f,-0.40f,  0.60f, 1.00f },

    /* CONCERNED   inner brows up; that one cue carries most of the worry       */
    [NEV_MOOD_CONCERNED]   = { 0.88f, -0.70f, 0.94f, 1.06f,  0.00f,-0.10f, 1.20f,   0.35f, 0.85f,  -0.60f, 0.00f, 0.90f,  -0.20f, 0.00f, 0.10f,  0.00f, 0.35f },

    /* THINKING    looking at nothing in particular, asymmetric brow            */
    [NEV_MOOD_THINKING]    = { 0.80f,  0.10f, 1.00f, 1.00f, -0.60f,-0.50f, 0.90f,   0.25f,-0.18f,  -0.15f, 0.00f, 0.80f,   0.30f, 0.00f, 0.00f,  0.00f, 0.70f },
};
/* clang-format on */

static const char *kNames[NEV_MOOD_COUNT] = {"idle",   "curious",     "happy",     "focused",
                                             "sleepy", "celebrating", "concerned", "thinking"};

const char *nev_mood_name(nev_mood_t mood) {
    return (mood >= 0 && mood < NEV_MOOD_COUNT) ? kNames[mood] : "?";
}

bool nev_mood_from_name(const char *name, nev_mood_t *out) {
    if (!name || !out) return false;
    for (int i = 0; i < NEV_MOOD_COUNT; i++) {
        if (strcmp(name, kNames[i]) == 0) {
            *out = (nev_mood_t)i;
            return true;
        }
    }
    return false;
}

const nev_face_params_t *nev_face_preset(nev_mood_t mood) {
    return &kPresets[(mood >= 0 && mood < NEV_MOOD_COUNT) ? mood : NEV_MOOD_IDLE];
}

void nev_face_lerp(nev_face_params_t *out, const nev_face_params_t *a, const nev_face_params_t *b,
                   float t) {
    if (!out || !a || !b) return;

    /*
     * Endpoints copy rather than interpolate. a + (b - a) * 1.0f is not
     * bit-exactly b for every float, and a transition that ends one ULP short
     * of its target leaves the face permanently almost-in-a-mood — and the
     * error accumulates if the mood is retargeted repeatedly. Exactness at the
     * ends is worth two comparisons.
     */
    if (t <= 0.0f) {
        *out = *a;
        return;
    }
    if (t >= 1.0f) {
        *out = *b;
        return;
    }

    /* Treating the struct as an array of floats keeps this correct when a field
     * is added: there is no per-field list here to forget to update. */
    const float *pa = (const float *)a;
    const float *pb = (const float *)b;
    float *po = (float *)out;
    for (size_t i = 0; i < sizeof(nev_face_params_t) / sizeof(float); i++) {
        po[i] = pa[i] + (pb[i] - pa[i]) * t;
    }
}
