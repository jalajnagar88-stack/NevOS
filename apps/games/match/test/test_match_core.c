/* Match's rules, with no display. */
#include "match_core.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

static match_core_t m;
static uint32_t seed;

/* A small deterministic generator, so a failing board can be reproduced. */
static uint32_t xs_rand(void *ctx, uint32_t upper) {
    uint32_t *s = ctx;
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s % upper;
}

void setUp(void) {
    seed = 0x1234567u;
    match_core_fill(&m, xs_rand, &seed);
}
void tearDown(void) {
}

static void set_board(const char *rows[MT_SIZE]) {
    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++)
            m.tile[r][c] = (uint8_t)(rows[r][c] - '0');
    }
}

/* ------------------------------------------------------------------- setup */

/* An opening board that already contains a match means free points the player
 * did not earn, and a board that scores itself before they touch it. */
static void test_a_fresh_board_has_no_matches_and_a_move(void) {
    for (int trial = 0; trial < 60; trial++) {
        seed = 0x9E3779B9u + (uint32_t)trial * 7919u;
        match_core_fill(&m, xs_rand, &seed);

        char msg[64];
        snprintf(msg, sizeof(msg), "trial %d opened with a match", trial);
        TEST_ASSERT_FALSE_MESSAGE(match_core_has_match(&m), msg);
        snprintf(msg, sizeof(msg), "trial %d opened with no legal move", trial);
        TEST_ASSERT_TRUE_MESSAGE(match_core_has_move(&m), msg);
    }
}

static void test_every_tile_is_a_real_colour(void) {
    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            TEST_ASSERT_TRUE_MESSAGE(m.tile[r][c] < MT_COLORS, "a tile has no colour");
        }
    }
}

/* -------------------------------------------------------------- adjacency */

static void test_only_orthogonal_neighbours_are_adjacent(void) {
    TEST_ASSERT_TRUE(match_core_adjacent(2, 2, 2, 3));
    TEST_ASSERT_TRUE(match_core_adjacent(2, 2, 3, 2));
    TEST_ASSERT_FALSE_MESSAGE(match_core_adjacent(2, 2, 3, 3), "diagonals are not adjacent");
    TEST_ASSERT_FALSE_MESSAGE(match_core_adjacent(2, 2, 2, 4), "two apart is not adjacent");
    TEST_ASSERT_FALSE_MESSAGE(match_core_adjacent(2, 2, 2, 2), "a cell is not its own neighbour");
    TEST_ASSERT_FALSE_MESSAGE(match_core_adjacent(0, 0, 0, MT_SIZE), "off the board");
}

/* ------------------------------------------------------------------ swaps */

static void test_a_swap_that_makes_a_match_is_accepted(void) {
    static const char *rows[MT_SIZE] = {
        "0111000", /* swapping (0,0) with (1,0) puts three 1s in row 0 */
        "1000000", "2323232", "3232323", "2323232", "3232323", "2323232",
    };
    set_board(rows);
    TEST_ASSERT_TRUE(match_core_swap(&m, 0, 0, 1, 0));
    TEST_ASSERT_EQUAL_UINT8(1, m.tile[0][0]);
    TEST_ASSERT_TRUE(match_core_has_match(&m));
}

/* A swap that achieves nothing must be refused, not shown and then undone. */
static void test_a_pointless_swap_is_refused_and_leaves_the_board_alone(void) {
    const match_core_t before = m;
    bool tried = false;

    for (uint8_t r = 0; r < MT_SIZE && !tried; r++) {
        for (uint8_t c = 0; c + 1 < MT_SIZE && !tried; c++) {
            match_core_t probe = m;
            if (match_core_swap(&probe, r, c, r, (uint8_t)(c + 1))) continue;
            tried = true;
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &probe, sizeof(m),
                                             "a refused swap changed the board");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(tried, "every swap on this board was legal");
}

static void test_non_adjacent_swaps_are_refused(void) {
    const match_core_t before = m;
    TEST_ASSERT_FALSE(match_core_swap(&m, 0, 0, 3, 3));
    TEST_ASSERT_FALSE(match_core_swap(&m, 0, 0, 0, 4));
    TEST_ASSERT_EQUAL_MEMORY(&before, &m, sizeof(m));
}

/* --------------------------------------------------------------- resolving */

static void test_a_row_of_three_is_cleared(void) {
    static const char *rows[MT_SIZE] = {
        "1110000", "2323232", "3232323", "2323232", "3232323", "2323232", "3232323",
    };
    set_board(rows);

    match_resolve_t out;
    TEST_ASSERT_TRUE(match_core_resolve(&m, 1, xs_rand, &seed, &out));
    TEST_ASSERT_TRUE_MESSAGE(out.cleared >= 3, "the row of three was not cleared");
    TEST_ASSERT_TRUE(out.mask[0][0] && out.mask[0][1] && out.mask[0][2]);
}

static void test_a_column_of_three_is_cleared(void) {
    static const char *rows[MT_SIZE] = {
        "1232323", "1323232", "1232323", "3232323", "2323232", "3232323", "2323232",
    };
    set_board(rows);

    match_resolve_t out;
    TEST_ASSERT_TRUE(match_core_resolve(&m, 1, xs_rand, &seed, &out));
    TEST_ASSERT_TRUE(out.mask[0][0] && out.mask[1][0] && out.mask[2][0]);
}

/* A run longer than three must clear entirely, not just the first three. */
static void test_a_run_of_five_clears_all_five(void) {
    static const char *rows[MT_SIZE] = {
        "1111123", "2323232", "3232323", "2323232", "3232323", "2323232", "3232323",
    };
    set_board(rows);

    match_resolve_t out;
    TEST_ASSERT_TRUE(match_core_resolve(&m, 1, xs_rand, &seed, &out));
    for (uint8_t c = 0; c < 5; c++) {
        TEST_ASSERT_TRUE_MESSAGE(out.mask[0][c], "part of a five-run survived");
    }
}

static void test_resolving_a_quiet_board_reports_nothing(void) {
    match_resolve_t out;
    TEST_ASSERT_FALSE(match_core_resolve(&m, 1, xs_rand, &seed, &out));
    TEST_ASSERT_EQUAL_UINT8(0, out.cleared);
}

/* Cleared tiles must be filled from above, never left as holes. */
static void test_the_board_is_full_after_resolving(void) {
    static const char *rows[MT_SIZE] = {
        "1110000", "2323232", "3232323", "2323232", "3232323", "2323232", "3232323",
    };
    set_board(rows);

    match_resolve_t out;
    uint8_t guard = 0;
    bool again = match_core_resolve(&m, 1, xs_rand, &seed, &out);
    while (again && out.more && guard++ < 30) {
        again = match_core_resolve(&m, (uint8_t)(out.cascade + 1), xs_rand, &seed, &out);
    }

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(MT_EMPTY, m.tile[r][c], "a hole was left in the board");
        }
    }
}

/* Cascades terminate. A refill that keeps making matches forever would hang
 * the render task, which is the worst possible failure here. */
static void test_cascades_terminate(void) {
    for (int trial = 0; trial < 40; trial++) {
        seed = 0xBEEF0000u + (uint32_t)trial;
        match_core_fill(&m, xs_rand, &seed);
        /* Force a match, then resolve to exhaustion. */
        m.tile[3][2] = m.tile[3][3] = m.tile[3][4] = 1;

        match_resolve_t out;
        int passes = 0;
        bool again = match_core_resolve(&m, 1, xs_rand, &seed, &out);
        while (again && out.more) {
            again = match_core_resolve(&m, (uint8_t)(out.cascade + 1), xs_rand, &seed, &out);
            TEST_ASSERT_TRUE_MESSAGE(++passes < 200, "cascade did not terminate");
        }
    }
}

/* The board must always be playable; a dead one is a bug the player sits through. */
static void test_the_board_always_has_a_move_after_resolving(void) {
    for (int trial = 0; trial < 30; trial++) {
        seed = 0xC0FFEE00u + (uint32_t)trial;
        match_core_fill(&m, xs_rand, &seed);
        m.tile[3][2] = m.tile[3][3] = m.tile[3][4] = 2;

        match_resolve_t out;
        bool again = match_core_resolve(&m, 1, xs_rand, &seed, &out);
        int guard = 0;
        while (again && out.more && guard++ < 60) {
            again = match_core_resolve(&m, (uint8_t)(out.cascade + 1), xs_rand, &seed, &out);
        }
        TEST_ASSERT_TRUE_MESSAGE(match_core_has_move(&m), "resolved into a dead board");
    }
}

/* ---------------------------------------------------------------- scoring */

/* A deep cascade must be worth more than the same tiles cleared separately,
 * or there is no reason to set one up. */
static void test_deeper_cascades_score_more(void) {
    TEST_ASSERT_TRUE(match_core_score_for(3, 2) > match_core_score_for(3, 1));
    TEST_ASSERT_TRUE(match_core_score_for(3, 4) > match_core_score_for(6, 1));
    TEST_ASSERT_EQUAL_UINT32(match_core_score_for(3, 1), match_core_score_for(3, 0));
}

static void test_null_is_survivable(void) {
    match_core_fill(NULL, xs_rand, &seed);
    TEST_ASSERT_FALSE(match_core_has_match(NULL));
    TEST_ASSERT_FALSE(match_core_has_move(NULL));
    TEST_ASSERT_FALSE(match_core_swap(NULL, 0, 0, 0, 1));
    match_resolve_t out;
    TEST_ASSERT_FALSE(match_core_resolve(NULL, 1, xs_rand, &seed, &out));
    TEST_ASSERT_FALSE(match_core_resolve(&m, 1, xs_rand, &seed, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_board_has_no_matches_and_a_move);
    RUN_TEST(test_every_tile_is_a_real_colour);
    RUN_TEST(test_only_orthogonal_neighbours_are_adjacent);
    RUN_TEST(test_a_swap_that_makes_a_match_is_accepted);
    RUN_TEST(test_a_pointless_swap_is_refused_and_leaves_the_board_alone);
    RUN_TEST(test_non_adjacent_swaps_are_refused);
    RUN_TEST(test_a_row_of_three_is_cleared);
    RUN_TEST(test_a_column_of_three_is_cleared);
    RUN_TEST(test_a_run_of_five_clears_all_five);
    RUN_TEST(test_resolving_a_quiet_board_reports_nothing);
    RUN_TEST(test_the_board_is_full_after_resolving);
    RUN_TEST(test_cascades_terminate);
    RUN_TEST(test_the_board_always_has_a_move_after_resolving);
    RUN_TEST(test_deeper_cascades_score_more);
    RUN_TEST(test_null_is_survivable);
    return UNITY_END();
}
