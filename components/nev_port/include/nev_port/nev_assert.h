/* NEVOS L-1 — assertions.
 *
 * NEV_ASSERT is compiled out in release builds. NEV_CHECK never is: use it for
 * invariants whose violation would corrupt state rather than merely misbehave.
 */
#ifndef NEV_PORT_NEV_ASSERT_H
#define NEV_PORT_NEV_ASSERT_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void nev_panic(const char *file, int line, const char *expr) __attribute__((noreturn));

#define NEV_CHECK(expr)                                                                            \
    do {                                                                                           \
        if (!(expr)) nev_panic(__FILE__, __LINE__, #expr);                                         \
    } while (0)

#ifdef NDEBUG
#define NEV_ASSERT(expr) ((void)0)
#else
#define NEV_ASSERT(expr) NEV_CHECK(expr)
#endif

/* Guard for a pointer argument; returns err instead of crashing. */
#define NEV_REQUIRE(expr, err)                                                                     \
    do {                                                                                           \
        if (!(expr)) return (err);                                                                 \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_ASSERT_H */
