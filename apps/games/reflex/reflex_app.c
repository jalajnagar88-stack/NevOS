/*
 * NEVOS — Reflex. A reaction-time trainer.
 *
 * Rules in reflex_core.c. This file is the presentation: a target that fills
 * the field so there is nothing to aim at, and per-round feedback that is
 * readable in the moment rather than only at the end.
 */
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_game/game.h"
#include "reflex_core.h"
#include <string.h>

typedef struct {
    reflex_core_t core;
    lv_obj_t *target;
    lv_obj_t *prompt;
    lv_obj_t *detail;
    uint32_t last_ms;
} reflex_view_t;

static reflex_view_t s_view;
static nev_game_t s_game;

static uint32_t core_rand(void *ctx, uint32_t upper) {
    return nev_game_rand((nev_game_t *)ctx, upper);
}

static void repaint(reflex_view_t *v) {
    switch (v->core.state) {
        case RX_WAITING:
            lv_obj_set_style_bg_color(v->target, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
            lv_label_set_text(v->prompt, "Wait");
            lv_obj_set_style_text_color(v->prompt, NEV_COL_INK_FAINT, LV_PART_MAIN);
            break;

        case RX_ARMED:
            /* The target fills the field: this measures reaction, not aim. */
            lv_obj_set_style_bg_color(v->target, NEV_COL_SUCCESS, LV_PART_MAIN);
            lv_label_set_text(v->prompt, "TAP");
            lv_obj_set_style_text_color(v->prompt, NEV_COL_BG, LV_PART_MAIN);
            break;

        case RX_FEEDBACK:
            if (v->core.last_too_soon) {
                lv_obj_set_style_bg_color(v->target, NEV_COL_DANGER, LV_PART_MAIN);
                lv_label_set_text(v->prompt, "Too soon");
                lv_obj_set_style_text_color(v->prompt, NEV_COL_INK, LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_color(v->target, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
                lv_label_set_text_fmt(v->prompt, "%u ms", (unsigned)v->core.last_ms);
                lv_obj_set_style_text_color(v->prompt, NEV_COL_INK, LV_PART_MAIN);
            }
            break;

        case RX_FINISHED:
            lv_obj_set_style_bg_color(v->target, NEV_COL_SURFACE_ALT, LV_PART_MAIN);
            lv_label_set_text_fmt(v->prompt, "%u ms avg", (unsigned)reflex_core_average(&v->core));
            lv_obj_set_style_text_color(v->prompt, NEV_COL_INK, LV_PART_MAIN);
            break;
    }

    if (v->core.state == RX_FINISHED) {
        lv_label_set_text_fmt(v->detail, "best this run %u ms",
                              (unsigned)reflex_core_best(&v->core));
    } else {
        lv_label_set_text_fmt(v->detail, "round %u of %u",
                              (unsigned)(v->core.round + (v->core.state == RX_FEEDBACK ? 0 : 1)),
                              RX_ROUNDS);
    }
}

static void reflex_start(nev_game_t *g) {
    reflex_view_t *v = g->user;
    reflex_core_reset(&v->core, core_rand, g);
    v->last_ms = 0;
    repaint(v);
}

static void reflex_update(nev_game_t *g, float dt) {
    reflex_view_t *v = g->user;
    const uint32_t dt_ms = (uint32_t)(dt * 1000.0f + 0.5f);

    const rx_state_t before = v->core.state;
    if (reflex_core_step(&v->core, dt_ms, core_rand, g)) {
        nev_game_burst(g, NEV_GAME_FIELD / 2, NEV_GAME_FIELD / 2, NEV_COL_SUCCESS, 6, 140.0f);
    }
    if (v->core.state != before) repaint(v);

    if (v->core.state == RX_FINISHED) {
        nev_game_add_score(g, (int32_t)reflex_core_score(&v->core));
        nev_game_over(g);
    }
}

static void handle_tap(nev_game_t *g) {
    reflex_view_t *v = g->user;
    switch (reflex_core_tap(&v->core)) {
        case RX_TAP_COUNTED:
            nev_game_burst(g, NEV_GAME_FIELD / 2, NEV_GAME_FIELD / 2, NEV_COL_ACCENT, 8, 150.0f);
            break;
        case RX_TAP_TOO_SOON:
            nev_game_shake(g, 0.45f);
            nev_game_burst(g, NEV_GAME_FIELD / 2, NEV_GAME_FIELD / 2, NEV_COL_DANGER, 10, 170.0f);
            break;
        case RX_TAP_IGNORED:
            return;
    }
    repaint(v);
}

static void target_clicked_cb(lv_event_t *e) {
    (void)e;
    if (s_game.state == NEV_GAME_PLAYING) handle_tap(&s_game);
}

static void reflex_input(nev_game_t *g, const nev_event_t *ev) {
    if (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == 0) handle_tap(g);
}

static const nev_game_def_t kReflexDef = {
    .id = "reflex",
    .title = "Reflex",
    .how_to = "Tap the moment it turns green " LV_SYMBOL_BULLET " 5 rounds",
    .highscore_key = NEV_SET_HS_REFLEX,
    .tick_hz = 120, /* the clock this game reports is its whole point */
    .on_start = reflex_start,
    .on_update = reflex_update,
    .on_input = reflex_input,
};

static nev_err_t reflex_launch(lv_obj_t *root) {
    memset(&s_view, 0, sizeof(s_view));
    NEV_TRY(nev_game_begin(&s_game, root, &kReflexDef));
    s_game.user = &s_view;

    s_view.target = lv_obj_create(s_game.field);
    lv_obj_remove_style_all(s_view.target);
    lv_obj_set_size(s_view.target, NEV_GAME_FIELD, NEV_GAME_FIELD);
    lv_obj_set_style_bg_opa(s_view.target, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.target, NEV_RADIUS_MD, LV_PART_MAIN);
    lv_obj_add_flag(s_view.target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_view.target, target_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_view.prompt = nev_ui_label(s_view.target, "Wait", NEV_FONT_DISPLAY, NEV_COL_INK_FAINT);
    lv_obj_align(s_view.prompt, LV_ALIGN_CENTER, 0, -14);

    s_view.detail = nev_ui_label(s_view.target, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_align(s_view.detail, LV_ALIGN_CENTER, 0, 34);
    return NEV_OK;
}

static void reflex_tick(uint32_t now_ms) {
    nev_game_frame(&s_game, now_ms);
}
static void reflex_event(const nev_event_t *ev) {
    nev_game_event(&s_game, ev);
}
static void reflex_close(void) {
    nev_game_end(&s_game);
}

static const nev_app_desc_t kReflexApp = {
    .id = "reflex",
    .name = "Reflex",
    .icon = LV_SYMBOL_EYE_OPEN,
    .category = NEV_APP_CAT_GAME,
    .memory_budget_kb = 20,
    .on_launch = reflex_launch,
    .on_tick = reflex_tick,
    .on_event = reflex_event,
    .on_close = reflex_close,
};

NEV_APP_REGISTER(kReflexApp);
