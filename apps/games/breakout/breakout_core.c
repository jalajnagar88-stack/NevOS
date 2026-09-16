#include "breakout_core.h"
#include <math.h>
#include <string.h>

#define DROP_SPEED   110.0f
#define POWERUP_ODDS 6 /* one brick in six drops something */
#define WIDE_SECONDS 11.0f
#define SLOW_SECONDS 8.0f

int16_t bo_brick_x(uint8_t index) {
    return (int16_t)((index % BO_COLS) * BO_BRICK_W);
}
int16_t bo_brick_y(uint8_t index) {
    return (int16_t)(BO_BRICK_TOP + (index / BO_COLS) * (BO_BRICK_H + 4));
}

float breakout_core_ball_speed(const breakout_core_t *c) {
    if (!c) return 0.0f;
    return c->slow_timer > 0.0f ? c->speed * BO_SLOW_FACTOR : c->speed;
}

static void park_ball_on_paddle(breakout_core_t *c) {
    c->ball_stuck = true;
    c->ball_x = c->paddle_x;
    c->ball_y = (float)(BO_PADDLE_Y - BO_BALL_R - 1);
    c->ball_vx = 0.0f;
    c->ball_vy = 0.0f;
}

void breakout_core_reset(breakout_core_t *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->lives = BO_LIVES;
    c->paddle_w = BO_PADDLE_W;
    c->paddle_x = BO_FIELD / 2.0f;
}

void breakout_core_start_level(breakout_core_t *c, uint8_t level,
                               uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    if (!c) return;
    c->level = level;
    c->bricks_left = 0;
    c->slow_timer = 0.0f;
    c->wide_timer = 0.0f;
    c->paddle_w = BO_PADDLE_W;
    memset(c->drop, 0, sizeof(c->drop));

    for (uint8_t i = 0; i < BO_BRICKS; i++) {
        /*
         * Two-hit bricks appear from level 2 and get commoner, and only in the
         * upper rows so the first contact with a level is always a clean break.
         */
        uint8_t hp = 1;
        if (level >= 2 && (i / BO_COLS) < 2) {
            const uint32_t roll = rand_fn ? rand_fn(ctx, 100) : 50;
            if (roll < (uint32_t)(20 + level * 8)) hp = 2;
        }
        c->brick[i].hp = hp;
        c->bricks_left++;
    }

    c->speed = BO_SPEED_START + BO_SPEED_STEP * (float)(level - 1);
    if (c->speed > BO_SPEED_MAX) c->speed = BO_SPEED_MAX;
    park_ball_on_paddle(c);
}

void breakout_core_move_paddle(breakout_core_t *c, float centre_x) {
    if (!c) return;
    const float half = c->paddle_w / 2.0f;
    if (centre_x < half) centre_x = half;
    if (centre_x > BO_FIELD - half) centre_x = BO_FIELD - half;
    c->paddle_x = centre_x;
    if (c->ball_stuck) c->ball_x = centre_x;
}

void breakout_core_launch(breakout_core_t *c) {
    if (!c || !c->ball_stuck) return;
    c->ball_stuck = false;
    /* Always upward and slightly off-vertical: a perfectly vertical launch can
     * bounce straight up and down forever without touching a brick. */
    const float speed = breakout_core_ball_speed(c);
    c->ball_vx = speed * 0.35f;
    c->ball_vy = -speed * 0.94f;
}

static void spawn_drop(breakout_core_t *c, float x, float y, uint32_t (*rand_fn)(void *, uint32_t),
                       void *ctx) {
    if (!rand_fn || rand_fn(ctx, POWERUP_ODDS) != 0) return;
    for (int i = 0; i < BO_MAX_DROPS; i++) {
        if (c->drop[i].active) continue;
        c->drop[i].active = true;
        /* A spare life is the rarest, because it is worth the most. */
        const uint32_t roll = rand_fn(ctx, 10);
        c->drop[i].kind = roll < 4 ? BO_DROP_WIDE : (roll < 8 ? BO_DROP_SLOW : BO_DROP_LIFE);
        c->drop[i].x = x;
        c->drop[i].y = y;
        return;
    }
}

/* Returns the brick index under a point, or -1. */
static int brick_at(const breakout_core_t *c, float x, float y) {
    if (y < BO_BRICK_TOP) return -1;
    const int row = (int)((y - BO_BRICK_TOP) / (BO_BRICK_H + 4));
    if (row < 0 || row >= BO_ROWS) return -1;
    /* The 4px gap between rows is not brick. */
    if ((y - BO_BRICK_TOP) - (float)(row * (BO_BRICK_H + 4)) > (float)BO_BRICK_H) return -1;

    const int col = (int)(x / BO_BRICK_W);
    if (col < 0 || col >= BO_COLS) return -1;

    const int idx = row * BO_COLS + col;
    return c->brick[idx].hp > 0 ? idx : -1;
}

static void break_brick(breakout_core_t *c, int idx, breakout_step_t *out,
                        uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    c->brick[idx].hp--;
    out->hit_x = (int16_t)(bo_brick_x((uint8_t)idx) + BO_BRICK_W / 2);
    out->hit_y = (int16_t)(bo_brick_y((uint8_t)idx) + BO_BRICK_H / 2);
    if (c->brick[idx].hp == 0) {
        c->bricks_left--;
        out->bricks_broken++;
        spawn_drop(c, (float)out->hit_x, (float)out->hit_y, rand_fn, ctx);
    } else {
        out->bricks_broken++; /* a chip still scores, at a lower rate */
    }
}

void breakout_core_step(breakout_core_t *c, float dt, uint32_t (*rand_fn)(void *, uint32_t),
                        void *ctx, breakout_step_t *out) {
    if (!c || !out) return;
    memset(out, 0, sizeof(*out));

    if (c->slow_timer > 0.0f) c->slow_timer -= dt;
    if (c->wide_timer > 0.0f) {
        c->wide_timer -= dt;
        if (c->wide_timer <= 0.0f) {
            c->paddle_w = BO_PADDLE_W;
            breakout_core_move_paddle(c, c->paddle_x);
        }
    }

    /* ---- falling powerups ---- */
    for (int i = 0; i < BO_MAX_DROPS; i++) {
        if (!c->drop[i].active) continue;
        c->drop[i].y += DROP_SPEED * dt;

        const bool caught = c->drop[i].y >= BO_PADDLE_Y - 6 && c->drop[i].y <= BO_PADDLE_Y + 14 &&
                            fabsf(c->drop[i].x - c->paddle_x) < c->paddle_w / 2.0f + 8.0f;
        if (caught) {
            c->drop[i].active = false;
            out->caught = c->drop[i].kind;
            switch (c->drop[i].kind) {
                case BO_DROP_WIDE:
                    c->paddle_w = BO_PADDLE_W_MAX;
                    c->wide_timer = WIDE_SECONDS;
                    break;
                case BO_DROP_SLOW:
                    c->slow_timer = SLOW_SECONDS;
                    break;
                case BO_DROP_LIFE:
                    if (c->lives < 9) c->lives++;
                    break;
                default:
                    break;
            }
        } else if (c->drop[i].y > BO_FIELD) {
            c->drop[i].active = false;
        }
    }

    if (c->ball_stuck) return;

    /* ---- ball ---- */
    const float speed = breakout_core_ball_speed(c);
    const float len = sqrtf(c->ball_vx * c->ball_vx + c->ball_vy * c->ball_vy);
    if (len > 0.001f) {
        /* Renormalise every step so powerups and paddle angles change direction
         * without ever changing speed. A ball that slowly accelerates because of
         * accumulated floating-point drift is a game that stops being fair. */
        c->ball_vx = c->ball_vx / len * speed;
        c->ball_vy = c->ball_vy / len * speed;
    }

    c->ball_x += c->ball_vx * dt;
    c->ball_y += c->ball_vy * dt;

    if (c->ball_x < BO_BALL_R) {
        c->ball_x = BO_BALL_R;
        c->ball_vx = -c->ball_vx;
        out->hit_wall = true;
    } else if (c->ball_x > BO_FIELD - BO_BALL_R) {
        c->ball_x = BO_FIELD - BO_BALL_R;
        c->ball_vx = -c->ball_vx;
        out->hit_wall = true;
    }
    if (c->ball_y < BO_BALL_R) {
        c->ball_y = BO_BALL_R;
        c->ball_vy = -c->ball_vy;
        out->hit_wall = true;
    }

    /* ---- bricks ---- */
    /* Sample the leading edge on each axis separately, so a corner hit
     * reflects on the axis it actually arrived from rather than always
     * inverting y. */
    const float lead_x = c->ball_x + (c->ball_vx > 0 ? BO_BALL_R : -BO_BALL_R);
    const float lead_y = c->ball_y + (c->ball_vy > 0 ? BO_BALL_R : -BO_BALL_R);

    int hit = brick_at(c, lead_x, c->ball_y);
    if (hit >= 0) {
        break_brick(c, hit, out, rand_fn, ctx);
        c->ball_vx = -c->ball_vx;
    }
    hit = brick_at(c, c->ball_x, lead_y);
    if (hit >= 0) {
        break_brick(c, hit, out, rand_fn, ctx);
        c->ball_vy = -c->ball_vy;
    }

    /* ---- paddle ---- */
    if (c->ball_vy > 0 && c->ball_y + BO_BALL_R >= BO_PADDLE_Y &&
        c->ball_y - BO_BALL_R <= BO_PADDLE_Y + BO_PADDLE_H &&
        fabsf(c->ball_x - c->paddle_x) <= c->paddle_w / 2.0f + BO_BALL_R) {

        c->ball_y = (float)(BO_PADDLE_Y - BO_BALL_R);
        /*
         * Where the ball lands on the paddle sets the outgoing angle. This is
         * the entire skill of the game: without it the player is a spectator
         * who occasionally intercepts.
         */
        const float offset = (c->ball_x - c->paddle_x) / (c->paddle_w / 2.0f);
        const float clamped = offset < -1.0f ? -1.0f : (offset > 1.0f ? 1.0f : offset);
        c->ball_vx = clamped * speed * 0.78f;
        c->ball_vy = -sqrtf(speed * speed - c->ball_vx * c->ball_vx);
        out->hit_paddle = true;
    }

    /* ---- lost ---- */
    if (c->ball_y - BO_BALL_R > BO_FIELD) {
        out->lost_life = true;
        if (c->lives > 0) c->lives--;
        if (c->lives == 0) {
            out->game_over = true;
        } else {
            c->paddle_w = BO_PADDLE_W;
            c->wide_timer = 0.0f;
            c->slow_timer = 0.0f;
            park_ball_on_paddle(c);
        }
        return;
    }

    if (c->bricks_left == 0) out->level_cleared = true;
}
