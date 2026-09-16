/*
 * Match — the rules, with no LVGL.
 *
 * Original tile-matching puzzle: swap adjacent tiles to line up three or more,
 * cleared tiles fall and refill, and the refill can line up more — a cascade,
 * worth progressively more.
 *
 * Three invariants a tile-matcher has to hold or it stops being a puzzle:
 *   - the opening board contains no already-made matches
 *   - a swap that makes nothing is refused and reverted
 *   - the board always has at least one legal move, or it is reshuffled
 * All three are tested.
 */
#ifndef MATCH_CORE_H
#define MATCH_CORE_H

#include "nev_port/nev_types.h"

#define MT_SIZE          7
#define MT_COLORS        6
#define MT_EMPTY         0xFF
#define MT_RUN           3 /* tiles in a line to score */

#define MT_ROUND_SECONDS 90

typedef struct {
    uint8_t tile[MT_SIZE][MT_SIZE];
} match_core_t;

typedef struct {
    uint8_t cleared;             /* tiles removed this pass        */
    uint8_t cascade;             /* 1 for the first pass, then up  */
    bool mask[MT_SIZE][MT_SIZE]; /* what was removed, for effects  */
    bool more;                   /* another pass is pending        */
    bool reshuffled;             /* the board had no legal move    */
} match_resolve_t;

typedef uint32_t (*match_rand_t)(void *ctx, uint32_t upper);

/* Fills a board with no existing matches and at least one legal move. */
void match_core_fill(match_core_t *m, match_rand_t rand_fn, void *ctx);

/* True when the two cells are orthogonally adjacent and both on the board. */
bool match_core_adjacent(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2);

/*
 * Swaps only if the result contains a match, exactly as the player expects: a
 * swap that achieves nothing is refused rather than silently undone a moment
 * later. Returns false and leaves the board untouched otherwise.
 */
bool match_core_swap(match_core_t *m, uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2);

bool match_core_has_match(const match_core_t *m);
bool match_core_has_move(const match_core_t *m);

/*
 * One pass: clear every match, drop what is above, refill the top. Call
 * repeatedly while out->more is set; `cascade` is the pass number, which the
 * caller turns into a scoring multiplier.
 */
bool match_core_resolve(match_core_t *m, uint8_t cascade, match_rand_t rand_fn, void *ctx,
                        match_resolve_t *out);

/* Base 10 a tile, multiplied by the cascade depth. A four-deep cascade is
 * worth far more than four separate matches, which is the point of chasing one. */
uint32_t match_core_score_for(uint8_t cleared, uint8_t cascade);

#endif /* MATCH_CORE_H */
