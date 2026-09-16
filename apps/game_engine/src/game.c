#include "nev_game/game.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_time.h"
#include <stdio.h>
#include <string.h>

#define TAG                 "game"

/*
 * The most fixed updates one frame may run before the engine gives up and drops
 * the remainder. Without this, a frame that took 400 ms schedules 24 updates,
 * each of which makes the next frame later — the classic spiral where a single
 * hitch turns into a permanent slowdown. Dropping simulated time is the lesser
 * evil: the game slows for one frame instead of forever.
 */
#define MAX_STEPS_PER_FRAME 5

#define PARTICLE_SIZE       6
#define SHAKE_PIXELS        9.0f

static uint32_t rng_next(nev_game_t *g) {
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

uint32_t nev_game_rand(nev_game_t *g, uint32_t upper_exclusive) {
    if (!g || upper_exclusive == 0) return 0;
    return rng_next(g) % upper_exclusive;
}

static float rand_signed(nev_game_t *g) {
    return (float)(rng_next(g) % 2001u) / 1000.0f - 1.0f;
}

/* ------------------------------------------------------------------ chrome */

/*
 * NULL-safe on purpose. The scoring rules are worth unit-testing without a
 * display, and the same class of bug — a handler reaching for a widget that
 * belongs to a screen it is not on — already cost one crash in the shell.
 */
static void update_hud(nev_game_t *g) {
    if (!g->hud_score || !g->hud_best) return;
    lv_label_set_text_fmt(g->hud_score, "%u", (unsigned)g->score);
    if (g->beat_best) {
        lv_label_set_text_fmt(g->hud_best, LV_SYMBOL_UP " %u", (unsigned)g->best);
        lv_obj_set_style_text_color(g->hud_best, NEV_COL_SUCCESS, LV_PART_MAIN);
    } else {
        lv_label_set_text_fmt(g->hud_best, "best %u", (unsigned)g->best);
    }
}

static void show_overlay(nev_game_t *g, const char *title, const char *body) {
    if (!g->overlay || !g->overlay_title || !g->overlay_body) return;
    lv_label_set_text(g->overlay_title, title);
    lv_label_set_text(g->overlay_body, body);
    lv_obj_remove_flag(g->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g->overlay);
}

static void hide_overlay(nev_game_t *g) {
    lv_obj_add_flag(g->overlay, LV_OBJ_FLAG_HIDDEN);
}

static void overlay_clicked_cb(lv_event_t *e) {
    nev_game_restart((nev_game_t *)lv_event_get_user_data(e));
}

/* ---------------------------------------------------------------- lifecycle */

nev_err_t nev_game_begin(nev_game_t *g, lv_obj_t *root, const nev_game_def_t *def) {
    NEV_REQUIRE(g && root && def && def->on_update, NEV_ERR_INVALID_ARG);

    memset(g, 0, sizeof(*g));
    g->def = def;
    g->root = root;
    g->state = NEV_GAME_ATTRACT;
    g->rng = nev_now_ms() | 1u;
    g->best = nev_store_is_ready() ? nev_store_num(def->highscore_key) : 0;

    /* Square, centred playfield: no game has to reason about letterboxing or
     * about where the HUD is. */
    g->field = lv_obj_create(root);
    lv_obj_remove_style_all(g->field);
    lv_obj_set_size(g->field, NEV_GAME_FIELD, NEV_GAME_FIELD);
    lv_obj_align(g->field, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(g->field, NEV_COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g->field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g->field, NEV_RADIUS_MD, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(g->field, true, LV_PART_MAIN);
    lv_obj_remove_flag(g->field, LV_OBJ_FLAG_SCROLLABLE);

    g->hud_score = nev_ui_label(root, "0", NEV_FONT_TITLE, NEV_COL_INK);
    lv_obj_align(g->hud_score, LV_ALIGN_TOP_LEFT, NEV_SP_5, 0);
    g->hud_best = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_align(g->hud_best, LV_ALIGN_TOP_RIGHT, -NEV_SP_5, NEV_SP_2);

    /* Particles are created once and reused. Allocating an LVGL object at the
     * moment something explodes is allocating on the hot path. */
    for (int i = 0; i < NEV_GAME_MAX_PARTICLES; i++) {
        lv_obj_t *p = lv_obj_create(g->field);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, PARTICLE_SIZE, PARTICLE_SIZE);
        lv_obj_set_style_radius(p, NEV_RADIUS_FULL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
        g->particle[i].obj = p;
    }

    g->overlay = lv_obj_create(root);
    lv_obj_remove_style_all(g->overlay);
    lv_obj_set_size(g->overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g->overlay, NEV_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g->overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_flex_flow(g->overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g->overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g->overlay, NEV_SP_3, LV_PART_MAIN);
    lv_obj_remove_flag(g->overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(g->overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g->overlay, overlay_clicked_cb, LV_EVENT_CLICKED, g);

    g->overlay_title = nev_ui_label(g->overlay, def->title, NEV_FONT_DISPLAY, NEV_COL_INK);
    g->overlay_body = nev_ui_label(g->overlay, def->how_to, NEV_FONT_BODY, NEV_COL_INK_MUTED);

    update_hud(g);
    show_overlay(g, def->title, def->how_to);
    g->last_ms = nev_now_ms();
    return NEV_OK;
}

void nev_game_end(nev_game_t *g) {
    if (!g || !g->def) return;
    if (g->state == NEV_GAME_PLAYING && g->def->on_stop) g->def->on_stop(g);
    memset(g, 0, sizeof(*g)); /* the shell deletes root, taking every child */
}

void nev_game_restart(nev_game_t *g) {
    if (!g || !g->def) return;
    if (g->state == NEV_GAME_PLAYING && g->def->on_stop) g->def->on_stop(g);

    for (int i = 0; i < NEV_GAME_MAX_PARTICLES; i++) {
        g->particle[i].life = 0.0f;
        if (g->particle[i].obj) lv_obj_add_flag(g->particle[i].obj, LV_OBJ_FLAG_HIDDEN);
    }
    g->score = 0;
    g->beat_best = false;
    g->elapsed_ms = 0;
    g->accum_us = 0;
    g->shake = 0.0f;
    g->last_ms = nev_now_ms();
    g->state = NEV_GAME_PLAYING;

    hide_overlay(g);
    update_hud(g);
    if (g->def->on_start) g->def->on_start(g);
}

void nev_game_add_score(nev_game_t *g, int32_t delta) {
    if (!g) return;
    g->score =
        (delta < 0 && (uint32_t)(-delta) > g->score) ? 0u : (uint32_t)((int32_t)g->score + delta);

    /* Tell the player the moment the record falls, not at the end. That is the
     * beat worth celebrating, and it is what makes one more go tempting. */
    if (!g->beat_best && g->score > g->best && g->best > 0) {
        g->beat_best = true;
        g->best = g->score;
        nev_game_shake(g, 0.35f);
        (void)nev_bus_publish_type(NEV_EVT_GAME_HIGHSCORE_BEAT, NEV_SRC_GAME);
    } else if (g->score > g->best) {
        g->best = g->score;
    }
    update_hud(g);

    nev_event_t ev = nev_event_make(NEV_EVT_GAME_SCORE, NEV_SRC_GAME);
    ev.p.score.score = g->score;
    ev.p.score.best = g->best;
    (void)nev_bus_publish(&ev);
}

uint32_t nev_game_score(const nev_game_t *g) {
    return g ? g->score : 0;
}
uint32_t nev_game_best(const nev_game_t *g) {
    return g ? g->best : 0;
}

void nev_game_over(nev_game_t *g) {
    if (!g || g->state != NEV_GAME_PLAYING) return;
    g->state = NEV_GAME_OVER;
    if (g->def->on_stop) g->def->on_stop(g);

    if (g->score >= g->best && nev_store_is_ready()) {
        (void)nev_store_set_num(g->def->highscore_key, g->score);
    }
    nev_game_shake(g, 0.6f);

    static char body[64];
    if (g->beat_best) {
        snprintf(body, sizeof(body), "New best: %u. Tap to play again.", (unsigned)g->score);
    } else {
        snprintf(body, sizeof(body), "%u  " LV_SYMBOL_BULLET "  best %u. Tap to play again.",
                 (unsigned)g->score, (unsigned)g->best);
    }
    show_overlay(g, g->beat_best ? "Record" : "Game over", body);

    nev_event_t ev = nev_event_make(NEV_EVT_GAME_OVER, NEV_SRC_GAME);
    ev.p.score.score = g->score;
    ev.p.score.best = g->best;
    (void)nev_bus_publish(&ev);
}

/* -------------------------------------------------------------------- juice */

void nev_game_shake(nev_game_t *g, float strength) {
    if (!g) return;
    if (strength > 1.0f) strength = 1.0f;
    if (strength > g->shake) g->shake = strength; /* a bigger hit wins */
}

void nev_game_burst(nev_game_t *g, int32_t x, int32_t y, lv_color_t color, uint8_t count,
                    float speed) {
    if (!g) return;
    for (uint8_t i = 0; i < count; i++) {
        /* Round-robin over a fixed pool: a burst during a burst steals the
         * oldest particles rather than failing or allocating. */
        const uint8_t idx = g->next_particle;
        g->next_particle = (uint8_t)((g->next_particle + 1) % NEV_GAME_MAX_PARTICLES);

        g->particle[idx].x = (float)x;
        g->particle[idx].y = (float)y;
        g->particle[idx].vx = rand_signed(g) * speed;
        g->particle[idx].vy = rand_signed(g) * speed;
        g->particle[idx].life0 = 0.35f + (float)(rng_next(g) % 300u) / 1000.0f;
        g->particle[idx].life = g->particle[idx].life0;
        if (g->particle[idx].obj) {
            lv_obj_set_style_bg_color(g->particle[idx].obj, color, LV_PART_MAIN);
            lv_obj_remove_flag(g->particle[idx].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void step_effects(nev_game_t *g, float dt) {
    for (int i = 0; i < NEV_GAME_MAX_PARTICLES; i++) {
        if (g->particle[i].life <= 0.0f) continue;

        g->particle[i].life -= dt;
        if (g->particle[i].life <= 0.0f) {
            if (g->particle[i].obj) lv_obj_add_flag(g->particle[i].obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        g->particle[i].vy += 420.0f * dt; /* a little gravity reads as weight */
        g->particle[i].x += g->particle[i].vx * dt;
        g->particle[i].y += g->particle[i].vy * dt;

        if (!g->particle[i].obj) continue;
        lv_obj_set_pos(g->particle[i].obj, (int32_t)g->particle[i].x - PARTICLE_SIZE / 2,
                       (int32_t)g->particle[i].y - PARTICLE_SIZE / 2);
        lv_obj_set_style_opa(g->particle[i].obj,
                             (lv_opa_t)(255.0f * (g->particle[i].life / g->particle[i].life0)),
                             LV_PART_MAIN);
    }

    if (g->shake > 0.001f) {
        g->shake -= dt * 3.2f;
        if (g->shake < 0.0f) g->shake = 0.0f;
        const int32_t nx = (int32_t)(rand_signed(g) * SHAKE_PIXELS * g->shake);
        const int32_t ny = (int32_t)(rand_signed(g) * SHAKE_PIXELS * g->shake);
        if ((nx != g->shake_x || ny != g->shake_y) && g->field) {
            g->shake_x = nx;
            g->shake_y = ny;
            lv_obj_align(g->field, LV_ALIGN_CENTER, nx, 10 + ny);
        }
    } else if ((g->shake_x || g->shake_y) && g->field) {
        g->shake_x = g->shake_y = 0;
        lv_obj_align(g->field, LV_ALIGN_CENTER, 0, 10);
    }
}

/* --------------------------------------------------------------------- loop */

void nev_game_frame(nev_game_t *g, uint32_t now_ms) {
    if (!g || !g->def) return;

    const uint32_t delta_ms = now_ms - g->last_ms;
    g->last_ms = now_ms;

    const uint32_t step_us = 1000000u / (g->def->tick_hz ? g->def->tick_hz : 60u);
    const float dt = (float)step_us / 1000000.0f;

    if (g->state == NEV_GAME_PLAYING) {
        g->elapsed_ms += delta_ms;
        g->accum_us += delta_ms * 1000u;

        int steps = 0;
        while (g->accum_us >= step_us && steps < MAX_STEPS_PER_FRAME) {
            g->accum_us -= step_us;
            g->def->on_update(g, dt);
            steps++;
            if (g->state != NEV_GAME_PLAYING) break; /* the update ended the run */
        }
        if (steps == MAX_STEPS_PER_FRAME && g->accum_us >= step_us) {
            /* Behind by more than the cap: discard the backlog rather than
             * spiralling. The game stutters once; it does not slow down forever. */
            g->accum_us = 0;
        }
    }

    /* Effects run even when not playing, so the game-over shake is visible. */
    step_effects(g, delta_ms > 100u ? 0.1f : (float)delta_ms / 1000.0f);
}

void nev_game_event(nev_game_t *g, const nev_event_t *ev) {
    if (!g || !g->def || !ev) return;

    /* Any tap or button A starts or restarts. One gesture, every game, both
     * states — a per-game "press start" convention would be a per-game thing to
     * learn for no benefit. */
    const bool go = ev->type == NEV_EVT_INPUT_TOUCH ||
                    (ev->type == NEV_EVT_INPUT_BUTTON_DOWN && ev->p.button.id == 0);
    if (g->state != NEV_GAME_PLAYING) {
        if (go) nev_game_restart(g);
        return;
    }
    if (g->def->on_input) g->def->on_input(g, ev);
}
