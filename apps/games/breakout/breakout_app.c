/*
 * NEVOS — Breakout. An original implementation of the paddle-and-bricks genre.
 *
 * Rules in breakout_core.c, unit-tested without a display. This file is the
 * renderer and the input: the paddle is dragged directly (ADR 0011), which
 * works identically in the simulator and on hardware. Tilt joins it at M5.
 */
#include "breakout_core.h"
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_game/game.h"
#include <string.h>

#define SCORE_PER_HIT   10
#define SCORE_PER_LEVEL 100

typedef struct {
    breakout_core_t core;
    lv_obj_t *brick[BO_BRICKS];
    lv_obj_t *ball;
    lv_obj_t *paddle;
    lv_obj_t *drop[BO_MAX_DROPS];
    lv_obj_t *lives_label;
} breakout_view_t;

static breakout_view_t s_view;
static nev_game_t s_game;

static uint32_t core_rand(void *ctx, uint32_t upper) {
    return nev_game_rand((nev_game_t *)ctx, upper);
}

static lv_color_t drop_color(bo_drop_t kind) {
    switch (kind) {
        case BO_DROP_WIDE:
            return NEV_COL_ACCENT;
        case BO_DROP_SLOW:
            return NEV_COL_WARN;
        case BO_DROP_LIFE:
            return NEV_COL_SUCCESS;
        default:
            return NEV_COL_INK_FAINT;
    }
}

static void redraw(breakout_view_t *v) {
    for (uint8_t i = 0; i < BO_BRICKS; i++) {
        if (v->core.brick[i].hp == 0) {
            lv_obj_add_flag(v->brick[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(v->brick[i], LV_OBJ_FLAG_HIDDEN);
        /* A two-hit brick is visibly solid rather than merely a different hue:
         * the player has to be able to see what will not break in one go. */
        lv_obj_set_style_bg_opa(v->brick[i], v->core.brick[i].hp > 1 ? LV_OPA_COVER : LV_OPA_50,
                                LV_PART_MAIN);
    }

    lv_obj_set_pos(v->ball, (int32_t)v->core.ball_x - BO_BALL_R,
                   (int32_t)v->core.ball_y - BO_BALL_R);

    lv_obj_set_size(v->paddle, (int32_t)v->core.paddle_w, BO_PADDLE_H);
    lv_obj_set_pos(v->paddle, (int32_t)(v->core.paddle_x - v->core.paddle_w / 2.0f), BO_PADDLE_Y);

    for (int i = 0; i < BO_MAX_DROPS; i++) {
        if (!v->core.drop[i].active) {
            lv_obj_add_flag(v->drop[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_style_bg_color(v->drop[i], drop_color(v->core.drop[i].kind), LV_PART_MAIN);
        lv_obj_set_pos(v->drop[i], (int32_t)v->core.drop[i].x - 7, (int32_t)v->core.drop[i].y - 7);
        lv_obj_remove_flag(v->drop[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(v->lives_label, "L%u  " LV_SYMBOL_BULLET "  %u " LV_SYMBOL_CHARGE,
                          (unsigned)v->core.level, (unsigned)v->core.lives);
}

static void breakout_start(nev_game_t *g) {
    breakout_view_t *v = g->user;
    breakout_core_reset(&v->core);
    breakout_core_start_level(&v->core, 1, core_rand, g);
    redraw(v);
}

static void breakout_update(nev_game_t *g, float dt) {
    breakout_view_t *v = g->user;
    breakout_step_t out;
    breakout_core_step(&v->core, dt, core_rand, g, &out);

    if (out.bricks_broken) {
        nev_game_add_score(g, SCORE_PER_HIT * out.bricks_broken);
        nev_game_burst(g, out.hit_x, out.hit_y, NEV_COL_ACCENT, 6, 130.0f);
    }
    if (out.caught != BO_DROP_NONE) {
        nev_game_burst(g, (int32_t)v->core.paddle_x, BO_PADDLE_Y, drop_color(out.caught), 8,
                       100.0f);
        nev_game_add_score(g, 25);
    }
    if (out.lost_life) {
        nev_game_shake(g, 0.5f);
        nev_game_burst(g, (int32_t)v->core.ball_x, BO_FIELD - 8, NEV_COL_DANGER, 12, 170.0f);
    }
    if (out.level_cleared) {
        nev_game_add_score(g, SCORE_PER_LEVEL);
        nev_game_shake(g, 0.3f);
        breakout_core_start_level(&v->core, (uint8_t)(v->core.level + 1), core_rand, g);
    }
    if (out.game_over) {
        nev_game_over(g);
        return;
    }
    redraw(v);
}

/* Dragging: the paddle follows the finger directly rather than accelerating
 * toward it. A desk toy played in short bursts should not also require
 * learning a control. */
static void field_pressing_cb(lv_event_t *e) {
    breakout_view_t *v = lv_event_get_user_data(e);
    if (s_game.state != NEV_GAME_PLAYING) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t a;
    lv_obj_get_coords(s_game.field, &a);
    breakout_core_move_paddle(&v->core, (float)(p.x - a.x1));
    redraw(v);
}

static void field_clicked_cb(lv_event_t *e) {
    breakout_view_t *v = lv_event_get_user_data(e);
    if (s_game.state == NEV_GAME_PLAYING) breakout_core_launch(&v->core);
}

static void breakout_input(nev_game_t *g, const nev_event_t *ev) {
    breakout_view_t *v = g->user;
    if (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == 0) {
        breakout_core_launch(&v->core);
    }
}

static const nev_game_def_t kBreakoutDef = {
    .id = "breakout",
    .title = "Breakout",
    .how_to = "Drag to move " LV_SYMBOL_BULLET " Tap to launch",
    .highscore_key = NEV_SET_HS_BREAKOUT,
    .tick_hz = 120, /* fast ball, small paddle: 60 Hz can tunnel through both */
    .on_start = breakout_start,
    .on_update = breakout_update,
    .on_input = breakout_input,
};

static nev_err_t breakout_launch(lv_obj_t *root) {
    memset(&s_view, 0, sizeof(s_view));
    NEV_TRY(nev_game_begin(&s_game, root, &kBreakoutDef));
    s_game.user = &s_view;

    lv_obj_t *field = s_game.field;

    for (uint8_t i = 0; i < BO_BRICKS; i++) {
        lv_obj_t *b = lv_obj_create(field);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, BO_BRICK_W - 3, BO_BRICK_H);
        lv_obj_set_pos(b, bo_brick_x(i) + 1, bo_brick_y(i));
        lv_obj_set_style_radius(b, NEV_RADIUS_SM, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, nev_theme_play_color(i / BO_COLS), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_PART_MAIN);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_view.brick[i] = b;
    }

    s_view.paddle = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.paddle);
    lv_obj_set_style_radius(s_view.paddle, NEV_RADIUS_FULL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.paddle, NEV_COL_INK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.paddle, LV_OPA_COVER, LV_PART_MAIN);

    s_view.ball = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.ball);
    lv_obj_set_size(s_view.ball, BO_BALL_R * 2, BO_BALL_R * 2);
    lv_obj_set_style_radius(s_view.ball, NEV_RADIUS_FULL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.ball, NEV_COL_INK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.ball, LV_OPA_COVER, LV_PART_MAIN);

    for (int i = 0; i < BO_MAX_DROPS; i++) {
        lv_obj_t *d = lv_obj_create(field);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 14, 14);
        lv_obj_set_style_radius(d, NEV_RADIUS_SM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_view.drop[i] = d;
    }

    s_view.lives_label = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_align(s_view.lives_label, LV_ALIGN_TOP_MID, 0, NEV_SP_2);

    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(field, field_pressing_cb, LV_EVENT_PRESSING, &s_view);
    lv_obj_add_event_cb(field, field_clicked_cb, LV_EVENT_CLICKED, &s_view);
    return NEV_OK;
}

static void breakout_tick(uint32_t now_ms) {
    nev_game_frame(&s_game, now_ms);
}
static void breakout_event(const nev_event_t *ev) {
    nev_game_event(&s_game, ev);
}
static void breakout_close(void) {
    nev_game_end(&s_game);
}

static const nev_app_desc_t kBreakoutApp = {
    .id = "breakout",
    .name = "Breakout",
    .icon = LV_SYMBOL_STOP,
    .category = NEV_APP_CAT_GAME,
    .memory_budget_kb = 44,
    .on_launch = breakout_launch,
    .on_tick = breakout_tick,
    .on_event = breakout_event,
    .on_close = breakout_close,
};

NEV_APP_REGISTER(kBreakoutApp);
