#include "nev_services/display_service.h"
#include "nev_board/board.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_time.h"
#include "lvgl.h"
#include <string.h>

#define TAG "display"

static lv_display_t *s_disp;
static uint8_t *s_strip[2];
static display_stats_t s_stats;
static bool s_ready;
static uint32_t s_last_report_ms;
static uint32_t s_frames_at_report;
static uint64_t s_total_us;

uint32_t display_service_frame_budget_us(void) {
    return 1000000u / NEV_DISPLAY_TARGET_FPS;
}

/* LVGL hands us a finished strip; L0 puts it on the panel. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const nev_rect_t rect = {.x1 = (int16_t)area->x1,
                             .y1 = (int16_t)area->y1,
                             .x2 = (int16_t)area->x2,
                             .y2 = (int16_t)area->y2};
    nev_board_display_flush(&rect, (const uint16_t *)(void *)px_map);
    lv_display_flush_ready(disp);
}

/* LVGL's clock must be the same monotonic clock as everything else. */
static uint32_t tick_cb(void) {
    return nev_now_ms();
}

nev_err_t display_service_init(void) {
    if (s_ready) return NEV_OK;

    lv_init();
    lv_tick_set_cb(tick_cb);

    s_disp = lv_display_create(NEV_DISPLAY_WIDTH, NEV_DISPLAY_HEIGHT);
    if (!s_disp) return NEV_ERR_NO_MEM;

    /*
     * Two strips in internal SRAM, not two full framebuffers in PSRAM. The
     * panel scans the framebuffer out of PSRAM continuously; rendering into it
     * directly puts the CPU and the LCD DMA on the same bus. See
     * ARCHITECTURE.md §5. LVGL composes into these, flush_cb copies across.
     */
    for (int i = 0; i < 2; i++) {
        s_strip[i] = nev_malloc(NEV_DISPLAY_STRIP_BYTES, NEV_MEM_INTERNAL | NEV_MEM_DMA);
        if (!s_strip[i]) {
            NEV_LOGE(TAG, "draw strip %d (%u B) did not fit in internal memory", i,
                     (unsigned)NEV_DISPLAY_STRIP_BYTES);
            display_service_deinit();
            return NEV_ERR_NO_MEM;
        }
    }

    lv_display_set_buffers(s_disp, s_strip[0], s_strip[1], NEV_DISPLAY_STRIP_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    memset(&s_stats, 0, sizeof(s_stats));
    s_total_us = 0;
    s_frames_at_report = 0;
    s_last_report_ms = nev_now_ms();
    s_ready = true;

    NEV_LOGI(TAG, "LVGL %d.%d.%d, %dx%d, 2 x %u B strips, %u fps budget = %u us",
             lv_version_major(), lv_version_minor(), lv_version_patch(), NEV_DISPLAY_WIDTH,
             NEV_DISPLAY_HEIGHT, (unsigned)NEV_DISPLAY_STRIP_BYTES, NEV_DISPLAY_TARGET_FPS,
             (unsigned)display_service_frame_budget_us());
    return NEV_OK;
}

void display_service_deinit(void) {
    for (int i = 0; i < 2; i++) {
        nev_free(s_strip[i]);
        s_strip[i] = NULL;
    }
    if (s_ready) lv_deinit();
    s_disp = NULL;
    s_ready = false;
}

static void publish_frame_stats(void) {
    nev_event_t ev = nev_event_make(NEV_EVT_DISPLAY_FRAME_STATS, NEV_SRC_DISPLAY);
    ev.p.frame.frame = s_stats.frames;
    ev.p.frame.render_us = (uint16_t)NEV_MIN(s_stats.last_us, 0xFFFFu);
    ev.p.frame.flush_us = 0;
    ev.p.frame.fps_q4 = s_stats.fps_q4;
    ev.p.frame.overruns = (uint16_t)NEV_MIN(s_stats.overruns, 0xFFFFu);
    (void)nev_bus_publish(&ev);
}

bool display_service_frame(void) {
    if (!s_ready) return false;

    const uint64_t start_us = nev_now_us();
    const uint32_t budget_us = display_service_frame_budget_us();

    lv_timer_handler();
    nev_board_tick();

    const uint32_t elapsed_us = (uint32_t)(nev_now_us() - start_us);

    s_stats.frames++;
    s_stats.last_us = elapsed_us;
    s_total_us += elapsed_us;
    s_stats.avg_us = (uint32_t)(s_total_us / s_stats.frames);
    if (elapsed_us > s_stats.worst_us) s_stats.worst_us = elapsed_us;
    if (elapsed_us > budget_us) {
        s_stats.overruns++;
        NEV_LOGD(TAG, "frame %u took %u us, over the %u us budget", (unsigned)s_stats.frames,
                 (unsigned)elapsed_us, (unsigned)budget_us);
    }

    /* Report once a second rather than once a frame: at 30 fps the latter would
     * be 30 bus events per second carrying almost no new information. */
    const uint32_t now_ms = nev_now_ms();
    if (now_ms - s_last_report_ms >= 1000u) {
        /* Rate over the interval just elapsed. Dividing the total frame count by
         * the interval length would report the average since boot climbing
         * without bound, which is how this read 60 fps while pacing at 30. */
        const uint32_t span_ms = now_ms - s_last_report_ms;
        const uint32_t frames_in_span = s_stats.frames - s_frames_at_report;
        s_stats.fps_q4 = (uint16_t)NEV_MIN(frames_in_span * 16000u / NEV_MAX(span_ms, 1u), 0xFFFFu);
        s_last_report_ms = now_ms;
        s_frames_at_report = s_stats.frames;
        publish_frame_stats();
    }

    /* Pace the frame. This is the one sanctioned sleep in the render path: it
     * gives the remaining budget back rather than spinning. */
    nev_sleep_until_us(start_us + budget_us);

    return !nev_board_quit_requested();
}

void display_service_stats(display_stats_t *out) {
    if (out) *out = s_stats;
}
