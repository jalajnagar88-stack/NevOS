/* The parts of randomness that are the same everywhere. */
#include "nev_port/nev_rand.h"

#include <string.h>

uint32_t nev_rand_u32(void) {
    uint32_t v = 0;
    nev_rand_fill((uint8_t *)&v, sizeof(v));
    return v;
}

uint32_t nev_rand_below(uint32_t bound) {
    if (bound == 0) return 0;

    /*
     * Rejection sampling rather than a modulo.
     *
     * The pairing code is six digits — a million values out of 2^32 — and a
     * plain modulo would make the lowest 967,296 codes very slightly likelier
     * than the rest. That bias is far too small to matter against a five-guess
     * lockout, but this is the function the next secret will also be drawn
     * from, and a biased draw is the kind of thing that gets copied.
     *
     * The loop terminates with probability 1 and, for any bound, expects fewer
     * than two iterations.
     */
    uint32_t limit = UINT32_MAX - (UINT32_MAX % bound) - 1;
    uint32_t v;
    do {
        v = nev_rand_u32();
    } while (v > limit);
    return v % bound;
}
