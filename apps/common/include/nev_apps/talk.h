/* NEVOS L5 — hold to talk, shared by every app that takes dictation. */
#ifndef NEV_APPS_TALK_H
#define NEV_APPS_TALK_H

#include "lvgl.h"
#include "nev_kernel/nev_event.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The gesture, the button on the screen, and the microphone, joined up once.
 *
 * Notes and Ask both want exactly this and want it to behave identically —
 * the same hold, the same ceiling, the same words when a capture is thrown
 * away. Two copies of it would drift within a release, so there is one, here,
 * and the apps say what to do with the result.
 *
 * Both input routes end up in the same state machine: the physical button
 * (INPUT.BUTTON_DOWN/UP for button A) and the on-screen button this builds.
 * The on-screen one is not a simulator affordance to be deleted later — it is
 * how the device works with the buttons full of somebody's thumb, and it is the
 * only route that exists until the board is chosen.
 *
 * It lives with the apps rather than in nev_appkit for the same reason
 * game_engine does: it is shared *between apps*, and it calls downward into
 * audio_service at L2. Putting it in the toolkit would drag L2 and a board
 * into the toolkit's own unit tests, which have no business with either.
 */

typedef enum {
    NEV_TALK_IDLE = 0,
    NEV_TALK_LISTENING,  /* the microphone is open   */
    NEV_TALK_SENT,       /* released with something worth transcribing */
    NEV_TALK_DISCARDED,  /* too short, too long, or cancelled */
    NEV_TALK_UNAVAILABLE /* no microphone, or it refused to start */
} nev_talk_state_t;

/*
 * Called whenever the state changes. `detail` is a sentence for the user when
 * there is one to say and "" otherwise — the app decides where to put it, since
 * only the app knows which label is its status line.
 */
typedef void (*nev_talk_cb_t)(nev_talk_state_t state, const char *detail);

/*
 * Builds the hold button under `parent` and starts listening for button A.
 * `kind` is what the daemon should do with the audio. Returns the button so the
 * caller can size or place it; NULL if it could not be built.
 */
lv_obj_t *nev_talk_attach(lv_obj_t *parent, nev_audio_kind_t kind, nev_talk_cb_t cb);

/* Feed it the app's events; it only looks at INPUT.BUTTON_*. */
void nev_talk_event(const nev_event_t *ev);

/* Once per frame, from the app's on_tick. Arms the press and enforces the
 * ceiling — without it a hold never opens the microphone. */
void nev_talk_tick(uint32_t now_ms);

/*
 * Abandons a capture in progress and leaves the button where it is.
 *
 * For the link dropping out from under a hold: the microphone must close, but
 * the button has to keep working, because the link usually comes back and the
 * user is still holding it.
 */
void nev_talk_stop(void);

/* From on_close. Stops anything running and forgets the widget. */
void nev_talk_detach(void);

bool nev_talk_is_listening(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_APPS_TALK_H */
