/* Runner's rules, with no display. */
#include "runner_core.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

#define DT (1.0f / 60.0f)

static runner_core_t c;

static uint32_t mid_rand(void *ctx, uint32_t upper) {
    (void)ctx;
    return upper / 2;
}
/* No obstacles at all, for tests about jumping. */
static void clear_obstacles(void) {
    memset(c.obs, 0, sizeof(c.obs));
    c.next_gap = 1e9f;
}

void setUp(void) {
    runner_core_reset(&c);
}
void tearDown(void) {
}

static runner_step_t advance(int steps) {
    runner_step_t out;
    memset(&out, 0, sizeof(out));
    for (int i = 0; i < steps; i++) {
        runner_step_t s;
        runner_core_step(&c, DT, mid_rand, NULL, &s);
        if (s.crashed) out.crashed = true;
        if (s.landed) out.landed = true;
        if (s.jumped) out.jumped = true;
        out.cleared = (uint8_t)(out.cleared + s.cleared);
        if (s.crashed) break;
    }
    return out;
}

static void test_starts_on_the_ground(void) {
    TEST_ASSERT_TRUE(c.on_ground);
    TEST_ASSERT_EQUAL_FLOAT((float)(RN_GROUND_Y - RN_PLAYER_H), c.player_y);
}

static void test_jumping_leaves_the_ground_and_comes_back(void) {
    clear_obstacles();
    runner_core_jump(&c);
    TEST_ASSERT_FALSE(c.on_ground);

    advance(5);
    TEST_ASSERT_TRUE_MESSAGE(c.player_y < (float)(RN_GROUND_Y - RN_PLAYER_H), "never rose");

    const runner_step_t out = advance(120);
    TEST_ASSERT_TRUE_MESSAGE(out.landed, "never came down");
    TEST_ASSERT_TRUE(c.on_ground);
    TEST_ASSERT_EQUAL_FLOAT((float)(RN_GROUND_Y - RN_PLAYER_H), c.player_y);
}

static void test_no_double_jump(void) {
    clear_obstacles();
    runner_core_jump(&c);
    advance(6);
    const float apex_vel = c.vel_y;
    runner_core_jump(&c); /* mid-air: must be buffered, not applied */
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(apex_vel, c.vel_y, "a second jump fired in mid-air");
}

/*
 * Coyote time: a jump pressed just after leaving the ground still works.
 * Without it every near-miss feels like the game's fault.
 */
static void test_coyote_time_allows_a_slightly_late_jump(void) {
    clear_obstacles();
    c.on_ground = false; /* just walked off, coyote still running */
    c.coyote = RN_COYOTE_S;
    c.vel_y = 0.0f;

    runner_core_jump(&c);
    TEST_ASSERT_TRUE_MESSAGE(c.vel_y < 0.0f, "a jump inside coyote time was refused");
}

static void test_coyote_time_expires(void) {
    clear_obstacles();
    runner_core_jump(&c);
    advance(30); /* well into the air; coyote long gone */
    const float before = c.vel_y;
    runner_core_jump(&c);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(before, c.vel_y, "jumped after coyote time expired");
}

/*
 * Input buffering: a jump pressed just before landing fires on landing.
 * Swallowing it is the most common way a runner feels unresponsive.
 */
static void test_a_jump_pressed_before_landing_fires_on_landing(void) {
    clear_obstacles();
    runner_core_jump(&c);
    /* Fall until just above the ground. */
    while (!c.on_ground) {
        runner_step_t s;
        runner_core_step(&c, DT, mid_rand, NULL, &s);
        if (c.vel_y > 0.0f && c.player_y > (float)(RN_GROUND_Y - RN_PLAYER_H) - 20.0f) break;
    }
    TEST_ASSERT_FALSE(c.on_ground);

    runner_core_jump(&c); /* buffered */
    TEST_ASSERT_TRUE(c.buffered > 0.0f);

    const runner_step_t out = advance(12);
    TEST_ASSERT_TRUE_MESSAGE(out.jumped, "the buffered jump was swallowed on landing");
}

static void test_the_buffer_expires(void) {
    clear_obstacles();
    runner_core_jump(&c);
    advance(4);
    runner_core_jump(&c); /* buffered this early, it should lapse before landing */
    advance(60);
    TEST_ASSERT_TRUE_MESSAGE(c.on_ground, "should have landed");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, c.buffered, "a stale buffered jump survived");
}

/* ------------------------------------------------------------- difficulty */

static void test_speed_climbs_and_is_capped(void) {
    clear_obstacles();
    const float start = c.speed;
    advance(600);
    TEST_ASSERT_TRUE_MESSAGE(c.speed > start, "never got faster");

    c.speed = RN_SPEED_MAX;
    advance(600);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, RN_SPEED_MAX, c.speed, "speed was not capped");
}

/*
 * Every gap must stay clearable as the speed climbs. The game gets harder by
 * leaving less margin, never by becoming impossible — which is the one thing a
 * procedural generator must not be allowed to do.
 */
static void test_gaps_stay_wider_than_a_jump_at_every_speed(void) {
    for (float speed = RN_SPEED_START; speed <= RN_SPEED_MAX; speed += 20.0f) {
        runner_core_reset(&c);
        c.speed = speed;
        c.since_spawn = 1e9f; /* force a spawn on the next step */

        runner_step_t out;
        runner_core_step(&c, DT, mid_rand, NULL, &out);

        const float span = runner_core_jump_span(&c);
        char msg[96];
        snprintf(msg, sizeof(msg), "gap %.0f is not clearable at speed %.0f (jump spans %.0f)",
                 (double)c.next_gap, (double)speed, (double)span);
        TEST_ASSERT_TRUE_MESSAGE(c.next_gap > span, msg);
    }
}

static void test_the_first_gap_is_generous(void) {
    TEST_ASSERT_TRUE_MESSAGE(c.next_gap > runner_core_jump_span(&c) * 1.5f,
                             "the opening gap is tight enough to kill on sight");
}

/* ----------------------------------------------------------------- crashing */

static void test_running_into_an_obstacle_crashes(void) {
    clear_obstacles();
    c.obs[0].active = true;
    c.obs[0].x = (float)(RN_PLAYER_X + 4);
    c.obs[0].w = 24;
    c.obs[0].h = 40;

    runner_step_t out;
    runner_core_step(&c, DT, mid_rand, NULL, &out);
    TEST_ASSERT_TRUE(out.crashed);
}

static void test_jumping_over_an_obstacle_clears_it(void) {
    clear_obstacles();
    c.speed = RN_SPEED_START;
    c.obs[0].active = true;
    c.obs[0].w = 22;
    c.obs[0].h = 34;
    /* Place it so the player meets it near the apex of a jump started now. */
    c.obs[0].x = (float)RN_PLAYER_X + runner_core_jump_span(&c) * 0.45f;

    runner_core_jump(&c);
    const runner_step_t out = advance(120);
    TEST_ASSERT_FALSE_MESSAGE(out.crashed, "hit an obstacle it should have cleared");
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, out.cleared, "never counted the obstacle as passed");
}

static void test_a_tall_obstacle_still_catches_a_grounded_player(void) {
    clear_obstacles();
    c.obs[0].active = true;
    c.obs[0].x = (float)(RN_PLAYER_X + 2);
    c.obs[0].w = 20;
    c.obs[0].h = 26; /* the shortest obstacle */

    runner_step_t out;
    runner_core_step(&c, DT, mid_rand, NULL, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.crashed, "walked through the shortest obstacle");
}

static void test_obstacles_are_recycled_not_leaked(void) {
    c.next_gap = 40.0f;
    for (int i = 0; i < 4000; i++) {
        runner_step_t out;
        runner_core_step(&c, DT, mid_rand, NULL, &out);
        c.player_y = -500.0f; /* fly over everything so it never crashes */
        c.on_ground = false;
        c.vel_y = 0.0f;

        int live = 0;
        for (int k = 0; k < RN_MAX_OBS; k++)
            live += c.obs[k].active ? 1 : 0;
        TEST_ASSERT_TRUE_MESSAGE(live <= RN_MAX_OBS, "obstacle pool overflowed");
    }
}

static void test_null_is_survivable(void) {
    runner_core_reset(NULL);
    runner_core_jump(NULL);
    runner_core_step(NULL, DT, mid_rand, NULL, NULL);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, runner_core_jump_span(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_on_the_ground);
    RUN_TEST(test_jumping_leaves_the_ground_and_comes_back);
    RUN_TEST(test_no_double_jump);
    RUN_TEST(test_coyote_time_allows_a_slightly_late_jump);
    RUN_TEST(test_coyote_time_expires);
    RUN_TEST(test_a_jump_pressed_before_landing_fires_on_landing);
    RUN_TEST(test_the_buffer_expires);
    RUN_TEST(test_speed_climbs_and_is_capped);
    RUN_TEST(test_gaps_stay_wider_than_a_jump_at_every_speed);
    RUN_TEST(test_the_first_gap_is_generous);
    RUN_TEST(test_running_into_an_obstacle_crashes);
    RUN_TEST(test_jumping_over_an_obstacle_clears_it);
    RUN_TEST(test_a_tall_obstacle_still_catches_a_grounded_player);
    RUN_TEST(test_obstacles_are_recycled_not_leaked);
    RUN_TEST(test_null_is_survivable);
    return UNITY_END();
}
