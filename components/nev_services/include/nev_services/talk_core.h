/* NEVOS L2 — press and hold to talk, with no microphone in it. */
#ifndef NEV_SERVICES_TALK_CORE_H
#define NEV_SERVICES_TALK_CORE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Push-to-talk is one button and three numbers, and every one of the three
 * exists because of a way the obvious version is wrong.
 *
 * The obvious version opens the microphone on press and closes it on release.
 * On a desk ornament that is held, knocked and slept against, that means:
 *   - a sleeve brushing the button records a second of nothing, which the
 *     daemon dutifully transcribes as nothing, which reads as the device
 *     mishearing rather than as nobody having spoken;
 *   - a tap — which is a gesture a person will absolutely try — is
 *     indistinguishable from the start of a very short hold;
 *   - a button wedged under something records until the battery dies, and
 *     sends every second of it to somebody's computer.
 *
 * So: a press has to survive `arm_ms` before the microphone opens at all, a
 * capture shorter than `min_speech_ms` is thrown away rather than sent, and one
 * longer than `max_ms` stops itself. Meeting mode is where long-form capture
 * lives, and it is a deliberate press of a button that says Record.
 *
 * There is no audio here and no clock of its own: time arrives as an argument,
 * so the whole of the above is a unit test rather than something you verify by
 * holding a button for thirty seconds.
 */

typedef enum {
    NEV_PTT_IDLE = 0,
    NEV_PTT_ARMING,  /* held, but not yet long enough to mean it */
    NEV_PTT_TALKING, /* the microphone is open                   */
} nev_ptt_state_t;

typedef enum {
    NEV_PTT_DO_NOTHING = 0,
    NEV_PTT_DO_START,  /* open the microphone                                */
    NEV_PTT_DO_SEND,   /* close it; there is something worth transcribing    */
    NEV_PTT_DO_DISCARD /* close it and say so; too short, or held too long   */
} nev_ptt_action_t;

/* Why a capture was discarded, so the screen can say which. A person who held
 * the button for a tenth of a second and one who held it for a minute need
 * different sentences. */
typedef enum {
    NEV_PTT_DISCARD_NONE = 0,
    NEV_PTT_DISCARD_TOO_SHORT,
    NEV_PTT_DISCARD_TOO_LONG,
} nev_ptt_discard_t;

typedef struct {
    uint32_t arm_ms;        /* a press shorter than this never opens the mic */
    uint32_t min_speech_ms; /* captured audio shorter than this is discarded */
    uint32_t max_ms;        /* a stuck button stops itself                   */
} nev_ptt_cfg_t;

typedef struct {
    nev_ptt_cfg_t cfg;
    nev_ptt_state_t state;
    uint32_t pressed_at_ms;
    uint32_t talking_since_ms;
    nev_ptt_discard_t last_discard;
} nev_ptt_t;

/* Defaults: 120 ms to arm, 350 ms of speech, 30 s ceiling. */
void nev_ptt_init(nev_ptt_t *p, const nev_ptt_cfg_t *cfg);
void nev_ptt_defaults(nev_ptt_cfg_t *cfg);

nev_ptt_action_t nev_ptt_press(nev_ptt_t *p, uint32_t now_ms);
nev_ptt_action_t nev_ptt_release(nev_ptt_t *p, uint32_t now_ms);

/* Time passing: arms the press and enforces the ceiling. */
nev_ptt_action_t nev_ptt_tick(nev_ptt_t *p, uint32_t now_ms);

/*
 * Gives up wherever it is — the app closed, the link dropped, the screen slept.
 * Returns what to do about the capture that was running, if any.
 */
nev_ptt_action_t nev_ptt_cancel(nev_ptt_t *p);

nev_ptt_state_t nev_ptt_state(const nev_ptt_t *p);
bool nev_ptt_is_talking(const nev_ptt_t *p);
nev_ptt_discard_t nev_ptt_last_discard(const nev_ptt_t *p);

/* How long the microphone has been open. 0 when it is not. */
uint32_t nev_ptt_talk_ms(const nev_ptt_t *p, uint32_t now_ms);

/* Plain words for a discard, for a status line. Never NULL. */
const char *nev_ptt_discard_reason(nev_ptt_discard_t why);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_TALK_CORE_H */
