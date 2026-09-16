#include "snake_core.h"

void snake_core_reset(snake_core_t *s, bool wrap) {
    if (!s) return;
    s->len = SNAKE_START_LEN;
    for (uint16_t i = 0; i < s->len; i++) {
        s->x[i] = (uint8_t)(SNAKE_GRID / 2 - i);
        s->y[i] = SNAKE_GRID / 2;
    }
    s->dx = 1;
    s->dy = 0;
    s->queued = 0;
    s->wrap = wrap;
    s->food_x = 0;
    s->food_y = 0;
}

bool snake_core_occupied(const snake_core_t *s, uint8_t cx, uint8_t cy) {
    if (!s) return false;
    for (uint16_t i = 0; i < s->len; i++) {
        if (s->x[i] == cx && s->y[i] == cy) return true;
    }
    return false;
}

void snake_core_set_food(snake_core_t *s, uint8_t cx, uint8_t cy) {
    if (!s) return;
    s->food_x = cx;
    s->food_y = cy;
}

bool snake_core_spawn_food(snake_core_t *s, uint32_t (*rand_fn)(void *, uint32_t), void *ctx) {
    if (!s || !rand_fn) return false;
    if (s->len >= SNAKE_GRID * SNAKE_GRID) return false;

    /*
     * Rejection sampling. With 400 cells and at most 72 occupied, the expected
     * number of retries is under a fifth of one, and building a free-cell list
     * every meal would cost more than it saves. The bound is a belt-and-braces
     * guard, not an expected path.
     */
    for (int guard = 0; guard < 1000; guard++) {
        const uint8_t cx = (uint8_t)rand_fn(ctx, SNAKE_GRID);
        const uint8_t cy = (uint8_t)rand_fn(ctx, SNAKE_GRID);
        if (!snake_core_occupied(s, cx, cy)) {
            snake_core_set_food(s, cx, cy);
            return true;
        }
    }
    /* Pathologically unlucky: fall back to a linear scan rather than give up. */
    for (uint8_t cy = 0; cy < SNAKE_GRID; cy++) {
        for (uint8_t cx = 0; cx < SNAKE_GRID; cx++) {
            if (!snake_core_occupied(s, cx, cy)) {
                snake_core_set_food(s, cx, cy);
                return true;
            }
        }
    }
    return false;
}

void snake_core_turn(snake_core_t *s, int8_t dx, int8_t dy) {
    if (!s) return;
    if (dx == 0 && dy == 0) return;
    if (dx != 0 && dy != 0) return; /* diagonals are not a thing here */

    if (s->queued >= SNAKE_TURN_QUEUE) return; /* already two turns ahead */

    /*
     * Validate against the last direction that will be in effect when this turn
     * lands — the previous queued one, or the current heading if the queue is
     * empty. Refusing a reversal rather than letting it kill: the player meant
     * to turn, and killing them for a fumbled swipe is not difficulty, it is a
     * trap.
     */
    const int8_t prev_dx = s->queued ? s->queue_dx[s->queued - 1] : s->dx;
    const int8_t prev_dy = s->queued ? s->queue_dy[s->queued - 1] : s->dy;
    if (dx == -prev_dx && dy == -prev_dy) return;
    if (dx == prev_dx && dy == prev_dy) return; /* already going that way */

    s->queue_dx[s->queued] = dx;
    s->queue_dy[s->queued] = dy;
    s->queued++;
}

snake_step_t snake_core_step(snake_core_t *s) {
    if (!s) return SNAKE_DIED;

    if (s->queued > 0) {
        s->dx = s->queue_dx[0];
        s->dy = s->queue_dy[0];
        for (uint8_t i = 1; i < s->queued; i++) {
            s->queue_dx[i - 1] = s->queue_dx[i];
            s->queue_dy[i - 1] = s->queue_dy[i];
        }
        s->queued--;
    }

    int16_t nx = (int16_t)s->x[0] + s->dx;
    int16_t ny = (int16_t)s->y[0] + s->dy;

    if (s->wrap) {
        nx = (int16_t)((nx + SNAKE_GRID) % SNAKE_GRID);
        ny = (int16_t)((ny + SNAKE_GRID) % SNAKE_GRID);
    } else if (nx < 0 || ny < 0 || nx >= SNAKE_GRID || ny >= SNAKE_GRID) {
        return SNAKE_DIED;
    }

    const bool ate = (uint8_t)nx == s->food_x && (uint8_t)ny == s->food_y;

    /*
     * The tail is about to move out from under the head, so entering exactly
     * that cell is legal — unless the snake is about to grow, in which case the
     * tail stays put. Getting this wrong in either direction is the difference
     * between a game that feels fair and one that feels broken.
     */
    const uint16_t check_len = (ate && s->len < SNAKE_MAX_LEN) ? s->len : (uint16_t)(s->len - 1);
    for (uint16_t i = 0; i < check_len; i++) {
        if (s->x[i] == (uint8_t)nx && s->y[i] == (uint8_t)ny) return SNAKE_DIED;
    }

    if (ate && s->len < SNAKE_MAX_LEN) s->len++;

    for (uint16_t i = (uint16_t)(s->len - 1); i > 0; i--) {
        s->x[i] = s->x[i - 1];
        s->y[i] = s->y[i - 1];
    }
    s->x[0] = (uint8_t)nx;
    s->y[0] = (uint8_t)ny;

    return ate ? SNAKE_ATE : SNAKE_MOVED;
}
