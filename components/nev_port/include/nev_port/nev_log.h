/* NEVOS L-1 — levelled logging.
 *
 * The mechanism lives here so that every layer, including the port itself,
 * has one logger. L1 (nev_kernel) installs a ring-buffer sink on top via
 * nev_log_set_sink(); the console sink is always active.
 */
#ifndef NEV_PORT_NEV_LOG_H
#define NEV_PORT_NEV_LOG_H

#include "nev_port/nev_types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_LOG_NONE = 0,
    NEV_LOG_ERROR,
    NEV_LOG_WARN,
    NEV_LOG_INFO,
    NEV_LOG_DEBUG,
    NEV_LOG_VERBOSE,
} nev_log_level_t;

#ifndef NEV_LOG_COMPILE_LEVEL
#define NEV_LOG_COMPILE_LEVEL NEV_LOG_DEBUG
#endif

/* Secondary sink; receives an already-formatted line without a trailing newline. */
typedef void (*nev_log_sink_t)(nev_log_level_t level, const char *tag, const char *line);

void            nev_log_set_level(nev_log_level_t level);
nev_log_level_t nev_log_get_level(void);
void            nev_log_set_sink(nev_log_sink_t sink);

void nev_log_write(nev_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define NEV_LOG_AT(lvl, tag, ...)                                                                  \
    do {                                                                                           \
        if ((lvl) <= NEV_LOG_COMPILE_LEVEL) nev_log_write((lvl), (tag), __VA_ARGS__);              \
    } while (0)

#define NEV_LOGE(tag, ...) NEV_LOG_AT(NEV_LOG_ERROR, tag, __VA_ARGS__)
#define NEV_LOGW(tag, ...) NEV_LOG_AT(NEV_LOG_WARN, tag, __VA_ARGS__)
#define NEV_LOGI(tag, ...) NEV_LOG_AT(NEV_LOG_INFO, tag, __VA_ARGS__)
#define NEV_LOGD(tag, ...) NEV_LOG_AT(NEV_LOG_DEBUG, tag, __VA_ARGS__)
#define NEV_LOGV(tag, ...) NEV_LOG_AT(NEV_LOG_VERBOSE, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_LOG_H */
