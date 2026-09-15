/*
 * NEVOS firmware entry point (ESP32-S3).
 *
 * M1 proves the two layers that must be right before anything else can be:
 * nev_port on FreeRTOS, and the event bus and blob pool on real silicon with
 * real PSRAM. The display, touch, audio and radio stacks arrive in M5.
 */
#include "nev_board/board.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_task.h"
#include "nev_port/nev_time.h"

#define TAG "nevos"

static nev_sub_t *s_sys_sub;

static void heartbeat_task(void *arg) {
    (void)arg;
    for (;;) {
        (void)nev_bus_publish_type(NEV_EVT_SYS_TICK_1S, NEV_SRC_KERNEL);
        nev_bus_report_overflows();
        nev_blob_check_stale(2000);

        nev_event_t ev = nev_event_make(NEV_EVT_SYS_HEAP_STATS, NEV_SRC_KERNEL);
        ev.p.heap.internal_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_INTERNAL);
        ev.p.heap.psram_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_PSRAM);
        (void)nev_bus_publish(&ev);

        nev_sleep_ms(1000);
    }
}

void app_main(void) {
    NEV_LOGI(TAG, "NEVOS booting on %s", nev_board_name());

    NEV_CHECK(nev_bus_init() == NEV_OK);
    NEV_CHECK(nev_blob_pool_init() == NEV_OK);

    /* The board has no pins assigned until M5; that is expected, not fatal. */
    nev_err_t board_rc = nev_board_init();
    if (board_rc != NEV_OK) {
        NEV_LOGW(TAG, "board init: %s — continuing with kernel only", nev_err_str(board_rc));
    }

    nev_sub_cfg_t cfg = {
        .name = "main", .domains = NEV_DOM(SYS), .depth = 8, .full_policy = NEV_FULL_DROP_OLDEST};
    s_sys_sub = nev_bus_subscribe(&cfg);
    NEV_CHECK(s_sys_sub != NULL);

    nev_task_t hb;
    nev_task_cfg_t hb_cfg = {.name = "nev_sys",
                             .fn = heartbeat_task,
                             .stack_bytes = 4096,
                             .priority = NEV_PRIO_LOW,
                             .core = NEV_CORE_IO};
    NEV_CHECK(nev_task_create(&hb, &hb_cfg) == NEV_OK);

    (void)nev_bus_publish_type(NEV_EVT_SYS_BOOT_DONE, NEV_SRC_KERNEL);
    NEV_LOGI(TAG, "internal %u KB free, PSRAM %u KB free",
             (unsigned)(nev_mem_free_bytes(NEV_MEM_INTERNAL) / 1024),
             (unsigned)(nev_mem_free_bytes(NEV_MEM_PSRAM) / 1024));

    nev_event_t ev;
    for (;;) {
        if (!nev_bus_recv(s_sys_sub, &ev, NEV_WAIT_FOREVER)) continue;
        if (ev.type == NEV_EVT_SYS_HEAP_STATS) {
            NEV_LOGI(TAG, "heap: internal %u KB, PSRAM %u KB",
                     (unsigned)(ev.p.heap.internal_free / 1024),
                     (unsigned)(ev.p.heap.psram_free / 1024));
        }
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
}
