/* NEVOS L2 — playing the cues. */
#ifndef NEV_SERVICES_SOUND_SERVICE_H
#define NEV_SERVICES_SOUND_SERVICE_H

#include "nev_port/nev_types.h"
#include "nev_services/sound_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The thin half: reads the volume and mute settings, renders whatever
 * sound_core says, and pushes it at the board.
 *
 * It also listens to the bus, so most of the device makes sounds without ever
 * calling this: a launched app, a beaten high score and a finished game are all
 * events that already exist. An app only calls nev_sound_play for something
 * that is genuinely its own.
 */
nev_err_t sound_service_init(void);
void sound_service_deinit(void);

/* Renders a slice. Called every frame; does nothing when nothing is playing. */
void sound_service_tick(uint32_t now_ms);

void nev_sound_play(nev_sound_cue_t cue);

bool sound_service_is_playing(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_SOUND_SERVICE_H */
