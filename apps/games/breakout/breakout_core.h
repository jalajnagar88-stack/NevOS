/*
 * Breakout — the rules, with no LVGL.
 *
 * Original implementation of the paddle-and-bricks genre. Difficulty is
 * approachable then steep (ADR 0011): the first level is slow enough that
 * anyone clears it, and the ball speed climbs sharply from there.
 */
#ifndef BREAKOUT_CORE_H
#define BREAKOUT_CORE_H

#include "nev_port/nev_types.h"

#define BO_FIELD        400
#define BO_COLS         8
#define BO_ROWS         5
#define BO_BRICKS       (BO_COLS * BO_ROWS)
#define BO_BRICK_W      (BO_FIELD / BO_COLS)
#define BO_BRICK_H      18
#define BO_BRICK_TOP    44

#define BO_PADDLE_W     74
#define BO_PADDLE_W_MAX 130
#define BO_PADDLE_H     10
#define BO_PADDLE_Y     (BO_FIELD - 26)
#define BO_BALL_R       6
#define BO_LIVES        3

#define BO_MAX_DROPS    4

/* Speed in pixels per second. The whole difficulty curve is these four numbers. */
#define BO_SPEED_START  150.0f
#define BO_SPEED_STEP   26.0f
#define BO_SPEED_MAX    360.0f
#define BO_SLOW_FACTOR  0.72f

typedef enum {
    BO_DROP_NONE = 0,
    BO_DROP_WIDE,
    BO_DROP_SLOW,
    BO_DROP_LIFE,
} bo_drop_t;

typedef struct {
    bool active;
    bo_drop_t kind;
    float x, y;
} bo_falling_t;

typedef struct {
    uint8_t hp; /* 0 = cleared */
} bo_brick_t;

typedef struct {
    bo_brick_t brick[BO_BRICKS];
    uint8_t bricks_left;

    float ball_x, ball_y, ball_vx, ball_vy;
    float speed;     /* current nominal speed, before the slow powerup */
    bool ball_stuck; /* resting on the paddle, waiting to launch       */

    float paddle_x; /* centre */
    float paddle_w;

    bo_falling_t drop[BO_MAX_DROPS];

    uint8_t lives;
    uint8_t level;
    float slow_timer;
    float wide_timer;
} breakout_core_t;

/* What one step produced. Several can be true at once. */
typedef struct {
    bool hit_paddle;
    bool hit_wall;
    bool lost_life;
    bool level_cleared;
    bool game_over;
    uint8_t bricks_broken;
    int16_t hit_x, hit_y; /* where the last brick broke, for particles */
    bo_drop_t caught;
} breakout_step_t;

void breakout_core_reset(breakout_core_t *c);
void breakout_core_start_level(breakout_core_t *c, uint8_t level,
                               uint32_t (*rand_fn)(void *, uint32_t), void *ctx);

/* Clamped to the field. The paddle is dragged, not accelerated: a desk toy
 * played in short bursts should not also require learning a control. */
void breakout_core_move_paddle(breakout_core_t *c, float centre_x);
void breakout_core_launch(breakout_core_t *c);

void breakout_core_step(breakout_core_t *c, float dt, uint32_t (*rand_fn)(void *, uint32_t),
                        void *ctx, breakout_step_t *out);

float breakout_core_ball_speed(const breakout_core_t *c);
int16_t bo_brick_x(uint8_t index);
int16_t bo_brick_y(uint8_t index);

#endif /* BREAKOUT_CORE_H */
