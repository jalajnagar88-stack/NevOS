/*
 * NEVOS — meeting.
 *
 * Long-form capture. Press record, put the device down, press it again when
 * the meeting is over. What was said appears on the computer as a transcript;
 * the button in the middle marks a moment worth finding again.
 *
 * Almost nothing happens here. The audio service captures, the bridge sends,
 * the daemon transcribes in segments and files the result. This screen's job is
 * to make a recording that runs for an hour feel like something that is
 * definitely working, which mostly means an elapsed time that moves, a live
 * line of transcript, and an indicator that cannot be mistaken for anything
 * else.
 */
#include <stdio.h>
#include <string.h>

#include "nev_appkit/app.h"
#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_services/audio_service.h"

static lv_obj_t *s_dot;
static lv_obj_t *s_elapsed;
static lv_obj_t *s_heard;
static lv_obj_t *s_record;
static lv_obj_t *s_mark;
static lv_obj_t *s_hint;

static uint32_t s_markers;
static uint32_t s_painted_second = 0xFFFFFFFFu;

static bool running(void) {
    return audio_service_is_capturing();
}

static void repaint(void) {
    const bool live = running();

    lv_obj_set_style_bg_color(s_dot, live ? NEV_COL_DANGER : NEV_COL_INK_FAINT, 0);
    lv_label_set_text(lv_obj_get_child(s_record, 0), live ? "Stop" : "Record");

    if (live) {
        lv_obj_remove_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }

    const uint32_t ms = audio_service_elapsed_ms();
    const uint32_t total = ms / 1000u;
    lv_label_set_text_fmt(s_elapsed, "%u:%02u:%02u", (unsigned)(total / 3600u),
                          (unsigned)((total / 60u) % 60u), (unsigned)(total % 60u));
}

static void record_clicked(lv_event_t *e) {
    (void)e;
    if (running()) {
        audio_service_stop();
        lv_label_set_text(s_heard, "Filing the transcript...");
    } else {
        s_markers = 0;
        if (audio_service_start(NEV_AUDIO_KIND_TRANSCRIPT) == 0) {
            lv_label_set_text(s_heard, "The microphone would not start.");
            return;
        }
        lv_label_set_text(s_heard, "");
    }
    repaint();
}

static void mark_clicked(lv_event_t *e) {
    (void)e;
    const uint32_t session = audio_service_session();
    if (session == 0) return;

    /*
     * The device's own count, not the daemon's. The daemon is a segment behind
     * — it is still transcribing what was said a minute ago — so a marker timed
     * there would land a minute late, in the middle of a different sentence.
     */
    const float at = (float)audio_service_elapsed_ms() / 1000.0f;
    if (!nev_bridge_mark(session, at)) {
        lv_label_set_text(s_heard, "Could not mark that.");
        return;
    }
    s_markers++;
    lv_label_set_text_fmt(s_heard, "Marked at %u:%02u  (%u so far)", (unsigned)((uint32_t)at / 60u),
                          (unsigned)((uint32_t)at % 60u), (unsigned)s_markers);
}

static nev_err_t meeting_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root, NEV_SP_4, 0);
    lv_obj_set_style_pad_row(root, NEV_SP_3, 0);

    s_dot = lv_obj_create(root);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 16, 16);
    lv_obj_set_style_radius(s_dot, NEV_RADIUS_FULL, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);

    s_elapsed = nev_ui_label(root, "0:00:00", NEV_FONT_DISPLAY, NEV_COL_INK);

    s_heard = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_set_width(s_heard, LV_PCT(100));
    lv_label_set_long_mode(s_heard, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_heard, LV_TEXT_ALIGN_CENTER, 0);

    s_record = nev_ui_button(root, "Record", record_clicked, NULL);
    lv_obj_set_width(s_record, LV_PCT(70));

    s_mark = nev_ui_button_ghost(root, "Mark this moment", mark_clicked, NULL);
    lv_obj_set_width(s_mark, LV_PCT(70));

    s_hint = nev_ui_label(root, "Saved to your computer as a transcript", NEV_FONT_CAPTION,
                          NEV_COL_INK_FAINT);

    s_markers = 0;
    s_painted_second = 0xFFFFFFFFu;
    repaint();
    return NEV_OK;
}

static void meeting_event(const nev_event_t *ev) {
    switch (ev->type) {
        case NEV_EVT_BRIDGE_TRANSCRIPT_PARTIAL: {
            /* The last thing the daemon finished transcribing. Seeing words
             * arrive is the difference between a recording you trust and a
             * timer you hope is doing something. */
            const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
            if (text && text[0]) lv_label_set_text(s_heard, text);
            break;
        }

        case NEV_EVT_BRIDGE_TRANSCRIPT_FINAL: {
            const char *text = (const char *)nev_blob_data(ev->p.blob.handle);
            if (text && text[0]) {
                lv_label_set_text_fmt(s_heard, "Saved. %s", text);
            } else {
                lv_label_set_text(s_heard, "Saved.");
            }
            break;
        }

        case NEV_EVT_BRIDGE_DISCONNECTED:
            /*
             * Recording continues. The audio goes nowhere, which is a real
             * loss, but stopping the capture because the laptop shut its lid
             * would throw away the part the user could still have had.
             */
            if (running()) lv_label_set_text(s_heard, "Lost the computer. Still recording.");
            break;

        default:
            break;
    }
}

static void meeting_tick(uint32_t now_ms) {
    (void)now_ms;
    /* Once a second: the elapsed label is the only thing that changes, and
     * redrawing it at 30 fps dirties a rectangle 29 times for nothing. */
    const uint32_t second = audio_service_elapsed_ms() / 1000u;
    if (second == s_painted_second) return;
    s_painted_second = second;
    repaint();
}

static void meeting_close(void) {
    /*
     * A capture survives the app closing. Meeting mode exists to run while the
     * device is doing something else — showing the clock, asleep on a desk —
     * and stopping the recording because someone swiped home would be the worst
     * possible reading of that gesture.
     */
    s_dot = s_elapsed = s_heard = s_record = s_mark = s_hint = NULL;
}

static const nev_app_desc_t kMeetingApp = {
    .id = "meeting",
    .name = "Meeting",
    .icon = LV_SYMBOL_AUDIO,
    .category = NEV_APP_CAT_PRODUCTIVITY,
    .memory_budget_kb = 16,
    .requires_bridge = true,
    .on_launch = meeting_launch,
    .on_event = meeting_event,
    .on_tick = meeting_tick,
    .on_close = meeting_close,
};

NEV_APP_REGISTER(kMeetingApp);
