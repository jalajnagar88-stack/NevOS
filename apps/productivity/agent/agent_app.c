/*
 * NEVOS — agent.
 *
 * Ask a question, watch the answer arrive a word at a time.
 *
 * The reply streams because a local model on a laptop takes a second or two to
 * finish, and a blank screen for two seconds reads as "it did not hear me" —
 * which makes people repeat themselves, which makes it worse. Tokens appearing
 * is the device saying "I am working on it" in the only vocabulary it has.
 *
 * Push-to-talk is the primary input (ADR 0009): hold the button, ask, let go.
 * The audio goes up as a QUESTION capture rather than a note, so the daemon
 * transcribes it and hands it back without filing anything — a question you
 * asked out loud should not quietly become a note on somebody's disk.
 *
 * The tappable prompts stay. They are the fastest way to ask something you ask
 * often without saying it out loud, and they are the whole app on a device
 * whose microphone is off or missing.
 */
#include <stdio.h>
#include <string.h>

#include "nev_apps/talk.h"
#include "nev_appkit/app.h"
#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"

/* Enough for a few sentences. The daemon's prompt asks for two, and a reply
 * longer than this is one the screen could not show anyway. */
#define REPLY_CAP 512

static lv_obj_t *s_question;
static lv_obj_t *s_reply;
static lv_obj_t *s_status;
static lv_obj_t *s_prompts;

static char s_reply_text[REPLY_CAP];
static uint32_t s_turn;
static bool s_waiting;

static const char *kPrompts[] = {
    "What should I focus on?",
    "Tell me something interesting",
    "How long until lunch?",
};

static void set_status(const char *text, lv_color_t colour) {
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

static void ask(const char *text) {
    if (s_waiting) return;

    s_turn++;
    s_reply_text[0] = '\0';
    lv_label_set_text(s_reply, "");
    lv_label_set_text(s_question, text);

    if (!nev_bridge_ask(s_turn, text, "agent")) {
        /*
         * Said out loud rather than swallowed. A question that vanishes with no
         * acknowledgement is the single most annoying thing a device like this
         * can do — the user cannot tell whether to ask again.
         */
        set_status("Could not send that. Is the computer connected?", NEV_COL_WARN);
        return;
    }
    s_waiting = true;
    set_status("Thinking", NEV_COL_INK_MUTED);
    lv_obj_add_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);
}

static void prompt_clicked(lv_event_t *e) {
    const char *text = (const char *)lv_event_get_user_data(e);
    ask(text);
}

static void talk_changed(nev_talk_state_t state, const char *detail) {
    switch (state) {
        case NEV_TALK_LISTENING:
            set_status("Listening", NEV_COL_SUCCESS);
            lv_obj_add_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);
            break;
        case NEV_TALK_SENT:
            set_status("Working out what you said", NEV_COL_INK_MUTED);
            break;
        case NEV_TALK_DISCARDED:
        case NEV_TALK_UNAVAILABLE:
            set_status(detail, NEV_COL_WARN);
            /* The prompts come back, because they are now the only way to ask
             * anything and the user has just been told the other way failed. */
            lv_obj_remove_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);
            break;
        case NEV_TALK_IDLE:
        default:
            break;
    }
}

static nev_err_t agent_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, NEV_SP_4, 0);
    lv_obj_set_style_pad_row(root, NEV_SP_3, 0);

    s_question = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_set_width(s_question, LV_PCT(100));
    lv_label_set_long_mode(s_question, LV_LABEL_LONG_WRAP);

    s_reply = nev_ui_label(root, "", NEV_FONT_TITLE, NEV_COL_INK);
    lv_obj_set_width(s_reply, LV_PCT(100));
    lv_label_set_long_mode(s_reply, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(s_reply, 1);

    s_status = nev_ui_label(root, "", NEV_FONT_CAPTION, NEV_COL_INK_FAINT);

    s_prompts = lv_obj_create(root);
    lv_obj_remove_style_all(s_prompts);
    lv_obj_set_width(s_prompts, LV_PCT(100));
    lv_obj_set_height(s_prompts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_prompts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_prompts, NEV_SP_2, 0);
    for (size_t i = 0; i < sizeof(kPrompts) / sizeof(kPrompts[0]); i++) {
        lv_obj_t *b = nev_ui_button_ghost(s_prompts, kPrompts[i], prompt_clicked,
                                          (void *)(uintptr_t)kPrompts[i]);
        lv_obj_set_width(b, LV_PCT(100));
    }

    nev_talk_attach(root, NEV_AUDIO_KIND_QUESTION, talk_changed);

    s_reply_text[0] = '\0';
    s_waiting = false;
    if (nev_bridge_state() == NEV_BRIDGE_READY) {
        set_status("Hold the button to talk, or tap a question", NEV_COL_INK_FAINT);
    } else {
        set_status("Not connected to a computer", NEV_COL_WARN);
    }
    return NEV_OK;
}

static void append_token(const nev_event_t *ev) {
    /* chunk_seq carries the turn, so a late reply to a question the user has
     * already replaced is dropped rather than typed over the new one. */
    if (ev->p.blob.chunk_seq != (uint16_t)s_turn) return;

    const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
    if (!text) return;

    size_t have = strlen(s_reply_text);
    size_t room = sizeof(s_reply_text) - have - 1;
    if (room == 0) return;
    strncat(s_reply_text, text, room);
    lv_label_set_text(s_reply, s_reply_text);
}

static void agent_event(const nev_event_t *ev) {
    nev_talk_event(ev);

    switch (ev->type) {
        case NEV_EVT_BRIDGE_AGENT_TOKEN:
            append_token(ev);
            break;

        case NEV_EVT_BRIDGE_AGENT_DONE: {
            if (ev->p.blob.chunk_seq != (uint16_t)s_turn) break;
            s_waiting = false;
            lv_obj_remove_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);

            const char *error = (const char *)nev_blob_data(ev->p.blob.handle);
            if (error && error[0] != '\0') {
                /* The daemon's own words, which name the actual problem — a
                 * model that is not pulled, a server that is not running. */
                set_status(error, NEV_COL_WARN);
            } else {
                set_status("", NEV_COL_INK_FAINT);
            }
            break;
        }

        case NEV_EVT_BRIDGE_TRANSCRIPT_FINAL: {
            /* What the user said, once the daemon has heard it. Asking from
             * here rather than from the transcript arriving at the notes app
             * keeps one question in flight at a time. */
            const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
            if (text && text[0] != '\0') {
                ask(text);
            } else {
                /* The daemon heard nothing usable. Without this the status line
                 * sits on "Working out what you said" until the app is closed,
                 * and a device that looks permanently busy is worse than one
                 * that admits it missed. */
                set_status("I did not catch that", NEV_COL_WARN);
                lv_obj_remove_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        }

        case NEV_EVT_BRIDGE_DISCONNECTED:
            nev_talk_stop();
            s_waiting = false;
            set_status("The computer went away", NEV_COL_WARN);
            lv_obj_remove_flag(s_prompts, LV_OBJ_FLAG_HIDDEN);
            break;

        case NEV_EVT_BRIDGE_CONNECTED:
            set_status("Hold the button to talk, or tap a question", NEV_COL_INK_FAINT);
            break;

        default:
            break;
    }
}

static void agent_tick(uint32_t now_ms) {
    nev_talk_tick(now_ms);
}

static void agent_close(void) {
    nev_talk_detach();
    s_question = s_reply = s_status = s_prompts = NULL;
    s_waiting = false;
}

static const nev_app_desc_t kAgentApp = {
    .id = "agent",
    .name = "Ask",
    .icon = LV_SYMBOL_CALL,
    .category = NEV_APP_CAT_PRODUCTIVITY,
    .memory_budget_kb = 24,
    .requires_bridge = true,
    .on_launch = agent_launch,
    .on_event = agent_event,
    .on_tick = agent_tick,
    .on_close = agent_close,
};

NEV_APP_REGISTER(kAgentApp);
