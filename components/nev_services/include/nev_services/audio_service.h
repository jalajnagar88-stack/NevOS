/* NEVOS L2 — microphone capture. */
#ifndef NEV_SERVICES_AUDIO_SERVICE_H
#define NEV_SERVICES_AUDIO_SERVICE_H

#include "nev_kernel/nev_event.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The only producer of AUDIO events. It pulls samples from the board, packs
 * them into chunks, and publishes them; it does not know that a daemon exists.
 *
 * That last part is the layering, not squeamishness: the bridge is L3 and this
 * is L2, so an audio service that called it would be reaching upward. It
 * publishes AUDIO.CHUNK and the bridge subscribes. The useful consequence is
 * that recording to somewhere else later — a file, a second consumer — is a new
 * subscriber rather than a change here.
 */

/* 2048 samples is 4096 bytes: exactly the largest pcm field the wire allows,
 * and 128 ms at 16 kHz, which is short enough that nobody hears the delay. */
#define NEV_AUDIO_CHUNK_SAMPLES 2048

nev_err_t audio_service_init(void);
void audio_service_deinit(void);

/*
 * Starts capturing. `kind` decides what the daemon does with it: a note is
 * transcribed and filed when it ends, a transcript is a meeting.
 *
 * Returns the session id, or 0 if capture could not start.
 */
uint32_t audio_service_start(nev_audio_kind_t kind);

/* Stops, marking the last chunk final so the daemon knows to finish up. */
void audio_service_stop(void);

/* Pulls whatever the microphone has and publishes it. Called every frame. */
void audio_service_poll(uint32_t now_ms);

bool audio_service_is_capturing(void);
uint32_t audio_service_session(void);

/* Milliseconds since the capture started, counted from samples delivered
 * rather than from the clock: what the user is marking is a position in the
 * recording, and the recording is as long as the audio in it. */
uint32_t audio_service_elapsed_ms(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_AUDIO_SERVICE_H */
