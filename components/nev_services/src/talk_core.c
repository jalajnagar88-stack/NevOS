/* Push-to-talk, with no microphone in it. See talk_core.h. */
#include "nev_services/talk_core.h"

void nev_ptt_defaults(nev_ptt_cfg_t *cfg) {
    if (!cfg) return;
    /*
     * 120 ms is longer than a knock and far shorter than a deliberate hold.
     * 350 ms is about the shortest a spoken word gets. 30 s is a ceiling rather
     * than a target: anything approaching it is a mistake, and meeting mode is
     * the app for long-form capture.
     */
    cfg->arm_ms = 120;
    cfg->min_speech_ms = 350;
    cfg->max_ms = 30000;
}

void nev_ptt_init(nev_ptt_t *p, const nev_ptt_cfg_t *cfg) {
    if (!p) return;
    if (cfg) {
        p->cfg = *cfg;
    } else {
        nev_ptt_defaults(&p->cfg);
    }
    p->state = NEV_PTT_IDLE;
    p->pressed_at_ms = 0;
    p->talking_since_ms = 0;
    p->last_discard = NEV_PTT_DISCARD_NONE;
}

nev_ptt_action_t nev_ptt_press(nev_ptt_t *p, uint32_t now_ms) {
    if (!p) return NEV_PTT_DO_NOTHING;
    /* A second press while already talking is a repeat, not a new gesture:
     * a button that chatters must not restart the capture underneath itself. */
    if (p->state != NEV_PTT_IDLE) return NEV_PTT_DO_NOTHING;

    p->state = NEV_PTT_ARMING;
    p->pressed_at_ms = now_ms;
    p->last_discard = NEV_PTT_DISCARD_NONE;

    /* arm_ms of 0 means "open it now", which is what a test wants and what a
     * physical button with hardware debouncing could reasonably ask for. */
    if (p->cfg.arm_ms == 0) {
        p->state = NEV_PTT_TALKING;
        p->talking_since_ms = now_ms;
        return NEV_PTT_DO_START;
    }
    return NEV_PTT_DO_NOTHING;
}

nev_ptt_action_t nev_ptt_release(nev_ptt_t *p, uint32_t now_ms) {
    if (!p) return NEV_PTT_DO_NOTHING;

    switch (p->state) {
        case NEV_PTT_ARMING:
            /* Never opened the microphone, so there is nothing to close and
             * nothing to apologise for. A brush is not an error message. */
            p->state = NEV_PTT_IDLE;
            return NEV_PTT_DO_NOTHING;

        case NEV_PTT_TALKING: {
            const uint32_t held = now_ms - p->talking_since_ms;
            p->state = NEV_PTT_IDLE;
            if (held < p->cfg.min_speech_ms) {
                p->last_discard = NEV_PTT_DISCARD_TOO_SHORT;
                return NEV_PTT_DO_DISCARD;
            }
            return NEV_PTT_DO_SEND;
        }

        case NEV_PTT_IDLE:
        default:
            return NEV_PTT_DO_NOTHING;
    }
}

nev_ptt_action_t nev_ptt_tick(nev_ptt_t *p, uint32_t now_ms) {
    if (!p) return NEV_PTT_DO_NOTHING;

    switch (p->state) {
        case NEV_PTT_ARMING:
            if ((uint32_t)(now_ms - p->pressed_at_ms) < p->cfg.arm_ms) return NEV_PTT_DO_NOTHING;
            p->state = NEV_PTT_TALKING;
            /*
             * The clock starts here, not at the press. min_speech_ms is about
             * how much audio exists, and the arming window produced none.
             */
            p->talking_since_ms = now_ms;
            return NEV_PTT_DO_START;

        case NEV_PTT_TALKING:
            if (p->cfg.max_ms == 0) return NEV_PTT_DO_NOTHING;
            if ((uint32_t)(now_ms - p->talking_since_ms) < p->cfg.max_ms) return NEV_PTT_DO_NOTHING;
            /*
             * Discarded rather than sent. Something is sitting on the button,
             * and thirty seconds of a room going to somebody's computer is the
             * outcome to avoid, not the one to salvage.
             */
            p->state = NEV_PTT_IDLE;
            p->last_discard = NEV_PTT_DISCARD_TOO_LONG;
            return NEV_PTT_DO_DISCARD;

        case NEV_PTT_IDLE:
        default:
            return NEV_PTT_DO_NOTHING;
    }
}

nev_ptt_action_t nev_ptt_cancel(nev_ptt_t *p) {
    if (!p) return NEV_PTT_DO_NOTHING;
    const bool was_talking = (p->state == NEV_PTT_TALKING);
    p->state = NEV_PTT_IDLE;
    if (!was_talking) return NEV_PTT_DO_NOTHING;
    /*
     * Discarded, not sent. A cancel is the device deciding the gesture is over
     * for a reason that has nothing to do with the speaker — the app closed,
     * the link dropped — and finishing a question the user is no longer
     * watching the answer to is worse than dropping it.
     */
    p->last_discard = NEV_PTT_DISCARD_NONE;
    return NEV_PTT_DO_DISCARD;
}

nev_ptt_state_t nev_ptt_state(const nev_ptt_t *p) {
    return p ? p->state : NEV_PTT_IDLE;
}

bool nev_ptt_is_talking(const nev_ptt_t *p) {
    return p && p->state == NEV_PTT_TALKING;
}

nev_ptt_discard_t nev_ptt_last_discard(const nev_ptt_t *p) {
    return p ? p->last_discard : NEV_PTT_DISCARD_NONE;
}

uint32_t nev_ptt_talk_ms(const nev_ptt_t *p, uint32_t now_ms) {
    if (!p || p->state != NEV_PTT_TALKING) return 0;
    return now_ms - p->talking_since_ms;
}

const char *nev_ptt_discard_reason(nev_ptt_discard_t why) {
    switch (why) {
        case NEV_PTT_DISCARD_TOO_SHORT:
            return "Hold the button while you talk";
        case NEV_PTT_DISCARD_TOO_LONG:
            return "That ran long. Use Meeting for anything this size.";
        case NEV_PTT_DISCARD_NONE:
        default:
            return "";
    }
}
