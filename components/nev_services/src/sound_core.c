/* The sounds the device makes. See sound_core.h. */
#include "nev_services/sound_core.h"

#include <math.h>

/*
 * Each cue is two notes and a duration. Two is enough: a rise reads as
 * something opening, a fall as something closing, and a repeat as something
 * wanting attention. A third note starts to sound like a ringtone.
 */
typedef struct {
    float hz_from;
    float hz_to;
    uint32_t ms;
    uint8_t gain; /* relative, 0..100 — a tap must not be as loud as an alarm */
    bool warble;  /* alternates between the two notes instead of sliding      */
} cue_spec_t;

static const cue_spec_t kCues[NEV_CUE_COUNT] = {
    /* Short and soft. This one plays most often, so it has to be almost
     * subliminal — a sound you notice is a sound you get tired of. */
    [NEV_CUE_TAP] = {.hz_from = 1200.0f, .hz_to = 1200.0f, .ms = 28, .gain = 35},
    [NEV_CUE_LAUNCH] = {.hz_from = 660.0f, .hz_to = 990.0f, .ms = 120, .gain = 55},
    [NEV_CUE_BACK] = {.hz_from = 880.0f, .hz_to = 587.0f, .ms = 110, .gain = 50},
    [NEV_CUE_SCORE] = {.hz_from = 784.0f, .hz_to = 1318.0f, .ms = 180, .gain = 70},
    /* A minor third down: the one cue allowed to sound disappointed. */
    [NEV_CUE_OVER] = {.hz_from = 523.0f, .hz_to = 415.0f, .ms = 320, .gain = 65},
    /* The only cue meant to be heard from across a room, so it warbles rather
     * than sliding — a pitch that changes abruptly carries where a smooth one
     * turns into background. */
    [NEV_CUE_ALERT] = {.hz_from = 880.0f, .hz_to = 1174.0f, .ms = 900, .gain = 90, .warble = true},
};

static uint32_t s_rate = 16000;

void nev_sound_core_init(nev_sound_core_t *s, uint32_t sample_rate) {
    if (!s) return;
    s->active = false;
    s->cue = NEV_CUE_TAP;
    s->sample = 0;
    s->total = 0;
    s->volume = 0;
    s->phase = 0.0f;
    if (sample_rate) s_rate = sample_rate;
}

uint32_t nev_sound_cue_ms(nev_sound_cue_t cue) {
    if (cue < 0 || cue >= NEV_CUE_COUNT) return 0;
    return kCues[cue].ms;
}

void nev_sound_core_start(nev_sound_core_t *s, nev_sound_cue_t cue, uint8_t volume) {
    if (!s || cue < 0 || cue >= NEV_CUE_COUNT) return;

    s->cue = cue;
    s->sample = 0;
    s->total = kCues[cue].ms * s_rate / 1000u;
    s->volume = volume > 100 ? 100 : volume;
    /* Phase is deliberately not reset: a new cue starting mid-waveform would
     * step the signal, and a step is a click. The envelope below starts at zero
     * anyway, so the join is silent either way — this just costs nothing. */
    s->active = s->total > 0 && s->volume > 0;
}

/*
 * A short attack and a long decay, both curved.
 *
 * The attack matters more than it sounds: a tone that begins at full amplitude
 * begins with a step, and a step through a small speaker is a click that is
 * louder than the note. Three milliseconds is enough to remove it and short
 * enough that the cue still feels instant.
 */
static float envelope(uint32_t sample, uint32_t total) {
    const uint32_t attack = s_rate * 3u / 1000u;
    if (total == 0) return 0.0f;

    if (sample < attack) {
        return (float)sample / (float)attack;
    }
    const float remaining = (float)(total - sample) / (float)(total - attack);
    /* Squared, so it fades out rather than ramping — a linear decay sounds like
     * the sound being switched off partway through. */
    return remaining * remaining;
}

size_t nev_sound_core_render(nev_sound_core_t *s, int16_t *out, size_t max) {
    if (!s || !out || max == 0 || !s->active) return 0;

    const cue_spec_t *spec = &kCues[s->cue];
    size_t written = 0;

    while (written < max && s->sample < s->total) {
        const float t = (float)s->sample / (float)s->total;

        float hz;
        if (spec->warble) {
            /* Four alternations over the cue, which is fast enough to be
             * insistent and slow enough not to buzz. */
            hz = (((uint32_t)(t * 8.0f)) & 1u) ? spec->hz_to : spec->hz_from;
        } else {
            hz = spec->hz_from + (spec->hz_to - spec->hz_from) * t;
        }

        s->phase += 2.0f * 3.14159265f * hz / (float)s_rate;
        if (s->phase > 2.0f * 3.14159265f) s->phase -= 2.0f * 3.14159265f;

        const float amp = envelope(s->sample, s->total) * ((float)spec->gain / 100.0f) *
                          ((float)s->volume / 100.0f);
        float sample = sinf(s->phase) * amp * 32000.0f;

        /* Clamped rather than wrapped. Wrapping an over-range sample turns a
         * loud note into a burst of noise, which is the worst possible way to
         * find out that two gains multiplied badly. */
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;

        out[written++] = (int16_t)sample;
        s->sample++;
    }

    if (s->sample >= s->total) s->active = false;
    return written;
}

bool nev_sound_core_active(const nev_sound_core_t *s) {
    return s && s->active;
}

const char *nev_sound_cue_name(nev_sound_cue_t cue) {
    switch (cue) {
        case NEV_CUE_TAP:
            return "tap";
        case NEV_CUE_LAUNCH:
            return "launch";
        case NEV_CUE_BACK:
            return "back";
        case NEV_CUE_SCORE:
            return "score";
        case NEV_CUE_OVER:
            return "over";
        case NEV_CUE_ALERT:
            return "alert";
        default:
            return "?";
    }
}
