/*
 * NEVOS — Match. An original tile-matching puzzle.
 *
 * Rules in match_core.c, unit-tested without a display, including the three
 * invariants that make it a puzzle rather than a slot machine: no free opening
 * match, no pointless swap, never a dead board.
 *
 * Ninety seconds. The clock is the pressure; cascades are how you beat it.
 */
#include "match_core.h"
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_game/game.h"
#include <string.h>

#define TILE           56
#define GAP            2
#define ORIGIN         ((NEV_GAME_FIELD - MT_SIZE * TILE) / 2)

/* Cascades step on a timer so they can be seen. Faster than this and a long
 * cascade is a single flicker; slower and the ninety seconds feel stolen. */
#define CASCADE_STEP_S 0.17f

typedef struct {
    match_core_t core;
    lv_obj_t *tile[MT_SIZE][MT_SIZE];
    lv_obj_t *timebar;

    bool has_selection;
    uint8_t sel_r, sel_c;

    bool resolving;
    uint8_t cascade;
    float step_timer;
    float remaining_s;
} match_view_t;

static match_view_t s_view;
static nev_game_t s_game;

static uint32_t core_rand(void *ctx, uint32_t upper) {
    return nev_game_rand((nev_game_t *)ctx, upper);
}

static void paint_tile(match_view_t *v, uint8_t r, uint8_t c) {
    lv_obj_t *t = v->tile[r][c];
    const uint8_t colour = v->core.tile[r][c];

    if (colour >= MT_COLORS) {
        lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(t, nev_theme_play_color(colour), LV_PART_MAIN);

    /* Selection is a border rather than a colour change, so the tile's own
     * colour — the thing being matched — stays readable while it is picked. */
    const bool selected = v->has_selection && v->sel_r == r && v->sel_c == c;
    lv_obj_set_style_border_width(t, selected ? 3 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(t, NEV_COL_INK, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(t, selected ? 276 : 256, LV_PART_MAIN);
}

static void repaint(match_view_t *v) {
    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++)
            paint_tile(v, r, c);
    }
}

static void match_start(nev_game_t *g) {
    match_view_t *v = g->user;
    match_core_fill(&v->core, core_rand, g);
    v->has_selection = false;
    v->resolving = false;
    v->cascade = 0;
    v->step_timer = 0.0f;
    v->remaining_s = (float)MT_ROUND_SECONDS;
    repaint(v);
}

static void run_cascade_step(nev_game_t *g) {
    match_view_t *v = g->user;
    match_resolve_t out;

    v->cascade = (uint8_t)(v->cascade + 1);
    if (!match_core_resolve(&v->core, v->cascade, core_rand, g, &out)) {
        v->resolving = false;
        v->cascade = 0;
        if (out.reshuffled) nev_ui_toast("No moves left — reshuffled", 1600);
        repaint(v);
        return;
    }

    nev_game_add_score(g, (int32_t)match_core_score_for(out.cleared, out.cascade));
    if (out.cascade >= 3) nev_game_shake(g, 0.12f * (float)out.cascade);

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            if (!out.mask[r][c]) continue;
            nev_game_burst(g, ORIGIN + c * TILE + TILE / 2, ORIGIN + r * TILE + TILE / 2,
                           nev_theme_play_color(v->core.tile[r][c]), 2, 90.0f);
        }
    }
    repaint(v);
    v->resolving = out.more;
    if (!out.more) v->cascade = 0;
}

static void match_update(nev_game_t *g, float dt) {
    match_view_t *v = g->user;

    v->remaining_s -= dt;
    if (v->remaining_s <= 0.0f) {
        v->remaining_s = 0.0f;
        nev_game_over(g);
        return;
    }
    lv_obj_set_width(v->timebar,
                     (int32_t)((float)NEV_GAME_FIELD * v->remaining_s / MT_ROUND_SECONDS));
    /* The last ten seconds are the ones worth noticing. */
    lv_obj_set_style_bg_color(v->timebar, v->remaining_s < 10.0f ? NEV_COL_DANGER : NEV_COL_ACCENT,
                              LV_PART_MAIN);

    if (!v->resolving) return;
    v->step_timer += dt;
    if (v->step_timer < CASCADE_STEP_S) return;
    v->step_timer = 0.0f;
    run_cascade_step(g);
}

static void tile_clicked_cb(lv_event_t *e) {
    match_view_t *v = &s_view;
    const uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    const uint8_t r = (uint8_t)(packed >> 8);
    const uint8_t c = (uint8_t)(packed & 0xFF);

    /* Input is ignored while a cascade runs: letting a swap land mid-resolve
     * would act on a board the player is not looking at. */
    if (s_game.state != NEV_GAME_PLAYING || v->resolving) return;

    if (!v->has_selection) {
        v->has_selection = true;
        v->sel_r = r;
        v->sel_c = c;
        repaint(v);
        return;
    }
    if (v->sel_r == r && v->sel_c == c) {
        v->has_selection = false; /* tapping it again deselects */
        repaint(v);
        return;
    }

    if (match_core_adjacent(v->sel_r, v->sel_c, r, c)) {
        if (match_core_swap(&v->core, v->sel_r, v->sel_c, r, c)) {
            v->has_selection = false;
            v->resolving = true;
            v->cascade = 0;
            v->step_timer = CASCADE_STEP_S; /* resolve on the very next update */
            repaint(v);
            return;
        }
        /* A swap that achieves nothing is refused, so say so rather than
         * leaving the player wondering whether the tap registered. */
        nev_game_shake(&s_game, 0.15f);
    }
    /* Not adjacent, or refused: treat the tap as choosing a new tile. */
    v->sel_r = r;
    v->sel_c = c;
    repaint(v);
}

static const nev_game_def_t kMatchDef = {
    .id = "match",
    .title = "Match",
    .how_to = "Swap neighbours to line up three " LV_SYMBOL_BULLET " 90 seconds",
    .highscore_key = NEV_SET_HS_MATCH,
    .tick_hz = 60,
    .on_start = match_start,
    .on_update = match_update,
};

static nev_err_t match_launch(lv_obj_t *root) {
    memset(&s_view, 0, sizeof(s_view));
    NEV_TRY(nev_game_begin(&s_game, root, &kMatchDef));
    s_game.user = &s_view;

    lv_obj_t *field = s_game.field;

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            lv_obj_t *t = lv_obj_create(field);
            lv_obj_remove_style_all(t);
            lv_obj_set_size(t, TILE - GAP * 2, TILE - GAP * 2);
            lv_obj_set_pos(t, ORIGIN + c * TILE + GAP, ORIGIN + r * TILE + GAP);
            lv_obj_set_style_radius(t, NEV_RADIUS_SM, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_transform_pivot_x(t, (TILE - GAP * 2) / 2, LV_PART_MAIN);
            lv_obj_set_style_transform_pivot_y(t, (TILE - GAP * 2) / 2, LV_PART_MAIN);
            lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(t, tile_clicked_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)((r << 8) | c));
            s_view.tile[r][c] = t;
        }
    }

    s_view.timebar = lv_obj_create(field);
    lv_obj_remove_style_all(s_view.timebar);
    lv_obj_set_size(s_view.timebar, NEV_GAME_FIELD, 5);
    lv_obj_set_pos(s_view.timebar, 0, NEV_GAME_FIELD - 5);
    lv_obj_set_style_bg_color(s_view.timebar, NEV_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.timebar, LV_OPA_COVER, LV_PART_MAIN);
    return NEV_OK;
}

static void match_tick(uint32_t now_ms) {
    nev_game_frame(&s_game, now_ms);
}
static void match_event(const nev_event_t *ev) {
    nev_game_event(&s_game, ev);
}
static void match_close(void) {
    nev_game_end(&s_game);
}

static const nev_app_desc_t kMatchApp = {
    .id = "match",
    .name = "Match",
    .icon = LV_SYMBOL_LIST,
    .category = NEV_APP_CAT_GAME,
    .memory_budget_kb = 48,
    .on_launch = match_launch,
    .on_tick = match_tick,
    .on_event = match_event,
    .on_close = match_close,
};

NEV_APP_REGISTER(kMatchApp);
