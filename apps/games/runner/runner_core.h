/*
 * Runner — the rules, with no LVGL.
 *
 * Original endless side-scroller. One button, one jump.
 *
 * The two things that make a one-button runner feel fair rather than cheap are
 * coyote time (a jump still registers for a moment after walking off an edge)
 * and input buffering (a jump pressed just before landing fires on landing).
 * Without them every miss feels like the game's fault. Both are here, and both
 * are tested.
 */
#ifndef RUNNER_CORE_H
#define RUNNER_CORE_H

#include "nev_port/nev_types.h"

#define RN_FIELD       400
#define RN_GROUND_Y    330
#define RN_PLAYER_X    70
#define RN_PLAYER_W    22
#define RN_PLAYER_H    30
#define RN_MAX_OBS     6

#define RN_GRAVITY     1500.0f
#define RN_JUMP_V      (-490.0f)
#define RN_COYOTE_S    0.10f
#define RN_BUFFER_S    0.13f

/* Approachable then steep: a gentle opening speed, a steady climb, a hard cap. */
#define RN_SPEED_START 165.0f
#define RN_SPEED_GAIN  7.0f /* px/s added per second survived */
#define RN_SPEED_MAX   430.0f

typedef struct {
    bool active;
    float x;
    uint8_t w, h;
} rn_obstacle_t;

typedef struct {
    float player_y; /* top edge, in field coordinates */
    float vel_y;
    bool on_ground;

    float coyote;   /* seconds of grace remaining after leaving the ground */
    float buffered; /* seconds remaining on a jump pressed slightly early  */

    rn_obstacle_t obs[RN_MAX_OBS];
    float since_spawn; /* pixels scrolled since the last obstacle */
    float next_gap;

    float speed;
    float distance;
    float elapsed;
} runner_core_t;

typedef struct {
    bool jumped;
    bool landed;
    bool crashed;
    uint8_t cleared; /* obstacles passed this step */
    float crash_x;
} runner_step_t;

void runner_core_reset(runner_core_t *c);

/* Records intent. The jump fires now if it can, and is buffered briefly if not,
 * so a press a fraction early is honoured rather than swallowed. */
void runner_core_jump(runner_core_t *c);

void runner_core_step(runner_core_t *c, float dt, uint32_t (*rand_fn)(void *, uint32_t), void *ctx,
                      runner_step_t *out);

/* The horizontal distance covered by a full jump at the current speed. The
 * spawner uses it to guarantee every gap is clearable. */
float runner_core_jump_span(const runner_core_t *c);

#endif /* RUNNER_CORE_H */
