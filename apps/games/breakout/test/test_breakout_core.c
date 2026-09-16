/* Breakout's rules, with no display. */
#include "breakout_core.h"
#include "unity.h"
#include <math.h>
#include <string.h>

static breakout_core_t c;
static uint32_t seq;

static uint32_t seq_rand(void *ctx, uint32_t upper) {
    (void)ctx;
    return (seq++) % upper;
}
/* Never rolls a powerup: POWERUP_ODDS is 6 and a drop needs a 0. */
static uint32_t no_drop_rand(void *ctx, uint32_t upper) {
    (void)ctx;
    return upper > 1 ? 1u : 0u;
}

void setUp(void) {
    seq = 1;
    breakout_core_reset(&c);
    breakout_core_start_level(&c, 1, no_drop_rand, NULL);
}
void tearDown(void) {
}

static void step(float dt) {
    breakout_step_t out;
    breakout_core_step(&c, dt, no_drop_rand, NULL, &out);
}

static void test_a_fresh_level_is_full_of_bricks(void) {
    TEST_ASSERT_EQUAL_UINT8(BO_BRICKS, c.bricks_left);
    TEST_ASSERT_EQUAL_UINT8(BO_LIVES, c.lives);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_stuck, "ball should wait on the paddle");
}

static void test_a_stuck_ball_does_not_move(void) {
    const float y = c.ball_y;
    for (int i = 0; i < 30; i++)
        step(1.0f / 60.0f);
    TEST_ASSERT_EQUAL_FLOAT(y, c.ball_y);
}

static void test_the_stuck_ball_follows_the_paddle(void) {
    breakout_core_move_paddle(&c, 120.0f);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, c.ball_x);
}

static void test_the_paddle_cannot_leave_the_field(void) {
    breakout_core_move_paddle(&c, -500.0f);
    TEST_ASSERT_TRUE(c.paddle_x - c.paddle_w / 2.0f >= -0.01f);
    breakout_core_move_paddle(&c, 9999.0f);
    TEST_ASSERT_TRUE(c.paddle_x + c.paddle_w / 2.0f <= BO_FIELD + 0.01f);
}

/* A perfectly vertical launch can bounce up and down forever without ever
 * touching a brick, so the launch must be off-axis. */
static void test_launch_is_upward_and_off_vertical(void) {
    breakout_core_launch(&c);
    TEST_ASSERT_FALSE(c.ball_stuck);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_vy < 0, "launched downward");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(c.ball_vx) > 1.0f, "launched perfectly vertical");
}

/*
 * Speed must be conserved exactly. Accumulated floating-point drift that makes
 * the ball creep faster is the difference between a tuned difficulty curve and
 * a game that quietly becomes impossible.
 */
static void test_ball_speed_is_conserved_over_a_long_rally(void) {
    breakout_core_launch(&c);
    const float want = breakout_core_ball_speed(&c);

    for (int i = 0; i < 4000; i++) {
        breakout_core_move_paddle(&c, c.ball_x); /* a perfect player */
        step(1.0f / 60.0f);
        if (c.ball_stuck) breakout_core_launch(&c);
    }
    const float got = sqrtf(c.ball_vx * c.ball_vx + c.ball_vy * c.ball_vy);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, want, got, "ball speed drifted");
}

static void test_walls_reflect(void) {
    breakout_core_launch(&c);
    c.ball_x = BO_BALL_R + 1.0f;
    c.ball_vx = -200.0f;
    c.ball_vy = -100.0f;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.hit_wall);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_vx > 0, "did not bounce off the left wall");
    TEST_ASSERT_TRUE_MESSAGE(c.ball_x >= BO_BALL_R, "escaped through the wall");
}

static void test_hitting_a_brick_removes_it(void) {
    breakout_core_launch(&c);
    /* Put the ball just under the bottom row, travelling up. */
    c.ball_x = BO_BRICK_W / 2.0f;
    c.ball_y = (float)(bo_brick_y(BO_BRICKS - BO_COLS) + BO_BRICK_H + BO_BALL_R);
    c.ball_vx = 0.0f;
    c.ball_vy = -200.0f;

    breakout_step_t out;
    for (int i = 0; i < 10 && out.bricks_broken == 0; i++) {
        breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, out.bricks_broken, "the brick survived");
    TEST_ASSERT_EQUAL_UINT8(BO_BRICKS - 1, c.bricks_left);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_vy > 0, "did not reflect off the brick");
}

/* Where the ball lands on the paddle sets the outgoing angle. Without that the
 * player is a spectator who occasionally intercepts. */
static void test_paddle_hit_position_steers_the_ball(void) {
    breakout_core_launch(&c);
    breakout_core_move_paddle(&c, 200.0f);

    c.ball_x = 200.0f + c.paddle_w / 2.0f - 4.0f; /* far right of the paddle */
    c.ball_y = (float)(BO_PADDLE_Y - BO_BALL_R + 1);
    c.ball_vx = 0.0f;
    c.ball_vy = 200.0f;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.hit_paddle);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_vx > 10.0f, "a right-edge hit did not send it right");
    TEST_ASSERT_TRUE_MESSAGE(c.ball_vy < 0, "did not bounce upward");
}

static void test_dropping_the_ball_costs_a_life_and_reparks(void) {
    breakout_core_launch(&c);
    c.ball_y = BO_FIELD + 50.0f;
    c.ball_vy = 200.0f;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.lost_life);
    TEST_ASSERT_FALSE(out.game_over);
    TEST_ASSERT_EQUAL_UINT8(BO_LIVES - 1, c.lives);
    TEST_ASSERT_TRUE_MESSAGE(c.ball_stuck, "did not return the ball to the paddle");
}

static void test_the_last_life_ends_the_game(void) {
    c.lives = 1;
    breakout_core_launch(&c);
    c.ball_y = BO_FIELD + 50.0f;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.game_over);
    TEST_ASSERT_EQUAL_UINT8(0, c.lives);
}

static void test_clearing_every_brick_reports_the_level(void) {
    breakout_core_launch(&c);
    for (int i = 0; i < BO_BRICKS; i++)
        c.brick[i].hp = 0;
    c.bricks_left = 0;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.level_cleared);
}

static void test_later_levels_are_faster_but_capped(void) {
    breakout_core_start_level(&c, 1, no_drop_rand, NULL);
    const float l1 = c.speed;
    breakout_core_start_level(&c, 4, no_drop_rand, NULL);
    TEST_ASSERT_TRUE_MESSAGE(c.speed > l1, "level 4 was not faster than level 1");

    breakout_core_start_level(&c, 60, no_drop_rand, NULL);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, BO_SPEED_MAX, c.speed, "speed was not capped");
}

static void test_the_slow_powerup_actually_slows(void) {
    const float normal = breakout_core_ball_speed(&c);
    c.slow_timer = 5.0f;
    TEST_ASSERT_TRUE(breakout_core_ball_speed(&c) < normal);
    c.slow_timer = 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(normal, breakout_core_ball_speed(&c));
}

static void test_a_caught_wide_powerup_expires(void) {
    c.drop[0].active = true;
    c.drop[0].kind = BO_DROP_WIDE;
    c.drop[0].x = c.paddle_x;
    c.drop[0].y = (float)BO_PADDLE_Y - 2.0f;

    breakout_step_t out;
    breakout_core_step(&c, 1.0f / 60.0f, no_drop_rand, NULL, &out);
    TEST_ASSERT_EQUAL_INT(BO_DROP_WIDE, out.caught);
    TEST_ASSERT_EQUAL_FLOAT(BO_PADDLE_W_MAX, c.paddle_w);

    for (int i = 0; i < 60 * 15; i++)
        step(1.0f / 60.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(BO_PADDLE_W, c.paddle_w, "the wide paddle never expired");
}

static void test_powerups_do_spawn_when_the_roll_allows(void) {
    seq = 0;
    breakout_core_reset(&c);
    breakout_core_start_level(&c, 1, seq_rand, NULL);
    breakout_core_launch(&c);

    c.ball_x = BO_BRICK_W / 2.0f;
    c.ball_y = (float)(bo_brick_y(BO_BRICKS - BO_COLS) + BO_BRICK_H + BO_BALL_R);
    c.ball_vx = 0.0f;
    c.ball_vy = -300.0f;

    breakout_step_t out;
    bool any = false;
    for (int i = 0; i < 400 && !any; i++) {
        breakout_core_step(&c, 1.0f / 60.0f, seq_rand, NULL, &out);
        for (int d = 0; d < BO_MAX_DROPS; d++)
            any = any || c.drop[d].active;
        if (c.ball_stuck) breakout_core_launch(&c);
        breakout_core_move_paddle(&c, c.ball_x);
    }
    TEST_ASSERT_TRUE_MESSAGE(any, "no powerup ever dropped");
}

static void test_null_is_survivable(void) {
    breakout_core_reset(NULL);
    breakout_core_move_paddle(NULL, 10.0f);
    breakout_core_launch(NULL);
    breakout_core_step(NULL, 0.016f, no_drop_rand, NULL, NULL);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, breakout_core_ball_speed(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_level_is_full_of_bricks);
    RUN_TEST(test_a_stuck_ball_does_not_move);
    RUN_TEST(test_the_stuck_ball_follows_the_paddle);
    RUN_TEST(test_the_paddle_cannot_leave_the_field);
    RUN_TEST(test_launch_is_upward_and_off_vertical);
    RUN_TEST(test_ball_speed_is_conserved_over_a_long_rally);
    RUN_TEST(test_walls_reflect);
    RUN_TEST(test_hitting_a_brick_removes_it);
    RUN_TEST(test_paddle_hit_position_steers_the_ball);
    RUN_TEST(test_dropping_the_ball_costs_a_life_and_reparks);
    RUN_TEST(test_the_last_life_ends_the_game);
    RUN_TEST(test_clearing_every_brick_reports_the_level);
    RUN_TEST(test_later_levels_are_faster_but_capped);
    RUN_TEST(test_the_slow_powerup_actually_slows);
    RUN_TEST(test_a_caught_wide_powerup_expires);
    RUN_TEST(test_powerups_do_spawn_when_the_roll_allows);
    RUN_TEST(test_null_is_survivable);
    return UNITY_END();
}
