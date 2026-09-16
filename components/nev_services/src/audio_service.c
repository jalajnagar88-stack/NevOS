/* Microphone capture. See audio_service.h. */
#include "nev_services/audio_service.h"

#include "nev_board/board.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_time.h"

#include <string.h>

#define TAG "audio"

static bool s_ready;
static bool s_capturing;
static uint32_t s_session;
static uint32_t s_seq;
static nev_audio_kind_t s_kind;
static uint64_t s_samples_total;

/* The chunk being filled. Static rather than a blob held across calls: a blob
 * is the unit of transfer, and holding one open for 128 ms would keep a pool
 * slot occupied for no reason. */
static int16_t s_chunk[NEV_AUDIO_CHUNK_SAMPLES];
static size_t s_chunk_len;

nev_err_t audio_service_init(void) {
    s_ready = true;
    s_capturing = false;
    s_session = 0;
    s_chunk_len = 0;
    return NEV_OK;
}

void audio_service_deinit(void) {
    if (s_capturing) audio_service_stop();
    s_ready = false;
}

/* Publishes one chunk. `final` ends the capture for the daemon. */
static void emit(bool final) {
    if (s_chunk_len == 0 && !final) return;

    const size_t bytes = s_chunk_len * sizeof(int16_t);
    uint8_t *data = NULL;
    nev_blob_t handle = nev_blob_alloc(bytes ? bytes : 1, &data);
    if (handle == NEV_BLOB_NONE) {
        /*
         * Dropped, not stalled.
         *
         * A microphone does not pause while a buffer frees up, and holding the
         * audio task waiting for one would put the backlog somewhere worse. The
         * sequence number goes up regardless, so the daemon sees the gap and
         * the transcript can say there is one.
         */
        NEV_LOGW(TAG, "no blob for audio; dropping %u samples", (unsigned)s_chunk_len);
        s_seq++;
        s_chunk_len = 0;
        return;
    }
    if (bytes) {
        /* Little-endian on both ends; the samples go out as they sit in memory.
         * Stated because on a big-endian port this is the line that breaks, and
         * it would break as noise rather than as a failure. */
        memcpy(data, s_chunk, bytes);
    }

    nev_event_t ev = nev_event_make(NEV_EVT_AUDIO_CHUNK, NEV_SRC_AUDIO);
    ev.flags |= NEV_EVF_BLOB;
    ev.p.audio.handle = handle;
    ev.p.audio.seq = (uint16_t)s_seq++;
    ev.p.audio.session = s_session;
    ev.p.audio.samples = (uint16_t)s_chunk_len;
    ev.p.audio.kind = (uint8_t)s_kind;
    ev.p.audio.final = final ? 1 : 0;

    (void)nev_bus_publish(&ev);
    /* Step 3 of the ownership protocol in nev_blob.h. The bus has retained one
     * reference per subscriber; this drops the one alloc gave us. */
    nev_blob_release(handle);
    s_chunk_len = 0;
}

uint32_t audio_service_start(nev_audio_kind_t kind) {
    if (!s_ready || s_capturing) return 0;

    if (nev_board_audio_start() != NEV_OK) {
        NEV_LOGE(TAG, "the microphone would not start");
        return 0;
    }
    /* Session ids only have to be distinct within a connection, and starting
     * from the boot-relative millisecond makes them so without any state that
     * has to survive a reboot. */
    s_session = nev_now_ms() | 1u;
    s_seq = 0;
    s_kind = kind;
    s_chunk_len = 0;
    s_samples_total = 0;
    s_capturing = true;

    nev_event_t ev = nev_event_make(NEV_EVT_AUDIO_CAPTURE_START, NEV_SRC_AUDIO);
    ev.p.u32[0] = s_session;
    ev.p.u32[1] = (uint32_t)kind;
    (void)nev_bus_publish(&ev);

    NEV_LOGI(TAG, "capture %u started (%s)", (unsigned)s_session,
             kind == NEV_AUDIO_KIND_TRANSCRIPT ? "meeting" : "note");
    return s_session;
}

void audio_service_stop(void) {
    if (!s_capturing) return;

    /* Whatever is in hand goes out marked final, so the daemon finishes rather
     * than waiting for a chunk that is never coming. */
    emit(true);
    nev_board_audio_stop();
    s_capturing = false;

    nev_event_t ev = nev_event_make(NEV_EVT_AUDIO_CAPTURE_STOP, NEV_SRC_AUDIO);
    ev.p.u32[0] = s_session;
    (void)nev_bus_publish(&ev);

    NEV_LOGI(TAG, "capture %u stopped after %u ms", (unsigned)s_session,
             (unsigned)audio_service_elapsed_ms());
}

void audio_service_poll(uint32_t now_ms) {
    (void)now_ms;
    if (!s_capturing) return;

    /*
     * Bounded per call. A frame that fell behind — a garbage collection, a slow
     * flush — leaves more audio waiting than one chunk, and draining all of it
     * here would make the next frame late as well. Four chunks is half a second
     * of catching up, after which the board's own buffer holds the rest.
     */
    for (int budget = 4; budget > 0; budget--) {
        const size_t room = NEV_AUDIO_CHUNK_SAMPLES - s_chunk_len;
        const size_t got = nev_board_audio_read(s_chunk + s_chunk_len, room);
        if (got == 0) return;

        s_chunk_len += got;
        s_samples_total += got;
        if (s_chunk_len == NEV_AUDIO_CHUNK_SAMPLES) emit(false);
    }
}

bool audio_service_is_capturing(void) {
    return s_capturing;
}

uint32_t audio_service_session(void) {
    return s_capturing ? s_session : 0;
}

uint32_t audio_service_elapsed_ms(void) {
    return (uint32_t)(s_samples_total * 1000u / NEV_AUDIO_SAMPLE_RATE);
}
