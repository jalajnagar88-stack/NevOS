#include "nev_port/nev_assert.h"
#include "esp_system.h"
#include <stdio.h>

void nev_panic(const char *file, int line, const char *expr) {
    /*
     * abort() rather than esp_restart(): abort triggers the panic handler,
     * which writes a core dump and a backtrace. Restarting silently would
     * discard exactly the evidence needed to understand why.
     */
    printf("\n*** NEVOS PANIC ***\n  %s:%d\n  assertion failed: %s\n", file, line, expr);
    fflush(stdout);
    abort();
}
