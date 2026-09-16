/*
 * NEVOS — focus.
 *
 * A timer you set by tapping a number and start by tapping the ring. It sits
 * there counting down, the face concentrates with you, and it makes a noise
 * when you are done.
 *
 * Everything about it is arranged around the fact that it is watched from
 * across a desk while doing something else: the remaining time is the largest
 * thing on the screen, the ring makes "nearly finished" readable without
 * reading, and the device's own brightness policy is left alone rather than
 * fought — a focus timer that keeps the screen at full brightness for
 * twenty-five minutes is a lamp.
 */
#include <stdio.h>

#include "nev_appkit/app.h"
#include "nev_appkit/theme.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_store.h"
#include "nev_persona/persona.h"
#include "nev_port/nev_time.h"
#include "nev_services/sound_service.h"

/* Minutes. Three is enough: a short one, the usual one, and a long one. A
 * picker with every value from 1 to 60 is a worse version of a keyboard. */
static const uint16_t kPresets[] = {5, 15, 25};
#define PRESET_COUNT (sizeof(kPresets) / sizeof(kPresets[0]))

typedef enum { FOCUS_IDLE, FOCUS_RUNNING, FOCUS_PAUSED, FOCUS_DONE } focus_state_t;

static lv_obj_t *s_ring;
static lv_obj_t *s_time;
static lv_obj_t *s_caption;
static lv_obj_t *s_presets;

static focus_state_t s_state;
static uint32_t s_total_s;
static uint32_t s_left_s;
static uint32_t s_last_tick_ms;
static uint8_t s_choice;

static void repaint(void) {
    lv_label_set_text_fmt(s_time, "%02u:%02u", (unsigned)(s_left_s / 60u),
                          (unsigned)(s_left_s % 60u));

    const int32_t pct = s_total_s ? (int32_t)(100u - (s_left_s * 100u / s_total_s)) : 0;
    nev_ui_progress_ring_set(s_ring, pct);

    switch (s_state) {
        case FOCUS_IDLE:
            lv_label_set_text(s_caption, "Tap to start");
            lv_obj_remove_flag(s_presets, LV_OBJ_FLAG_HIDDEN);
            break;
        case FOCUS_RUNNING:
            lv_label_set_text(s_caption, "Tap to pause");
            lv_obj_add_flag(s_presets, LV_OBJ_FLAG_HIDDEN);
            break;
        case FOCUS_PAUSED:
            lv_label_set_text(s_caption, "Paused");
            lv_obj_add_flag(s_presets, LV_OBJ_FLAG_HIDDEN);
            break;
        case FOCUS_DONE:
            lv_label_set_text(s_caption, "Done");
            lv_obj_remove_flag(s_presets, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

static void set_choice(uint8_t index) {
    if (index >= PRESET_COUNT) return;
    s_choice = index;
    s_total_s = (uint32_t)kPresets[index] * 60u;
    s_left_s = s_total_s;
    s_state = FOCUS_IDLE;

    /* Remembered, because most people use the same length every time and
     * re-picking it twice a day is the kind of small friction that makes a
     * feature stop being used. */
    (void)nev_store_set_num(NEV_SET_FOCUS_MINUTES, kPresets[index]);

    for (uint32_t i = 0; i < PRESET_COUNT; i++) {
        lv_obj_t *b = lv_obj_get_child(s_presets, (int32_t)i);
        if (!b) continue;
        lv_obj_set_style_text_color(b, i == index ? NEV_COL_ACCENT : NEV_COL_INK_MUTED,
                                    LV_PART_MAIN);
    }
    repaint();
}

static void preset_clicked(lv_event_t *e) {
    set_choice((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void ring_clicked(lv_event_t *e) {
    (void)e;
    switch (s_state) {
        case FOCUS_IDLE:
        case FOCUS_DONE:
            if (s_left_s == 0) s_left_s = s_total_s;
            s_state = FOCUS_RUNNING;
            s_last_tick_ms = nev_now_ms();
            /* The face concentrates for as long as the timer runs. It is the
             * one thing on the device that can show "we are doing this" while
             * the screen is dim and nobody is looking at the numbers. */
            nev_persona_set_mood(NEV_MOOD_FOCUSED, 200, 0);
            break;

        case FOCUS_RUNNING:
            s_state = FOCUS_PAUSED;
            nev_persona_set_mood(NEV_MOOD_IDLE, 255, 0);
            break;

        case FOCUS_PAUSED:
            s_state = FOCUS_RUNNING;
            s_last_tick_ms = nev_now_ms();
            nev_persona_set_mood(NEV_MOOD_FOCUSED, 200, 0);
            break;
    }
    repaint();
}

static nev_err_t focus_launch(lv_obj_t *root) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, NEV_SP_4, 0);

    s_ring = nev_ui_progress_ring(root, 260);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ring, ring_clicked, LV_EVENT_CLICKED, NULL);

    /* Inside the ring, because a number and a ring that disagree about where
     * the middle of the screen is look like two widgets rather than one clock. */
    s_time = nev_ui_label(s_ring, "25:00", NEV_FONT_DISPLAY, NEV_COL_INK);
    lv_obj_center(s_time);

    s_caption = nev_ui_label(root, "Tap to start", NEV_FONT_BODY, NEV_COL_INK_MUTED);

    s_presets = lv_obj_create(root);
    lv_obj_remove_style_all(s_presets);
    lv_obj_set_size(s_presets, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_presets, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_presets, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    for (uint32_t i = 0; i < PRESET_COUNT; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%u", (unsigned)kPresets[i]);
        nev_ui_button_ghost(s_presets, label, preset_clicked, (void *)(uintptr_t)i);
    }

    /* Whatever was used last time, or the middle preset for a new device. */
    const uint32_t saved = nev_store_num(NEV_SET_FOCUS_MINUTES);
    uint8_t choice = 1;
    for (uint32_t i = 0; i < PRESET_COUNT; i++) {
        if (kPresets[i] == saved) choice = (uint8_t)i;
    }
    set_choice(choice);
    return NEV_OK;
}

static void focus_tick(uint32_t now_ms) {
    if (s_state != FOCUS_RUNNING) return;

    /*
     * Counted from the clock rather than by decrementing once a frame. A frame
     * that took 40 ms instead of 33 would otherwise make a 25-minute timer run
     * long by a couple of minutes, and the error is invisible until someone
     * checks it against a phone.
     */
    const uint32_t elapsed = nev_elapsed_ms(s_last_tick_ms);
    if (elapsed < 1000u) return;

    const uint32_t seconds = elapsed / 1000u;
    s_last_tick_ms += seconds * 1000u;
    s_left_s = (s_left_s > seconds) ? s_left_s - seconds : 0;

    if (s_left_s == 0) {
        s_state = FOCUS_DONE;
        nev_sound_play(NEV_CUE_ALERT);
        nev_persona_set_mood(NEV_MOOD_CELEBRATING, 220, 3000);
    }
    repaint();
}

static void focus_close(void) {
    /*
     * The timer stops with the app.
     *
     * A countdown that kept running invisibly and then made a noise from the
     * home screen would be a small betrayal: the device would be doing
     * something the person could no longer see or cancel. Meeting mode is the
     * opposite case and survives on purpose — the difference is that a
     * recording is data being collected, and a timer is a promise to interrupt.
     */
    if (s_state == FOCUS_RUNNING) nev_persona_set_mood(NEV_MOOD_IDLE, 255, 0);
    s_state = FOCUS_IDLE;
    s_ring = s_time = s_caption = s_presets = NULL;
}

static const nev_app_desc_t kFocusApp = {
    .id = "focus",
    .name = "Focus",
    .icon = LV_SYMBOL_LOOP,
    .category = NEV_APP_CAT_PRODUCTIVITY,
    .memory_budget_kb = 12,
    .requires_bridge = false,
    .on_launch = focus_launch,
    .on_tick = focus_tick,
    .on_close = focus_close,
};

NEV_APP_REGISTER(kFocusApp);
