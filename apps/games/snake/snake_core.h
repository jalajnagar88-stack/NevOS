/*
 * Snake — the rules, with no LVGL and no rendering.
 *
 * Same split as persona_core and nev_store: the part with the interesting
 * behaviour takes no dependency on a UI toolkit, so "does eating grow the
 * snake", "is running into the tail cell legal", and "does a 180 kill you" are
 * assertions rather than things you try to reproduce by playing.
 *
 * A random-input smoke test cannot establish any of that: on a 400-cell grid a
 * random walk essentially never lands on the one food cell, so a long
 * auto-played run proves the game does not crash and nothing else.
 */
#ifndef SNAKE_CORE_H
#define SNAKE_CORE_H

#include "nev_port/nev_types.h"

#define SNAKE_GRID       20
#define SNAKE_MAX_LEN    72 /* one drawn segment each; 72 is already a long run */
#define SNAKE_START_LEN  4
#define SNAKE_TURN_QUEUE 2

typedef enum {
    SNAKE_MOVED = 0,
    SNAKE_ATE,
    SNAKE_DIED,
} snake_step_t;

typedef struct {
    uint8_t x[SNAKE_MAX_LEN], y[SNAKE_MAX_LEN]; /* [0] is the head */
    uint16_t len;

    int8_t dx, dy;

    /*
     * Up to two buffered turns, applied one per step.
     *
     * One slot is not enough: a player swiping down-then-left while heading
     * right expects both turns, and with a single slot the second overwrites
     * the first — which applied `left` directly from `right`, a 180 into the
     * body. Two slots let each turn be validated against the one before it, so
     * a reversal is impossible by construction rather than by a check that has
     * to guess which direction is "current".
     */
    int8_t queue_dx[SNAKE_TURN_QUEUE], queue_dy[SNAKE_TURN_QUEUE];
    uint8_t queued;

    uint8_t food_x, food_y;
    bool wrap;
} snake_core_t;

void snake_core_reset(snake_core_t *s, bool wrap);

/* Buffered until the next step, so a fast double-turn is not lost and a turn
 * cannot be applied halfway between cells. A reversal is refused, not fatal. */
void snake_core_turn(snake_core_t *s, int8_t dx, int8_t dy);

snake_step_t snake_core_step(snake_core_t *s);

bool snake_core_occupied(const snake_core_t *s, uint8_t cx, uint8_t cy);
void snake_core_set_food(snake_core_t *s, uint8_t cx, uint8_t cy);

/* Picks a free cell using `rand(upper)`; returns false only if the grid is full. */
bool snake_core_spawn_food(snake_core_t *s, uint32_t (*rand_fn)(void *, uint32_t), void *ctx);

#endif /* SNAKE_CORE_H */
