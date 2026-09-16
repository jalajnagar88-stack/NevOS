/* Snake's rules, with no display and no randomness. */
#include "snake_core.h"
#include "unity.h"
#include <string.h>

static snake_core_t s;

void setUp(void) {
    snake_core_reset(&s, false);
}
void tearDown(void) {
}

/* A deterministic stand-in for the engine's RNG. */
static uint32_t seq_rand(void *ctx, uint32_t upper) {
    uint32_t *n = ctx;
    return (*n)++ % upper;
}

static void drive(int steps) {
    for (int i = 0; i < steps; i++)
        snake_core_step(&s);
}

static void test_reset_places_a_snake_facing_right(void) {
    TEST_ASSERT_EQUAL_UINT16(SNAKE_START_LEN, s.len);
    TEST_ASSERT_EQUAL_INT8(1, s.dx);
    TEST_ASSERT_EQUAL_INT8(0, s.dy);
    /* The body trails behind the head, not in front of it. */
    TEST_ASSERT_TRUE(s.x[0] > s.x[1]);
    TEST_ASSERT_EQUAL_UINT8(s.y[0], s.y[1]);
}

static void test_moving_shifts_the_whole_body(void) {
    const uint8_t head_x = s.x[0];
    snake_core_set_food(&s, 0, 0);
    TEST_ASSERT_EQUAL_INT(SNAKE_MOVED, snake_core_step(&s));

    TEST_ASSERT_EQUAL_UINT8(head_x + 1, s.x[0]);
    TEST_ASSERT_EQUAL_UINT8(head_x, s.x[1]); /* segment 1 took the head's cell */
    TEST_ASSERT_EQUAL_UINT16(SNAKE_START_LEN, s.len);
}

static void test_eating_grows_and_respawns(void) {
    snake_core_set_food(&s, (uint8_t)(s.x[0] + 1), s.y[0]);
    TEST_ASSERT_EQUAL_INT(SNAKE_ATE, snake_core_step(&s));
    TEST_ASSERT_EQUAL_UINT16(SNAKE_START_LEN + 1, s.len);

    uint32_t counter = 7;
    TEST_ASSERT_TRUE(snake_core_spawn_food(&s, seq_rand, &counter));
    TEST_ASSERT_FALSE_MESSAGE(snake_core_occupied(&s, s.food_x, s.food_y),
                              "food spawned inside the snake");
}

static void test_growth_is_capped(void) {
    for (int i = 0; i < SNAKE_MAX_LEN + 20; i++) {
        snake_core_set_food(&s, (uint8_t)(s.x[0] + s.dx), (uint8_t)(s.y[0] + s.dy));
        if (snake_core_step(&s) == SNAKE_DIED) break;
        /* Walk a spiral so it does not hit a wall immediately. */
        if (i % 6 == 5) snake_core_turn(&s, s.dy, (int8_t)-s.dx);
    }
    TEST_ASSERT_TRUE_MESSAGE(s.len <= SNAKE_MAX_LEN, "grew past the segment pool");
}

/* ------------------------------------------------------------------- death */

static void test_running_into_a_wall_kills(void) {
    snake_core_set_food(&s, 0, 0);
    for (int i = 0; i < SNAKE_GRID; i++) {
        if (snake_core_step(&s) == SNAKE_DIED) {
            TEST_PASS();
            return;
        }
    }
    TEST_FAIL_MESSAGE("walked off the grid without dying");
}

static void test_wrapping_survives_the_edge(void) {
    snake_core_reset(&s, true);
    snake_core_set_food(&s, 0, 0);
    for (int i = 0; i < SNAKE_GRID + 2; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(SNAKE_DIED, snake_core_step(&s), "wrap mode died at a wall");
    }
    TEST_ASSERT_TRUE(s.x[0] < SNAKE_GRID);
}

/*
 * The tail vacates its cell on the same step the head arrives, so entering it
 * is legal. This is the single most common way a Snake implementation feels
 * broken, in either direction.
 */
static void test_entering_the_vacating_tail_cell_is_legal(void) {
    snake_core_set_food(&s, 0, 0);
    /* Drive a tight 2x2 loop: right, down, left, up returns onto the old tail. */
    snake_core_step(&s);
    snake_core_turn(&s, 0, 1);
    snake_core_step(&s);
    snake_core_turn(&s, -1, 0);
    snake_core_step(&s);
    snake_core_turn(&s, 0, -1);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(SNAKE_DIED, snake_core_step(&s),
                                  "died entering a cell the tail had just left");
}

/* ...but if it is about to grow, the tail does not move, so that cell is fatal. */
static void test_growing_into_the_tail_cell_is_fatal(void) {
    snake_core_reset(&s, true);
    s.len = 5;
    for (uint16_t i = 0; i < s.len; i++) {
        s.x[i] = (uint8_t)(10 - i);
        s.y[i] = 10;
    }
    s.dx = 1;
    s.dy = 0;

    /* Curl the body so the tail cell sits directly ahead of the head. */
    s.x[0] = 10;
    s.y[0] = 10;
    s.x[1] = 10;
    s.y[1] = 11;
    s.x[2] = 11;
    s.y[2] = 11;
    s.x[3] = 12;
    s.y[3] = 11;
    s.x[4] = 11;
    s.y[4] = 10;                     /* tail, directly right of the head */
    snake_core_set_food(&s, 11, 10); /* food on the tail cell: it will grow */

    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAKE_DIED, snake_core_step(&s),
                                  "grew into its own stationary tail and lived");
}

static void test_self_collision_kills(void) {
    snake_core_reset(&s, true);
    s.len = 6;
    /* A closed loop with the head about to re-enter the middle of the body. */
    s.x[0] = 10;
    s.y[0] = 10;
    s.x[1] = 9;
    s.y[1] = 10;
    s.x[2] = 9;
    s.y[2] = 11;
    s.x[3] = 10;
    s.y[3] = 11;
    s.x[4] = 11;
    s.y[4] = 11;
    s.x[5] = 11;
    s.y[5] = 12;
    s.dx = 0;
    s.dy = 1; /* straight into segment 3 */
    snake_core_set_food(&s, 0, 0);

    TEST_ASSERT_EQUAL_INT(SNAKE_DIED, snake_core_step(&s));
}

/* ------------------------------------------------------------------ turning */

static void test_a_reversal_is_refused_not_fatal(void) {
    snake_core_set_food(&s, 0, 0);
    snake_core_turn(&s, -1, 0); /* straight back into itself */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(SNAKE_DIED, snake_core_step(&s), "a fumbled swipe killed");
    TEST_ASSERT_EQUAL_INT8_MESSAGE(1, s.dx, "the reversal was applied anyway");
}

/*
 * Two quick turns must both happen, in order, and must never combine into a
 * 180. With a single-slot buffer the second turn replaced the first and applied
 * `left` straight from `right` — a reversal into the body.
 */
static void test_two_fast_turns_are_both_applied_in_order(void) {
    snake_core_set_food(&s, 0, 0);
    snake_core_turn(&s, 0, 1);  /* down, from right  */
    snake_core_turn(&s, -1, 0); /* left, from down    */

    snake_core_step(&s);
    TEST_ASSERT_EQUAL_INT8_MESSAGE(0, s.dx, "first turn was skipped");
    TEST_ASSERT_EQUAL_INT8_MESSAGE(1, s.dy, "first turn was skipped");

    snake_core_step(&s);
    TEST_ASSERT_EQUAL_INT8_MESSAGE(-1, s.dx, "second turn was dropped");
    TEST_ASSERT_EQUAL_INT8(0, s.dy);
}

/* Input beyond the queue depth is dropped, not applied late. */
static void test_the_turn_queue_is_bounded(void) {
    snake_core_set_food(&s, 0, 0);
    snake_core_turn(&s, 0, 1);
    snake_core_turn(&s, -1, 0);
    snake_core_turn(&s, 0, -1); /* third: refused */
    TEST_ASSERT_EQUAL_UINT8(SNAKE_TURN_QUEUE, s.queued);
}

static void test_turns_apply_on_the_step_boundary(void) {
    snake_core_set_food(&s, 0, 0);
    const uint8_t y0 = s.y[0];
    snake_core_turn(&s, 0, 1);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(y0, s.y[0], "the turn moved the snake before its step");
    snake_core_step(&s);
    TEST_ASSERT_EQUAL_UINT8(y0 + 1, s.y[0]);
}

static void test_nonsense_turns_are_ignored(void) {
    snake_core_set_food(&s, 0, 0);
    snake_core_turn(&s, 0, 0);
    snake_core_turn(&s, 1, 1); /* diagonal */
    snake_core_turn(NULL, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(0, s.queued);
    drive(1);
    TEST_ASSERT_EQUAL_INT8(1, s.dx);
    TEST_ASSERT_EQUAL_INT8(0, s.dy);
}

static void test_null_is_survivable(void) {
    TEST_ASSERT_EQUAL_INT(SNAKE_DIED, snake_core_step(NULL));
    TEST_ASSERT_FALSE(snake_core_occupied(NULL, 0, 0));
    uint32_t c = 0;
    TEST_ASSERT_FALSE(snake_core_spawn_food(NULL, seq_rand, &c));
    TEST_ASSERT_FALSE(snake_core_spawn_food(&s, NULL, &c));
    snake_core_reset(NULL, false);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_places_a_snake_facing_right);
    RUN_TEST(test_moving_shifts_the_whole_body);
    RUN_TEST(test_eating_grows_and_respawns);
    RUN_TEST(test_growth_is_capped);
    RUN_TEST(test_running_into_a_wall_kills);
    RUN_TEST(test_wrapping_survives_the_edge);
    RUN_TEST(test_entering_the_vacating_tail_cell_is_legal);
    RUN_TEST(test_growing_into_the_tail_cell_is_fatal);
    RUN_TEST(test_self_collision_kills);
    RUN_TEST(test_a_reversal_is_refused_not_fatal);
    RUN_TEST(test_two_fast_turns_are_both_applied_in_order);
    RUN_TEST(test_the_turn_queue_is_bounded);
    RUN_TEST(test_turns_apply_on_the_step_boundary);
    RUN_TEST(test_nonsense_turns_are_ignored);
    RUN_TEST(test_null_is_survivable);
    return UNITY_END();
}
