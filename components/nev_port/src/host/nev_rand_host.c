/* Host randomness: /dev/urandom. */
#include "nev_port/nev_rand.h"

#include <stdio.h>
#include <stdlib.h>

#include "nev_port/nev_assert.h"

void nev_rand_fill(uint8_t *out, size_t len) {
    if (!out || len == 0) return;

    /* Opened per call rather than kept open: this is called a few times a
     * minute at most, and a cached descriptor would be one more thing to get
     * wrong across a fork in the simulator. */
    FILE *f = fopen("/dev/urandom", "rb");
    /* No fallback to a clock-seeded PRNG. Silently degrading the source of a
     * pairing code is exactly the failure this should not have, and a host
     * without /dev/urandom is a host with bigger problems. */
    NEV_CHECK(f != NULL);
    size_t got = fread(out, 1, len, f);
    fclose(f);
    NEV_CHECK(got == len);
}
