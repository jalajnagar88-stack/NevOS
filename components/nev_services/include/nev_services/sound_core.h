/* NEVOS L2 — the sounds the device makes, as samples. No hardware. */
#ifndef NEV_SERVICES_SOUND_CORE_H
#define NEV_SERVICES_SOUND_CORE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A small synthesiser rather than a set of WAV files.
 *
 * Six cues at 16 kHz would be a few hundred kilobytes of assets for sounds that
 * are, in the end, two sine tones and an envelope. Generating them costs a
 * multiply per sample and nothing in flash — and it makes every property worth
 * testing a number rather than an ear: that a cue starts and ends at silence,
 * that volume zero is actually zero, that nothing clips.
 *
 * The device's speaker is small and close to a person's face. Everything here
 * is short, quiet, and ends by decaying rather than stopping.
 */

typedef enum {
    NEV_CUE_TAP = 0, /* something was pressed        */
    NEV_CUE_LAUNCH,  /* an app opened                */
    NEV_CUE_BACK,    /* back to the home screen      */
    NEV_CUE_SCORE,   /* a point, a beaten high score */
    NEV_CUE_OVER,    /* a game ended                 */
    NEV_CUE_ALERT,   /* a timer finished             */
    NEV_CUE_COUNT,
} nev_sound_cue_t;

typedef struct {
    bool active;
    nev_sound_cue_t cue;
    uint32_t sample; /* position within the cue */
    uint32_t total;  /* its length in samples   */
    uint8_t volume;  /* 0..100                  */
    float phase;     /* carried across buffers so there is no click at
                      * a buffer boundary, which is the one artefact a
                      * listener notices immediately */
} nev_sound_core_t;

void nev_sound_core_init(nev_sound_core_t *s, uint32_t sample_rate);

/* Starts a cue, replacing whatever was playing. Volume 0 means silence. */
void nev_sound_core_start(nev_sound_core_t *s, nev_sound_cue_t cue, uint8_t volume);

/*
 * Renders up to `max` samples. Returns how many were written; 0 when the cue
 * has finished. The caller keeps calling until it returns 0.
 */
size_t nev_sound_core_render(nev_sound_core_t *s, int16_t *out, size_t max);

bool nev_sound_core_active(const nev_sound_core_t *s);

/* How long a cue lasts, in milliseconds. */
uint32_t nev_sound_cue_ms(nev_sound_cue_t cue);

const char *nev_sound_cue_name(nev_sound_cue_t cue);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_SOUND_CORE_H */
