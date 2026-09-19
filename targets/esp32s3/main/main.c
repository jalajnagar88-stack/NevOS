/*
 * NEVOS firmware entry point (ESP32-S3).
 *
 * The same operating system the simulator runs, on FreeRTOS. Everything above
 * nev_board is shared source: the shell, the thirteen apps, the persona, the
 * services and the bridge are compiled from the same files, and the only thing
 * that differs is which board implementation is linked underneath them.
 *
 * That is the claim the project is built on, and until this file included them
 * it was only a claim. The firmware was an M1 skeleton — bus, blob pool,
 * heartbeat — so "the same application code compiles for both targets" had
 * never been tested by compiling it for both targets.
 *
 * Three tasks, matching ARCHITECTURE.md §4:
 *
 *   nev_ui    core 1, renders and runs the apps. Nothing else is allowed on
 *             this core's critical path, which is why the radio is not here.
 *             It does not tick the persona, for the same reason the simulator's
 *             shell mode does not: the face has no home on the home screen yet.
 *             Apps set moods and the mood machine records them; nothing draws
 *             them outside `nevos_sim --persona`. Deciding where the face
 *             belongs — idle screen, screensaver, a strip above the grid — is a
 *             design question, not an oversight to paper over here.
 *   nev_net   core 0, drives the bridge. It does socket work and must never
 *             be able to make the UI wait for a packet.
 *   nev_sys   core 0, once a second: heap, bus overflows, stale blobs.
 *
 * The board is still a stub, so on real hardware this boots, runs, renders
 * into nothing and reads a touch panel that always says "not touched". That is
 * the expected state until a board is chosen, and it is the point: every line
 * above nev_board is now compiled for Xtensa on every push.
 */
#include "nev_appkit/shell.h"
#include "nev_board/board.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_task.h"
#include "nev_port/nev_time.h"
#include "nev_services/audio_service.h"
#include "nev_services/display_service.h"
#include "nev_services/input_service.h"
#include "nev_services/net_service.h"
#include "nev_services/ota_service.h"
#include "nev_services/power_service.h"
#include "nev_services/sound_service.h"
#include "nev_services/sys_service.h"

#include "lvgl.h"

#define TAG             "nevos"

/* NVS namespace rather than a file path; nev_store knows the difference. */
#define STORE_NAMESPACE "nevos"

static nev_sub_t *s_sys_sub;

/* ------------------------------------------------------------------ nev_sys */

static void sys_task(void *arg) {
    (void)arg;
    for (;;) {
        (void)nev_bus_publish_type(NEV_EVT_SYS_TICK_1S, NEV_SRC_KERNEL);
        nev_bus_report_overflows();
        /*
         * A blob still held two seconds after it was published is a subscriber
         * that forgot to release it. Reported rather than freed: freeing it
         * would hide the leak and corrupt whoever still has the pointer.
         */
        nev_blob_check_stale(2000);

        nev_event_t ev = nev_event_make(NEV_EVT_SYS_HEAP_STATS, NEV_SRC_KERNEL);
        ev.p.heap.internal_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_INTERNAL);
        ev.p.heap.psram_free = (uint32_t)nev_mem_free_bytes(NEV_MEM_PSRAM);
        (void)nev_bus_publish(&ev);

        nev_sleep_ms(1000);
    }
}

/* ------------------------------------------------------------------ nev_net */

/*
 * The radio and the link, on the core that does not render.
 *
 * nev_bridge_poll never blocks, so this could in principle live on the UI task
 * the way it does in the simulator. It does not, because "never blocks" is a
 * property of our code and not of lwIP underneath it: a DNS lookup or a
 * retransmit on the render task is a dropped frame, and the whole point of the
 * two cores is that the face never stutters because a packet was slow.
 */
static void net_task(void *arg) {
    (void)arg;
    for (;;) {
        const uint32_t now_ms = nev_now_ms();
        net_service_tick(now_ms);
        ota_service_tick(now_ms);
        nev_bridge_poll();
        /* 20 ms is far more often than a radio changes state and far less
         * often than a frame, so it costs nothing and adds no latency the
         * user can see. */
        nev_sleep_ms(20);
    }
}

/* ------------------------------------------------------------------- nev_ui */

static void ui_task(void *arg) {
    (void)arg;

    /*
     * LVGL, the services that touch it, and the shell are all created here
     * rather than in app_main, because LVGL is not thread-safe and everything
     * that builds a widget has to run on the task that draws them.
     */
    NEV_CHECK(display_service_init() == NEV_OK);
    NEV_CHECK(input_service_init() == NEV_OK);
    NEV_CHECK(audio_service_init() == NEV_OK);
    NEV_CHECK(sound_service_init() == NEV_OK);
    NEV_CHECK(power_service_init(nev_now_ms()) == NEV_OK);
    NEV_CHECK(nev_shell_init(lv_screen_active()) == NEV_OK);

    (void)nev_bus_publish_type(NEV_EVT_SYS_BOOT_DONE, NEV_SRC_KERNEL);

    for (;;) {
        const uint32_t now_ms = nev_now_ms();

        input_service_poll(now_ms);
        power_service_tick(now_ms);
        audio_service_poll(now_ms);
        sound_service_tick(now_ms);
        sys_service_tick(now_ms);
        nev_shell_tick(now_ms);

        /*
         * Returns false when the display asks to stop, which on a device is
         * never — there is no window to close. Paced by the display service
         * itself, which is where the frame budget lives.
         */
        if (!display_service_frame()) break;
    }

    NEV_LOGE(TAG, "the render loop stopped, which should not happen");
    for (;;)
        nev_sleep_ms(1000);
}

/* ---------------------------------------------------------------- app_main */

void app_main(void) {
    NEV_LOGI(TAG, "NEVOS booting on %s", nev_board_name());

    NEV_CHECK(nev_bus_init() == NEV_OK);
    NEV_CHECK(nev_blob_pool_init() == NEV_OK);

    /*
     * A store that will not open is not fatal: every setting takes its default
     * and the device boots. Refusing to start because NVS is corrupt would
     * turn a bad flash write into a brick.
     */
    if (nev_store_init(STORE_NAMESPACE) != NEV_OK) {
        NEV_LOGW(TAG, "settings unavailable; using defaults");
    }

    /* The board has no pins assigned until one is chosen; that is expected,
     * not fatal, and the layers above it are what this build is proving. */
    nev_err_t board_rc = nev_board_init();
    if (board_rc != NEV_OK) {
        NEV_LOGW(TAG, "board init: %s — the screen will be dark", nev_err_str(board_rc));
    }

    NEV_CHECK(sys_service_init(nev_now_ms()) == NEV_OK);
    NEV_CHECK(net_service_init(nev_now_ms()) == NEV_OK);
    NEV_CHECK(ota_service_init() == NEV_OK);
    nev_bridge_init();

    (void)nev_store_set_num(NEV_SET_BOOT_COUNT, nev_store_num(NEV_SET_BOOT_COUNT) + 1);

    nev_sub_cfg_t cfg = {
        .name = "main", .domains = NEV_DOM(SYS), .depth = 8, .full_policy = NEV_FULL_DROP_OLDEST};
    s_sys_sub = nev_bus_subscribe(&cfg);
    NEV_CHECK(s_sys_sub != NULL);

    static nev_task_t s_ui, s_net, s_sys;

    /* 12 KB: LVGL's layout and draw recursion is the deepest stack in the
     * system, and an app's on_launch builds a widget tree on top of it. */
    nev_task_cfg_t ui_cfg = {.name = "nev_ui",
                             .fn = ui_task,
                             .stack_bytes = 12288,
                             .priority = NEV_PRIO_UI,
                             .core = NEV_CORE_UI};
    NEV_CHECK(nev_task_create(&s_ui, &ui_cfg) == NEV_OK);

    nev_task_cfg_t net_cfg = {.name = "nev_net",
                              .fn = net_task,
                              .stack_bytes = 8192,
                              .priority = NEV_PRIO_NET,
                              .core = NEV_CORE_IO};
    NEV_CHECK(nev_task_create(&s_net, &net_cfg) == NEV_OK);

    nev_task_cfg_t sys_cfg = {.name = "nev_sys",
                              .fn = sys_task,
                              .stack_bytes = 4096,
                              .priority = NEV_PRIO_LOW,
                              .core = NEV_CORE_IO};
    NEV_CHECK(nev_task_create(&s_sys, &sys_cfg) == NEV_OK);

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
