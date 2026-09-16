/*
 * NEVOS — the shared game engine.
 *
 * Five games, one loop. Everything here exists because writing it five times
 * would produce five subtly different answers to the same question: what the
 * timestep is, when the score is saved, what "game over" looks like, and how
 * hard the screen shakes.
 *
 * What it provides:
 *   - a fixed-timestep update decoupled from the render rate
 *   - attract / playing / over states, with the HUD and overlays
 *   - score and a persisted high score, via nev_store
 *   - collision helpers
 *   - a preallocated particle system and screen shake, for juice
 *
 * What it does not provide: any game rules. Those live in the games.
 */
#ifndef NEV_GAME_GAME_H
#define NEV_GAME_GAME_H

#include "nev_appkit/theme.h"
#include "nev_kernel/nev_event.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Preallocated once per game, never during play. Thirty-two is enough for a
 * satisfying burst and small enough that five games' worth would still fit if
 * they were all somehow live at once.
 */
#define NEV_GAME_MAX_PARTICLES 32

/* The playfield is square and centred, so no game has to reason about a
 * letterboxed area or about where the HUD is. */
#define NEV_GAME_FIELD         400

typedef enum {
    NEV_GAME_ATTRACT = 0, /* waiting to be started      */
    NEV_GAME_PLAYING,
    NEV_GAME_OVER,
} nev_game_state_t;

typedef struct nev_game nev_game_t;

typedef struct {
    const char *id;
    const char *title;
    const char *how_to; /* one line on the attract screen */
    nev_setting_t highscore_key;

    /* The fixed update rate. Not the frame rate: a game that ties its physics
     * to the display runs differently the moment a frame is slow. */
    uint16_t tick_hz;

    void (*on_start)(nev_game_t *g);
    void (*on_update)(nev_game_t *g, float dt);
    void (*on_input)(nev_game_t *g, const nev_event_t *ev);
    void (*on_stop)(nev_game_t *g);
} nev_game_def_t;

struct nev_game {
    const nev_game_def_t *def;
    nev_game_state_t state;

    lv_obj_t *root;
    lv_obj_t *field; /* draw here; already centred and clipped */
    lv_obj_t *hud_score;
    lv_obj_t *hud_best;
    lv_obj_t *overlay;
    lv_obj_t *overlay_title;
    lv_obj_t *overlay_body;

    uint32_t score;
    uint32_t best;
    bool beat_best; /* set once per run, when the record falls */

    uint32_t last_ms;
    uint32_t accum_us;
    uint32_t elapsed_ms; /* time spent in PLAYING */
    uint32_t rng;

    float shake; /* 0..1, decays */
    int32_t shake_x, shake_y;

    struct {
        lv_obj_t *obj;
        float x, y, vx, vy;
        float life;  /* seconds remaining */
        float life0; /* seconds at spawn, for fade */
    } particle[NEV_GAME_MAX_PARTICLES];
    uint8_t next_particle;

    void *user; /* the game's own state */
};

nev_err_t nev_game_begin(nev_game_t *g, lv_obj_t *root, const nev_game_def_t *def);
void nev_game_end(nev_game_t *g);

/* Once per frame from the app's on_tick. Runs as many fixed updates as the
 * elapsed time calls for, then the visual effects. */
void nev_game_frame(nev_game_t *g, uint32_t now_ms);
void nev_game_event(nev_game_t *g, const nev_event_t *ev);

void nev_game_add_score(nev_game_t *g, int32_t delta);
uint32_t nev_game_score(const nev_game_t *g);
uint32_t nev_game_best(const nev_game_t *g);

/* Ends the run: saves the score if it is a record, shows the overlay, and
 * publishes GAME.OVER (and GAME.HIGHSCORE_BEAT), which is what makes the face
 * celebrate without the game knowing the persona exists. */
void nev_game_over(nev_game_t *g);
void nev_game_restart(nev_game_t *g);

/* -------------------------------------------------------------------- juice */

void nev_game_shake(nev_game_t *g, float strength); /* 0..1 */
void nev_game_burst(nev_game_t *g, int32_t x, int32_t y, lv_color_t color, uint8_t count,
                    float speed);

/* --------------------------------------------------------------- utilities */

/* Deterministic per-run; seeded from the clock at start so runs differ. */
uint32_t nev_game_rand(nev_game_t *g, uint32_t upper_exclusive);

static inline bool nev_game_aabb(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx,
                                 int32_t by, int32_t bw, int32_t bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static inline bool nev_game_point_in(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w,
                                     int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static inline bool nev_game_circles_hit(int32_t ax, int32_t ay, int32_t ar, int32_t bx, int32_t by,
                                        int32_t br) {
    const int32_t dx = ax - bx, dy = ay - by, r = ar + br;
    return dx * dx + dy * dy <= r * r;
}

#ifdef __cplusplus
}
#endif
#endif /* NEV_GAME_GAME_H */
