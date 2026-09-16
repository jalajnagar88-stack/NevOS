/*
 * NEVOS — Runner. An original endless side-scroller.
 *
 * Rules in runner_core.c, unit-tested without a display — including the two
 * things that decide whether a one-button runner feels fair: coyote time and
 * input buffering.
 */
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_game/game.h"
#include "runner_core.h"
#include <string.h>

typedef struct {
    runner_core_t core;
    lv_obj_t *player;
    lv_obj_t *ground;
    lv_obj_t *obs[RN_MAX_OBS];
    uint32_t scored_distance;
} runner_view_t;

static runner_view_t s_view;
static nev_game_t s_game;

static uint32_t core_rand(void *ctx, uint32_t upper) {
    return nev_game_rand((nev_game_t *)ctx, upper);
}

static void redraw(runner_view_t *v) {
    lv_obj_set_pos(v->player, RN_PLAYER_X, (int32_t)v->core.player_y);

    for (int i = 0; i < RN_MAX_OBS; i++) {
        if (!v->core.obs[i].active) {
            lv_obj_add_flag(v->obs[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_size(v->obs[i], v->core.obs[i].w, v->core.obs[i].h);
        lv_obj_set_pos(v->obs[i], (int32_t)v->core.obs[i].x, RN_GROUND_Y - v->core.obs[i].h);
        lv_obj_remove_flag(v->obs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void runner_start(nev_game_t *g) {
    runner_view_t *v = g->user;
    runner_core_reset(&v->core);
    v->scored_distance = 0;
    redraw(v);
}

static void runner_update(nev_game_t *g, float dt) {
    runner_view_t *v = g->user;
    runner_step_t out;
    runner_core_step(&v->core, dt, core_rand, g, &out);

    if (out.crashed) {
        nev_game_shake(g, 0.7f);
        nev_game_burst(g, (int32_t)out.crash_x, RN_GROUND_Y - 16, NEV_COL_DANGER, 14, 210.0f);
        nev_game_over(g);
        return;
    }
    if (out.landed)
        nev_game_burst(g, RN_PLAYER_X + RN_PLAYER_W / 2, RN_GROUND_Y - 2, NEV_COL_INK_FAINT, 3,
                       60.0f);
    if (out.cleared) nev_game_burst(g, RN_PLAYER_X, RN_GROUND_Y - 30, NEV_COL_SUCCESS, 4, 90.0f);

    /* Distance is the score, awarded in whole units so the number is readable
     * rather than a blur. */
    const uint32_t units = (uint32_t)(v->core.distance / 12.0f);
    if (units > v->scored_distance) {
        nev_game_add_score(g, (int32_t)(units - v->scored_distance));
        v->scored_distance = units;
    }
    redraw(v);
}

static void runner_input(nev_game_t *g, const nev_event_t *ev) {
    runner_view_t *v = g->user;
    const bool pressed =
        (ev->type == NEV_EVT_INPUT_TOUCH && ev->p.touch.action == NEV_TOUCH_ACT_DOWN) ||
        (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == 0);
    if (pressed) runner_core_jump(&v->core);
}

static const nev_game_def_t kRunnerDef = {
    .id = "runner",
    .title = "Runner",
    .how_to = "Tap or press A to jump",
    .highscore_key = NEV_SET_HS_RUNNER,
    .tick_hz = 120, /* fast scroll against thin obstacles: 60 Hz can tunnel */
    .on_start = runner_start,
    .on_update = runner_update,
    .on_input = runner_input,
};

static nev_err_t runner_launch(lv_obj_t *root) {
    memset(&s_view, 0, sizeof(s_view));
    NEV_TRY(nev_game_begin(&s_game, root, &kRunnerDef));
    s_game.user = &s_view;

    lv_obj_t *field = s_game.field;

    s_view.ground = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.ground);
    lv_obj_set_size(s_view.ground, NEV_GAME_FIELD, 2);
    lv_obj_set_pos(s_view.ground, 0, RN_GROUND_Y);
    lv_obj_set_style_bg_color(s_view.ground, NEV_COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.ground, LV_OPA_COVER, LV_PART_MAIN);

    for (int i = 0; i < RN_MAX_OBS; i++) {
        lv_obj_t *o = lv_obj_create(field);
        lv_obj_remove_style_all(o);
        lv_obj_set_style_radius(o, NEV_RADIUS_SM, LV_PART_MAIN);
        lv_obj_set_style_bg_color(o, NEV_COL_WARN, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        s_view.obs[i] = o;
    }

    s_view.player = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.player);
    lv_obj_set_size(s_view.player, RN_PLAYER_W, RN_PLAYER_H);
    lv_obj_set_style_radius(s_view.player, NEV_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.player, NEV_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.player, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    return NEV_OK;
}

static void runner_tick(uint32_t now_ms) {
    nev_game_frame(&s_game, now_ms);
}
static void runner_event(const nev_event_t *ev) {
    nev_game_event(&s_game, ev);
}
static void runner_close(void) {
    nev_game_end(&s_game);
}

static const nev_app_desc_t kRunnerApp = {
    .id = "runner",
    .name = "Runner",
    .icon = LV_SYMBOL_RIGHT,
    .category = NEV_APP_CAT_GAME,
    .memory_budget_kb = 36,
    .on_launch = runner_launch,
    .on_tick = runner_tick,
    .on_event = runner_event,
    .on_close = runner_close,
};

NEV_APP_REGISTER(kRunnerApp);
