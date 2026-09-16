/* Playing the cues. The sounds themselves are in sound_core. */
#include "nev_services/sound_service.h"

#include "nev_board/board.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"

#define TAG           "sound"

/* One frame's worth at 30 fps, rounded up. Rendering further ahead would mean a
 * cue that cannot be replaced promptly when something more important happens. */
#define SLICE_SAMPLES 600

static nev_sound_core_t s_core;
static nev_sub_t *s_sub;
static bool s_ready;
static int16_t s_slice[SLICE_SAMPLES];
/* What the board would not take last time. A partial write is normal and the
 * remainder must be kept, or a cue arrives with a hole in it. */
static size_t s_pending;
static size_t s_pending_at;

static uint8_t current_volume(void) {
    if (!nev_store_is_ready()) return 60;
    if (nev_store_num(NEV_SET_SOUND_ENABLED) == 0) return 0;
    return (uint8_t)nev_store_num(NEV_SET_VOLUME);
}

nev_err_t sound_service_init(void) {
    nev_sound_core_init(&s_core, NEV_AUDIO_SAMPLE_RATE);
    s_pending = 0;
    s_pending_at = 0;

    /*
     * Most of the device's sounds are things that already happen on the bus.
     * Subscribing here rather than making every app remember to play a cue is
     * what keeps them consistent — and it means a silent app is a bug in one
     * place rather than an omission in twelve.
     */
    const nev_sub_cfg_t cfg = {
        .name = "sound",
        .domains = NEV_DOM(APP) | NEV_DOM(GAME),
        .depth = 8,
        .full_policy = NEV_FULL_DROP_OLDEST,
    };
    s_sub = nev_bus_subscribe(&cfg);
    if (!s_sub) return NEV_ERR_NO_MEM;

    if (nev_board_audio_out_start() != NEV_OK) {
        NEV_LOGW(TAG, "no audio output; the device will be silent");
    }
    s_ready = true;
    return NEV_OK;
}

void sound_service_deinit(void) {
    if (!s_ready) return;
    nev_board_audio_out_stop();
    s_ready = false;
    s_sub = NULL;
}

void nev_sound_play(nev_sound_cue_t cue) {
    if (!s_ready) return;
    const uint8_t volume = current_volume();
    if (volume == 0) return;

    nev_sound_core_start(&s_core, cue, volume);
    /* Whatever was half-written belongs to the cue that has just been replaced. */
    s_pending = 0;
    s_pending_at = 0;
}

static void drain_bus(void) {
    nev_event_t ev;
    while (nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        switch (ev.type) {
            case NEV_EVT_APP_LAUNCH:
                nev_sound_play(NEV_CUE_LAUNCH);
                break;
            case NEV_EVT_APP_NAV_HOME:
                nev_sound_play(NEV_CUE_BACK);
                break;
            case NEV_EVT_GAME_HIGHSCORE_BEAT:
                nev_sound_play(NEV_CUE_SCORE);
                break;
            case NEV_EVT_GAME_OVER:
                nev_sound_play(NEV_CUE_OVER);
                break;
            default:
                break;
        }
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
}

void sound_service_tick(uint32_t now_ms) {
    (void)now_ms;
    if (!s_ready) return;

    drain_bus();

    /* Finish the last slice before rendering a new one. */
    if (s_pending > 0) {
        const size_t wrote = nev_board_audio_out_write(s_slice + s_pending_at, s_pending);
        s_pending -= wrote;
        s_pending_at += wrote;
        if (s_pending > 0) return;
    }

    if (!nev_sound_core_active(&s_core)) return;

    const size_t rendered = nev_sound_core_render(&s_core, s_slice, SLICE_SAMPLES);
    if (rendered == 0) return;

    const size_t wrote = nev_board_audio_out_write(s_slice, rendered);
    s_pending = rendered - wrote;
    s_pending_at = wrote;
}

bool sound_service_is_playing(void) {
    return s_ready && (nev_sound_core_active(&s_core) || s_pending > 0);
}
