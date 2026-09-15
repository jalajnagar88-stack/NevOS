#include "nev_port/nev_time.h"
#include <errno.h>
#include <time.h>

static uint64_t now_raw_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
}

static uint64_t s_origin_us;

uint64_t nev_now_us(void) {
    uint64_t raw = now_raw_us();
    if (s_origin_us == 0) s_origin_us = raw;
    return raw - s_origin_us;
}

uint32_t nev_now_ms(void) { return (uint32_t)(nev_now_us() / 1000u); }

void nev_sleep_ms(uint32_t ms) {
    struct timespec req = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
}

void nev_sleep_until_us(uint64_t deadline_us) {
    uint64_t now = nev_now_us();
    if (deadline_us <= now) return;
    uint64_t        delta = deadline_us - now;
    struct timespec req = {.tv_sec = (time_t)(delta / 1000000u),
                           .tv_nsec = (long)((delta % 1000000u) * 1000u)};
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
}
