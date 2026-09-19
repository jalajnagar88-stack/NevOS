/*
 * NEVOS — notes.
 *
 * Hold the button, say the thing, let go. The daemon transcribes it and files
 * it on the computer; this screen shows what it heard.
 *
 * The notes themselves live on the companion machine, as plain files the owner
 * can read, back up and delete with anything. The device deliberately keeps no
 * copy: it has 512 KB of RAM and no encryption at rest, and a list of somebody's
 * private notes is the last thing that should be sitting in a desk ornament's
 * flash. What is shown here is this session's captures, and it is gone when the
 * app closes.
 *
 * Browsing older notes would need a new protocol message — the schema has no
 * way to ask the daemon for a list, and adding one is a deliberate decision
 * about what the device is allowed to hold, not an oversight to paper over
 * here.
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

/* This session only, and bounded: the screen shows three at a time and the
 * point of the app is the capture, not the archive. */
#define MAX_NOTES  6
#define NOTE_CHARS 160

static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;

static char s_notes[MAX_NOTES][NOTE_CHARS];
static uint8_t s_count;

static void set_status(const char *text, lv_color_t colour) {
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

static void rebuild_list(void) {
    lv_obj_clean(s_list);
    for (int i = (int)s_count - 1; i >= 0; i--) {
        lv_obj_t *card = nev_ui_panel(s_list, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(card, NEV_SP_3, 0);
        lv_obj_t *text = nev_ui_label(card, s_notes[i], NEV_FONT_BODY, NEV_COL_INK);
        lv_obj_set_width(text, LV_PCT(100));
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    }
    if (s_count > 0) {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_note(const char *text) {
    if (!text || text[0] == '\0') return;

    if (s_count == MAX_NOTES) {
        /* Oldest out. The note itself is safe on the computer; this is a view. */
        memmove(s_notes[0], s_notes[1], sizeof(s_notes) - sizeof(s_notes[0]));
        s_count--;
    }
    snprintf(s_notes[s_count], NOTE_CHARS, "%s", text);
    s_count++;
    rebuild_list();
}

/*
 * What the microphone is doing, in the status line.
 *
 * The one that matters is DISCARDED: a capture thrown away for being a tenth of
 * a second long looks exactly like one the daemon could not hear, and the user
 * fixes the two in completely different ways.
 */
static void talk_changed(nev_talk_state_t state, const char *detail) {
    switch (state) {
        case NEV_TALK_LISTENING:
            set_status("Listening", NEV_COL_SUCCESS);
            break;
        case NEV_TALK_SENT:
            set_status("Transcribing...", NEV_COL_INK_FAINT);
            break;
        case NEV_TALK_DISCARDED:
        case NEV_TALK_UNAVAILABLE:
            set_status(detail, NEV_COL_WARN);
            break;
        case NEV_TALK_IDLE:
        default:
            break;
    }
}

static nev_err_t notes_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, NEV_SP_4, 0);
    lv_obj_set_style_pad_row(root, NEV_SP_3, 0);

    s_list = lv_obj_create(root);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, NEV_SP_2, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    s_hint =
        nev_ui_label(root, "Hold the button and say something", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    s_status = nev_ui_label(root, "", NEV_FONT_CAPTION, NEV_COL_INK_FAINT);

    /* The on-screen button and button A are the same gesture; nev_talk owns
     * both. It is built last so it sits at the bottom, under the thumb. */
    nev_talk_attach(root, NEV_AUDIO_KIND_NOTE, talk_changed);

    s_count = 0;
    rebuild_list();

    if (nev_bridge_state() == NEV_BRIDGE_READY) {
        lv_label_set_text_fmt(s_status, "Saving to %s", nev_bridge_daemon_name());
    } else {
        set_status("Not connected. Nothing can be saved right now.", NEV_COL_WARN);
    }
    return NEV_OK;
}

static void notes_event(const nev_event_t *ev) {
    /* Button A is hold-to-talk. Given to nev_talk first and unconditionally:
     * a release must reach the state machine even when the app has decided it
     * has nothing else to do with it, or the microphone stays open. */
    nev_talk_event(ev);

    switch (ev->type) {
        case NEV_EVT_BRIDGE_TRANSCRIPT_PARTIAL: {
            const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
            if (text && text[0]) set_status(text, NEV_COL_INK_FAINT);
            break;
        }

        case NEV_EVT_BRIDGE_TRANSCRIPT_FINAL: {
            const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
            if (text && text[0] != '\0') {
                add_note(text);
                lv_label_set_text_fmt(s_status, "Saved to %s", nev_bridge_daemon_name());
            } else {
                /* An empty final is the daemon saying it heard nothing usable.
                 * Saying so is better than a silence the user reads as a bug. */
                set_status("I did not catch that", NEV_COL_WARN);
            }
            break;
        }

        case NEV_EVT_BRIDGE_DISCONNECTED:
            /* Recording into a link that is gone wastes the user's breath, and
             * unlike meeting mode there is nothing to salvage: a note is only
             * worth anything once the computer has transcribed it. */
            nev_talk_stop();
            set_status("Not connected. Nothing can be saved right now.", NEV_COL_WARN);
            break;

        case NEV_EVT_BRIDGE_CONNECTED:
            lv_label_set_text_fmt(s_status, "Saving to %s", nev_bridge_daemon_name());
            lv_obj_set_style_text_color(s_status, NEV_COL_INK_FAINT, 0);
            break;

        default:
            break;
    }
}

static void notes_tick(uint32_t now_ms) {
    /* Without this a held button never arms, because arming is a decision about
     * elapsed time and nothing else calls the state machine. */
    nev_talk_tick(now_ms);
}

static void notes_close(void) {
    nev_talk_detach();
    /* The session's notes go with the app. They are on the computer. */
    memset(s_notes, 0, sizeof(s_notes));
    s_count = 0;
    s_list = s_status = s_hint = NULL;
}

static const nev_app_desc_t kNotesApp = {
    .id = "notes",
    .name = "Notes",
    .icon = LV_SYMBOL_EDIT,
    .category = NEV_APP_CAT_PRODUCTIVITY,
    .memory_budget_kb = 20,
    .requires_bridge = true,
    .on_launch = notes_launch,
    .on_event = notes_event,
    .on_tick = notes_tick,
    .on_close = notes_close,
};

NEV_APP_REGISTER(kNotesApp);
