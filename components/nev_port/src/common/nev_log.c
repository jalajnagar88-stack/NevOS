#include "nev_port/nev_log.h"
#include "nev_port/nev_task.h"
#include "nev_port/nev_time.h"
#include <stdio.h>

#define NEV_LOG_LINE_MAX 192

static nev_log_level_t s_level = NEV_LOG_INFO;
static nev_log_sink_t  s_sink;

static const char kLevelChar[] = {'-', 'E', 'W', 'I', 'D', 'V'};

void nev_log_set_level(nev_log_level_t level) { s_level = level; }
nev_log_level_t nev_log_get_level(void) { return s_level; }
void nev_log_set_sink(nev_log_sink_t sink) { s_sink = sink; }

void nev_log_write(nev_log_level_t level, const char *tag, const char *fmt, ...) {
    if (level > s_level || level == NEV_LOG_NONE) return;

    char    line[NEV_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    /* Console sink is always on: a log you cannot see is not a log. */
    printf("%c (%8u) [%-10s] %s\n", kLevelChar[level], (unsigned)nev_now_ms(), tag, line);

    if (s_sink) s_sink(level, tag, line);
}
