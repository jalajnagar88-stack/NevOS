#include "nev_port/nev_assert.h"
#include <stdio.h>
#include <stdlib.h>

void nev_panic(const char *file, int line, const char *expr) {
    fflush(stdout);
    fprintf(stderr, "\n*** NEVOS PANIC ***\n  %s:%d\n  assertion failed: %s\n", file, line, expr);
    fflush(stderr);
    abort();
}
