/* Hold to talk. See talk.h. */
#include "nev_apps/talk.h"

#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_events.h"
#include "nev_port/nev_log.h"
#include "nev_services/audio_service.h"
#include "nev_services/talk_core.h"

#define TAG            "talk"

/* Button A. B is back, everywhere, and the shell owns it. */
#define TALK_BUTTON_ID 0

static nev_ptt_t s_ptt;
static lv_obj_t *s_button;
static nev_talk_cb_t s_cb;
static nev_audio_kind_t s_kind;
static bool s_ready;

static void say(nev_talk_state_t state, const char *detail) {
    if (s_cb) s_cb(state, detail ? detail : "");
}

static void paint(void) {
    if (!s_button) return;
    lv_obj_t *label = lv_obj_get_child(s_button, 0);
    const bool live = nev_ptt_is_talking(&s_ptt);
    if (label) lv_label_set_text(label, live ? "Listening" : "Hold to talk");
    lv_obj_set_style_bg_color(s_button, live ? NEV_COL_DANGER : NEV_COL_SURFACE_ALT, LV_PART_MAIN);
}

/* Carries out whatever the state machine decided. */
static void act(nev_ptt_action_t action) {
    switch (action) {
        case NEV_PTT_DO_START:
            if (audio_service_start(s_kind) == 0) {
                /*
                 * No microphone, or one that will not open. Told, not swallowed:
                 * a hold-to-talk button that does nothing at all is the single
                 * most confusing failure this device can have, because the user
                 * has no way to tell it from not being heard.
                 */
                (void)nev_ptt_cancel(&s_ptt);
                say(NEV_TALK_UNAVAILABLE, "The microphone would not start.");
                break;
            }
            say(NEV_TALK_LISTENING, "");
            break;

        case NEV_PTT_DO_SEND:
            audio_service_stop();
            say(NEV_TALK_SENT, "");
            break;

        case NEV_PTT_DO_DISCARD:
            /*
             * The capture is stopped either way. "Discard" is about what the
             * user is told, not about unsending audio: chunks already went to
             * the daemon, and the daemon files a note from what it heard. What
             * this prevents is the app treating a slip as a question.
             */
            audio_service_stop();
            say(NEV_TALK_DISCARDED, nev_ptt_discard_reason(nev_ptt_last_discard(&s_ptt)));
            break;

        case NEV_PTT_DO_NOTHING:
        default:
            return;
    }
    paint();
}

static void hold_cb(lv_event_t *e) {
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            act(nev_ptt_press(&s_ptt, lv_tick_get()));
            break;
        case LV_EVENT_RELEASED:
            act(nev_ptt_release(&s_ptt, lv_tick_get()));
            break;
        case LV_EVENT_PRESS_LOST:
            /*
             * A finger that slid off the button. Treated as a release rather
             * than a cancel, because the person was talking and stopped — the
             * audio is real and they should get their note.
             */
            act(nev_ptt_release(&s_ptt, lv_tick_get()));
            break;
        default:
            break;
    }
}

lv_obj_t *nev_talk_attach(lv_obj_t *parent, nev_audio_kind_t kind, nev_talk_cb_t cb) {
    nev_ptt_init(&s_ptt, NULL);
    s_cb = cb;
    s_kind = kind;
    s_ready = true;

    s_button = parent ? nev_ui_hold_button(parent, "Hold to talk", hold_cb, NULL) : NULL;
    if (s_button) lv_obj_set_width(s_button, LV_PCT(100));
    paint();
    return s_button;
}

void nev_talk_event(const nev_event_t *ev) {
    if (!s_ready || !ev) return;
    if (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == TALK_BUTTON_ID) {
        act(nev_ptt_press(&s_ptt, lv_tick_get()));
    } else if (ev->type == NEV_EVT_INPUT_BUTTON_UP && ev->p.button.id == TALK_BUTTON_ID) {
        act(nev_ptt_release(&s_ptt, lv_tick_get()));
    }
}

void nev_talk_tick(uint32_t now_ms) {
    if (!s_ready) return;
    act(nev_ptt_tick(&s_ptt, now_ms));
}

void nev_talk_stop(void) {
    if (!s_ready) return;
    if (nev_ptt_cancel(&s_ptt) == NEV_PTT_DO_DISCARD) audio_service_stop();
    paint();
}

void nev_talk_detach(void) {
    if (!s_ready) return;
    /* The capture does not outlive the app that started it. Meeting mode is the
     * one that keeps recording when you navigate away, and it says so. */
    if (nev_ptt_cancel(&s_ptt) == NEV_PTT_DO_DISCARD) audio_service_stop();
    s_ready = false;
    s_button = NULL;
    s_cb = NULL;
}

bool nev_talk_is_listening(void) {
    return s_ready && nev_ptt_is_talking(&s_ptt);
}
