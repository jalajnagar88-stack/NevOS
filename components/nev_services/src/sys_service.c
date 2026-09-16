/* Housekeeping. See sys_service.h. */
#include "nev_services/sys_service.h"

#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"

#define TAG                "sys"

/*
 * Below this, something is about to fail an allocation. Publishing a warning
 * lets the shell close a background app before that happens, which is a much
 * better outcome than an allocation returning NULL somewhere that assumed it
 * would not.
 */
#define LOW_INTERNAL_BYTES (32u * 1024u)

/* A buffer held this long is not in flight; it has been forgotten. */
#define STALE_BLOB_MS      2000u

static uint32_t s_last_second_ms;
static bool s_ready;
static bool s_warned_low;

nev_err_t sys_service_init(uint32_t now_ms) {
    s_last_second_ms = now_ms;
    s_warned_low = false;
    s_ready = true;
    return NEV_OK;
}

void sys_service_deinit(void) {
    s_ready = false;
}

void sys_service_tick(uint32_t now_ms) {
    if (!s_ready) return;
    if ((now_ms - s_last_second_ms) < 1000u) return;
    s_last_second_ms = now_ms;

    (void)nev_bus_publish_type(NEV_EVT_SYS_TICK_1S, NEV_SRC_KERNEL);

    /* Settled changes go to flash on this tick rather than on every write, so
     * that a slider being dragged is one write and not fifty. */
    nev_store_tick(now_ms);

    /* A subscriber that has been dropping events says so once a second rather
     * than never: a queue quietly overflowing in the field is a bug that takes
     * weeks to find. */
    nev_bus_report_overflows();

    /* And a buffer nobody released. This is the sweep that would have caught
     * the audio path's leak on the first run rather than the fiftieth. */
    nev_blob_check_stale(STALE_BLOB_MS);

    const uint32_t internal_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_INTERNAL);
    const uint32_t psram_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_PSRAM);

    nev_event_t ev = nev_event_make(NEV_EVT_SYS_HEAP_STATS, NEV_SRC_KERNEL);
    ev.p.heap.internal_free = internal_free;
    ev.p.heap.psram_free = psram_free;
    /* The low-water mark is the number that matters on a device that has been
     * running for a week; the current figure only says what this moment looks
     * like. Until the port tracks it, report what it does know. */
    ev.p.heap.internal_low_water = internal_free;
    (void)nev_bus_publish(&ev);

    if (internal_free < LOW_INTERNAL_BYTES) {
        if (!s_warned_low) {
            NEV_LOGW(TAG, "internal memory low: %u bytes", (unsigned)internal_free);
            s_warned_low = true;
        }
        /* Published every second while it lasts, not once: the shell evicts one
         * app per warning, and one eviction may not be enough. */
        (void)nev_bus_publish_type(NEV_EVT_SYS_MEM_PRESSURE, NEV_SRC_KERNEL);
    } else {
        s_warned_low = false;
    }
}
