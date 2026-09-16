#include "runner_core.h"
#include <string.h>

#define OBS_MIN_W 18
#define OBS_MAX_W 30
#define OBS_MIN_H 26
#define OBS_MAX_H 54

float runner_core_jump_span(const runner_core_t *c) {
    if (!c) return 0.0f;
    /* Time to rise and fall back: 2 * v / g. */
    const float air_time = (2.0f * -RN_JUMP_V) / RN_GRAVITY;
    return c->speed * air_time;
}

void runner_core_reset(runner_core_t *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->player_y = (float)(RN_GROUND_Y - RN_PLAYER_H);
    c->on_ground = true;
    c->speed = RN_SPEED_START;
    c->next_gap = 260.0f; /* a generous first gap: nobody should die to the opener */
}

void runner_core_jump(runner_core_t *c) {
    if (!c) return;
    if (c->on_ground || c->coyote > 0.0f) {
        c->vel_y = RN_JUMP_V;
        c->on_ground = false;
        c->coyote = 0.0f;
        c->buffered = 0.0f;
        return;
    }
    /* In the air: remember it briefly, in case the player pressed just before
     * landing. Swallowing that input is the most common way a runner feels
     * unresponsive. */
    c->buffered = RN_BUFFER_S;
}

static void spawn(runner_core_t *c, uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    for (int i = 0; i < RN_MAX_OBS; i++) {
        if (c->obs[i].active) continue;
        c->obs[i].active = true;
        c->obs[i].x = (float)RN_FIELD;
        c->obs[i].w = (uint8_t)(OBS_MIN_W + (rand_fn ? rand_fn(ctx, OBS_MAX_W - OBS_MIN_W) : 5));
        c->obs[i].h = (uint8_t)(OBS_MIN_H + (rand_fn ? rand_fn(ctx, OBS_MAX_H - OBS_MIN_H) : 10));

        /*
         * The next gap is derived from the jump span, never from a constant.
         * That is what keeps every gap clearable as the speed climbs: the game
         * gets harder by leaving less margin, not by becoming impossible.
         */
        const float span = runner_core_jump_span(c);
        const float slack =
            0.55f - 0.30f * (c->speed - RN_SPEED_START) / (RN_SPEED_MAX - RN_SPEED_START);
        c->next_gap = span * (1.05f + (slack < 0.25f ? 0.25f : slack));
        if (rand_fn) c->next_gap += (float)rand_fn(ctx, 70);
        return;
    }
}

void runner_core_step(runner_core_t *c, float dt, uint32_t (*rand_fn)(void *, uint32_t), void *ctx,
                      runner_step_t *out) {
    if (!c || !out) return;
    memset(out, 0, sizeof(*out));

    c->elapsed += dt;
    c->speed += RN_SPEED_GAIN * dt;
    if (c->speed > RN_SPEED_MAX) c->speed = RN_SPEED_MAX;

    const float scroll = c->speed * dt;
    c->distance += scroll;

    /* ---- vertical ---- */
    const bool was_airborne = !c->on_ground;
    if (!c->on_ground) {
        c->vel_y += RN_GRAVITY * dt;
        c->player_y += c->vel_y * dt;

        const float floor_y = (float)(RN_GROUND_Y - RN_PLAYER_H);
        if (c->player_y >= floor_y) {
            c->player_y = floor_y;
            c->vel_y = 0.0f;
            c->on_ground = true;
            c->coyote = RN_COYOTE_S;
            out->landed = true;
        }
    } else {
        c->coyote = RN_COYOTE_S;
    }
    if (c->coyote > 0.0f && !c->on_ground) {
        c->coyote -= dt;
        if (c->coyote < 0.0f) c->coyote = 0.0f;
    }

    if (c->buffered > 0.0f) {
        c->buffered -= dt;
        /* Clamp rather than letting it drift negative. Nothing reads it below
         * zero today, but a timer that stores -0.003 instead of 0 is the kind
         * of state that makes a later comparison behave oddly for no reason. */
        if (c->buffered < 0.0f) c->buffered = 0.0f;
        if (c->on_ground) {
            c->vel_y = RN_JUMP_V;
            c->on_ground = false;
            c->buffered = 0.0f;
            out->jumped = true;
        }
    }
    if (was_airborne && out->landed) c->coyote = RN_COYOTE_S;

    /* ---- obstacles ---- */
    c->since_spawn += scroll;
    if (c->since_spawn >= c->next_gap) {
        c->since_spawn = 0.0f;
        spawn(c, rand_fn, ctx);
    }

    const float px1 = (float)RN_PLAYER_X;
    const float px2 = (float)(RN_PLAYER_X + RN_PLAYER_W);
    const float py2 = c->player_y + (float)RN_PLAYER_H;

    for (int i = 0; i < RN_MAX_OBS; i++) {
        if (!c->obs[i].active) continue;
        const float before = c->obs[i].x;
        c->obs[i].x -= scroll;

        if (c->obs[i].x + c->obs[i].w < 0.0f) {
            c->obs[i].active = false;
            continue;
        }
        /* Counted as cleared when its trailing edge passes the player. */
        if (before + c->obs[i].w >= px1 && c->obs[i].x + c->obs[i].w < px1) out->cleared++;

        const float ox1 = c->obs[i].x;
        const float ox2 = c->obs[i].x + (float)c->obs[i].w;
        const float oy1 = (float)(RN_GROUND_Y - c->obs[i].h);

        if (px2 > ox1 && ox2 > px1 && py2 > oy1) {
            out->crashed = true;
            out->crash_x = ox1;
            return;
        }
    }
}
