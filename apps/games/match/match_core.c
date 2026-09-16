#include "match_core.h"
#include <string.h>

static uint8_t roll_colour(match_rand_t rand_fn, void *ctx) {
    return (uint8_t)(rand_fn ? rand_fn(ctx, MT_COLORS) : 0);
}

/* Marks every tile that is part of a run of MT_RUN or more. */
static uint8_t mark_matches(const match_core_t *m, bool mask[MT_SIZE][MT_SIZE]) {
    memset(mask, 0, sizeof(bool) * MT_SIZE * MT_SIZE);
    uint8_t count = 0;

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE;) {
            const uint8_t v = m->tile[r][c];
            uint8_t run = 1;
            while (c + run < MT_SIZE && m->tile[r][c + run] == v && v != MT_EMPTY)
                run++;
            if (v != MT_EMPTY && run >= MT_RUN) {
                for (uint8_t k = 0; k < run; k++)
                    mask[r][c + k] = true;
            }
            c = (uint8_t)(c + run);
        }
    }
    for (uint8_t c = 0; c < MT_SIZE; c++) {
        for (uint8_t r = 0; r < MT_SIZE;) {
            const uint8_t v = m->tile[r][c];
            uint8_t run = 1;
            while (r + run < MT_SIZE && m->tile[r + run][c] == v && v != MT_EMPTY)
                run++;
            if (v != MT_EMPTY && run >= MT_RUN) {
                for (uint8_t k = 0; k < run; k++)
                    mask[r + k][c] = true;
            }
            r = (uint8_t)(r + run);
        }
    }

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            if (mask[r][c]) count++;
        }
    }
    return count;
}

bool match_core_has_match(const match_core_t *m) {
    if (!m) return false;
    bool mask[MT_SIZE][MT_SIZE];
    return mark_matches(m, mask) > 0;
}

bool match_core_adjacent(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) {
    if (r1 >= MT_SIZE || c1 >= MT_SIZE || r2 >= MT_SIZE || c2 >= MT_SIZE) return false;
    const int dr = (int)r1 - (int)r2;
    const int dc = (int)c1 - (int)c2;
    return (dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1));
}

static void swap_cells(match_core_t *m, uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) {
    const uint8_t t = m->tile[r1][c1];
    m->tile[r1][c1] = m->tile[r2][c2];
    m->tile[r2][c2] = t;
}

bool match_core_swap(match_core_t *m, uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) {
    if (!m || !match_core_adjacent(r1, c1, r2, c2)) return false;

    swap_cells(m, r1, c1, r2, c2);
    if (match_core_has_match(m)) return true;

    /* Refused, not undone-a-moment-later: the board never shows a state the
     * player did not earn. */
    swap_cells(m, r1, c1, r2, c2);
    return false;
}

bool match_core_has_move(const match_core_t *m) {
    if (!m) return false;
    match_core_t probe = *m;

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            if (c + 1 < MT_SIZE) {
                swap_cells(&probe, r, c, r, (uint8_t)(c + 1));
                const bool hit = match_core_has_match(&probe);
                swap_cells(&probe, r, c, r, (uint8_t)(c + 1));
                if (hit) return true;
            }
            if (r + 1 < MT_SIZE) {
                swap_cells(&probe, r, c, (uint8_t)(r + 1), c);
                const bool hit = match_core_has_match(&probe);
                swap_cells(&probe, r, c, (uint8_t)(r + 1), c);
                if (hit) return true;
            }
        }
    }
    return false;
}

/* Drops everything above a hole and refills the top. */
static void collapse(match_core_t *m, match_rand_t rand_fn, void *ctx) {
    for (uint8_t c = 0; c < MT_SIZE; c++) {
        int write = MT_SIZE - 1;
        for (int r = MT_SIZE - 1; r >= 0; r--) {
            if (m->tile[r][c] != MT_EMPTY) m->tile[write--][c] = m->tile[r][c];
        }
        while (write >= 0)
            m->tile[write--][c] = roll_colour(rand_fn, ctx);
    }
}

static void reshuffle(match_core_t *m, match_rand_t rand_fn, void *ctx) {
    /*
     * A dead board is a bug the player would have to sit through, so it is
     * refilled rather than presented. Bounded: with six colours a random board
     * almost always has a move, and the bound only guards against a generator
     * that has stopped producing variety.
     */
    for (int attempt = 0; attempt < 64; attempt++) {
        for (uint8_t r = 0; r < MT_SIZE; r++) {
            for (uint8_t c = 0; c < MT_SIZE; c++)
                m->tile[r][c] = roll_colour(rand_fn, ctx);
        }
        if (!match_core_has_match(m) && match_core_has_move(m)) return;
    }
}

void match_core_fill(match_core_t *m, match_rand_t rand_fn, void *ctx) {
    if (!m) return;

    /*
     * Build it tile by tile, rejecting any colour that would complete a run.
     * Generating at random and re-rolling the whole board would work too, but
     * this cannot fail and needs no retry bound.
     */
    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            for (int attempt = 0; attempt < 16; attempt++) {
                const uint8_t v = roll_colour(rand_fn, ctx);
                const bool h_run = c >= 2 && m->tile[r][c - 1] == v && m->tile[r][c - 2] == v;
                const bool v_run = r >= 2 && m->tile[r - 1][c] == v && m->tile[r - 2][c] == v;
                m->tile[r][c] = v;
                if (!h_run && !v_run) break;
            }
        }
    }
    if (!match_core_has_move(m)) reshuffle(m, rand_fn, ctx);
}

bool match_core_resolve(match_core_t *m, uint8_t cascade, match_rand_t rand_fn, void *ctx,
                        match_resolve_t *out) {
    if (!m || !out) return false;
    memset(out, 0, sizeof(*out));
    out->cascade = cascade ? cascade : 1;

    out->cleared = mark_matches(m, out->mask);
    if (out->cleared == 0) {
        /* Nothing to clear, but the board may still be dead after the previous
         * pass, so this is where playability is re-established. */
        if (!match_core_has_move(m)) {
            reshuffle(m, rand_fn, ctx);
            out->reshuffled = true;
        }
        return false;
    }

    for (uint8_t r = 0; r < MT_SIZE; r++) {
        for (uint8_t c = 0; c < MT_SIZE; c++) {
            if (out->mask[r][c]) m->tile[r][c] = MT_EMPTY;
        }
    }
    collapse(m, rand_fn, ctx);

    out->more = match_core_has_match(m);
    if (!out->more && !match_core_has_move(m)) {
        reshuffle(m, rand_fn, ctx);
        out->reshuffled = true;
    }
    return true;
}

uint32_t match_core_score_for(uint8_t cleared, uint8_t cascade) {
    if (cascade == 0) cascade = 1;
    return (uint32_t)cleared * 10u * cascade;
}
