/*
 * NEVOS — Snake. An original implementation of the grid-growth genre.
 *
 * The rules live in snake_core.c, with no LVGL and no randomness of their own,
 * so they are unit-tested without a display. This file is the renderer and the
 * difficulty curve: it owns the segment objects, the speed ramp, and the juice.
 *
 * Where the pressure comes from: every meal shortens the step interval, so a
 * run ends because the player got faster than they could think, not because
 * they got bored.
 */
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_game/game.h"
#include "snake_core.h"
#include <string.h>

#define CELL         (NEV_GAME_FIELD / SNAKE_GRID)

/* Seconds per cell. These three numbers are the entire difficulty curve. */
#define PERIOD_START 0.170f
#define PERIOD_MIN   0.062f
#define PERIOD_DECAY 0.955f

typedef struct {
    snake_core_t core;
    float period;
    float accum;
    lv_obj_t *seg[SNAKE_MAX_LEN];
    lv_obj_t *food;
} snake_view_t;

static snake_view_t s_view;
static nev_game_t s_game;

/* The core takes its randomness as a callback so it stays free of the engine. */
static uint32_t core_rand(void *ctx, uint32_t upper) {
    return nev_game_rand((nev_game_t *)ctx, upper);
}

static void place(lv_obj_t *o, uint8_t cx, uint8_t cy) {
    lv_obj_set_pos(o, cx * CELL, cy * CELL);
}

static int32_t centre_of(uint8_t cell) {
    return cell * CELL + CELL / 2;
}

static void redraw(snake_view_t *v) {
    for (uint16_t i = 0; i < SNAKE_MAX_LEN; i++) {
        if (i < v->core.len) {
            place(v->seg[i], v->core.x[i], v->core.y[i]);
            lv_obj_remove_flag(v->seg[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(v->seg[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    place(v->food, v->core.food_x, v->core.food_y);
}

static void snake_start(nev_game_t *g) {
    snake_view_t *v = g->user;

    snake_core_reset(&v->core, nev_store_num(NEV_SET_SNAKE_WRAP) != 0);
    (void)snake_core_spawn_food(&v->core, core_rand, g);
    v->period = PERIOD_START;
    v->accum = 0.0f;

    lv_obj_remove_flag(v->food, LV_OBJ_FLAG_HIDDEN);
    redraw(v);
}

static void snake_stop(nev_game_t *g) {
    snake_view_t *v = g->user;
    /* Leave the wreck on screen under the overlay: seeing where you died is
     * part of wanting another go. */
    lv_obj_add_flag(v->food, LV_OBJ_FLAG_HIDDEN);
}

static void snake_update(nev_game_t *g, float dt) {
    snake_view_t *v = g->user;

    v->accum += dt;
    if (v->accum < v->period) return;
    v->accum -= v->period;

    const uint8_t prev_head_x = v->core.x[0];
    const uint8_t prev_head_y = v->core.y[0];

    switch (snake_core_step(&v->core)) {
        case SNAKE_DIED:
            nev_game_burst(g, centre_of(prev_head_x), centre_of(prev_head_y), NEV_COL_DANGER, 14,
                           200.0f);
            nev_game_over(g);
            return;

        case SNAKE_ATE:
            nev_game_add_score(g, 10);
            nev_game_burst(g, centre_of(v->core.x[0]), centre_of(v->core.y[0]), NEV_COL_SUCCESS, 8,
                           120.0f);
            v->period *= PERIOD_DECAY;
            if (v->period < PERIOD_MIN) v->period = PERIOD_MIN;
            (void)snake_core_spawn_food(&v->core, core_rand, g);
            break;

        case SNAKE_MOVED:
            break;
    }
    redraw(v);
}

static void field_gesture_cb(lv_event_t *e) {
    snake_view_t *v = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || s_game.state != NEV_GAME_PLAYING) return;

    switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_LEFT:
            snake_core_turn(&v->core, -1, 0);
            break;
        case LV_DIR_RIGHT:
            snake_core_turn(&v->core, 1, 0);
            break;
        case LV_DIR_TOP:
            snake_core_turn(&v->core, 0, -1);
            break;
        case LV_DIR_BOTTOM:
            snake_core_turn(&v->core, 0, 1);
            break;
        default:
            break;
    }
}

static void snake_input(nev_game_t *g, const nev_event_t *ev) {
    snake_view_t *v = g->user;
    /* Button A turns left relative to travel: one button is enough to play
     * with, and it is the control that still works when the screen is greasy. */
    if (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == 0) {
        snake_core_turn(&v->core, v->core.dy, (int8_t)-v->core.dx);
    }
}

static const nev_game_def_t kSnakeDef = {
    .id = "snake",
    .title = "Snake",
    .how_to = "Swipe to turn " LV_SYMBOL_BULLET " A turns left " LV_SYMBOL_BULLET " Tap to start",
    .highscore_key = NEV_SET_HS_SNAKE,
    .tick_hz = 60,
    .on_start = snake_start,
    .on_update = snake_update,
    .on_input = snake_input,
    .on_stop = snake_stop,
};

static nev_err_t snake_launch(lv_obj_t *root) {
    memset(&s_view, 0, sizeof(s_view));
    NEV_TRY(nev_game_begin(&s_game, root, &kSnakeDef));
    s_game.user = &s_view;

    lv_obj_t *field = s_game.field;
    for (uint16_t i = 0; i < SNAKE_MAX_LEN; i++) {
        lv_obj_t *seg = lv_obj_create(field);
        lv_obj_remove_style_all(seg);
        lv_obj_set_size(seg, CELL - 2, CELL - 2);
        lv_obj_set_style_radius(seg, NEV_RADIUS_SM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        /* A brighter head, so the player can see which way they are pointing
         * without tracing the whole body. */
        lv_obj_set_style_bg_color(seg, i == 0 ? NEV_COL_INK : NEV_COL_ACCENT, LV_PART_MAIN);
        lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
        s_view.seg[i] = seg;
    }

    s_view.food = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.food);
    lv_obj_set_size(s_view.food, CELL - 6, CELL - 6);
    lv_obj_set_style_radius(s_view.food, NEV_RADIUS_FULL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.food, NEV_COL_SUCCESS, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.food, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_view.food, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    /*
     * LVGL sets LV_OBJ_FLAG_GESTURE_BUBBLE on every object that has a parent
     * (lv_obj.c: `if(parent) obj->flags |= LV_OBJ_FLAG_GESTURE_BUBBLE`), so a
     * gesture is delivered to the topmost ancestor rather than to the object
     * that was touched. A handler attached here never fires until it is cleared.
     */
    lv_obj_remove_flag(field, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(field, field_gesture_cb, LV_EVENT_GESTURE, &s_view);
    return NEV_OK;
}

static void snake_tick(uint32_t now_ms) {
    nev_game_frame(&s_game, now_ms);
}
static void snake_event(const nev_event_t *ev) {
    nev_game_event(&s_game, ev);
}
static void snake_close(void) {
    nev_game_end(&s_game);
}

static const nev_app_desc_t kSnakeApp = {
    .id = "snake",
    .name = "Snake",
    .icon = LV_SYMBOL_LOOP,
    .category = NEV_APP_CAT_GAME,
    .memory_budget_kb = 40,
    .on_launch = snake_launch,
    .on_tick = snake_tick,
    .on_event = snake_event,
    .on_close = snake_close,
};

NEV_APP_REGISTER(kSnakeApp);
