/*
 * NEVOS L0 — simulator board.
 *
 * Owns the framebuffer and the injected input state; defers windowing to
 * whichever backend was compiled in (SDL2 or headless). The two backends exist
 * so that the same binary logic can either be looked at by a person or asserted
 * on by CI.
 */
#include "nev_board/board_sim.h"
#include "sim_backend.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_sync.h"
#include <stdio.h>
#include <string.h>

#define TAG "board"

static uint16_t *s_fb;
static bool s_ready;
static nev_mutex_t s_lock;

static nev_touch_t s_touch;
static bool s_touch_active;
static uint8_t s_buttons;
static nev_imu_sample_t s_imu = {.accel_g = {0.0f, 0.0f, 1.0f}};
static bool s_quit;
static uint8_t s_backlight = 100;

static uint32_t s_flush_count;
static uint32_t s_pixels_written;

nev_err_t nev_board_init(void) {
    if (s_ready) return NEV_OK;
    NEV_TRY(nev_mutex_init(&s_lock));

    /* Allocated from the simulated PSRAM budget, exactly as on the device. */
    s_fb = nev_malloc(NEV_DISPLAY_FB_BYTES, NEV_MEM_PSRAM);
    if (!s_fb) {
        nev_mutex_deinit(&s_lock);
        return NEV_ERR_NO_MEM;
    }
    memset(s_fb, 0, NEV_DISPLAY_FB_BYTES);

    nev_err_t rc = sim_backend_init(s_fb, NEV_DISPLAY_WIDTH, NEV_DISPLAY_HEIGHT);
    if (rc != NEV_OK) {
        nev_free(s_fb);
        s_fb = NULL;
        nev_mutex_deinit(&s_lock);
        return rc;
    }

    s_ready = true;
    NEV_LOGI(TAG, "%s %dx%d RGB565, %s backend, framebuffer %u KB", NEV_BOARD_NAME,
             NEV_DISPLAY_WIDTH, NEV_DISPLAY_HEIGHT,
             sim_backend_kind() == NEV_SIM_DISPLAY_SDL2 ? "SDL2" : "headless",
             (unsigned)(NEV_DISPLAY_FB_BYTES / 1024));
    return NEV_OK;
}

void nev_board_deinit(void) {
    if (!s_ready) return;
    sim_backend_deinit();
    nev_free(s_fb);
    s_fb = NULL;
    nev_mutex_deinit(&s_lock);
    s_ready = false;
    s_flush_count = 0;
    s_pixels_written = 0;
    s_quit = false;
}

const char *nev_board_name(void) {
    return NEV_BOARD_NAME " (simulator)";
}

void nev_board_display_flush(const nev_rect_t *area, const uint16_t *pixels) {
    if (!s_ready || !area || !pixels) return;

    /* Clip rather than trust: a renderer bug should not corrupt memory. */
    int32_t x1 = NEV_MAX(area->x1, 0);
    int32_t y1 = NEV_MAX(area->y1, 0);
    int32_t x2 = NEV_MIN(area->x2, NEV_DISPLAY_WIDTH - 1);
    int32_t y2 = NEV_MIN(area->y2, NEV_DISPLAY_HEIGHT - 1);
    if (x2 < x1 || y2 < y1) return;

    const int32_t src_stride = nev_rect_w(area);
    const int32_t copy_w = x2 - x1 + 1;

    for (int32_t y = y1; y <= y2; y++) {
        const uint16_t *src = pixels + (size_t)(y - area->y1) * src_stride + (x1 - area->x1);
        uint16_t *dst = s_fb + (size_t)y * NEV_DISPLAY_WIDTH + x1;
        memcpy(dst, src, (size_t)copy_w * sizeof(uint16_t));
    }

    s_flush_count++;
    s_pixels_written += (uint32_t)(copy_w * (y2 - y1 + 1));
}

void nev_board_backlight_set(uint8_t percent) {
    s_backlight = percent > 100 ? 100 : percent;
}

bool nev_board_touch_read(nev_touch_t *out) {
    if (!out) return false;
    nev_mutex_lock(&s_lock);
    bool active = s_touch_active;
    if (active) *out = s_touch;
    nev_mutex_unlock(&s_lock);
    return active;
}

uint8_t nev_board_buttons_read(void) {
    return s_buttons;
}

bool nev_board_imu_read(nev_imu_sample_t *out) {
    if (!out) return false;
    nev_mutex_lock(&s_lock);
    *out = s_imu;
    nev_mutex_unlock(&s_lock);
    return true;
}

void nev_board_power_state(nev_power_state_t *out) {
    if (!out) return;
    /* The simulator is always on mains with a full pack; power_service gets its
     * interesting cases from injection, not from a fake discharge curve. */
    out->millivolts = NEV_BATT_FULL_MV;
    out->percent = 100;
    out->charging = true;
    out->battery_present = true;
}

void nev_board_tick(void) {
    if (!s_ready) return;
    sim_backend_poll();
    sim_backend_present();
}

bool nev_board_quit_requested(void) {
    return s_quit;
}

/* ------------------------------------------------------------- sim-only API */

nev_sim_display_kind_t nev_board_sim_display_kind(void) {
    return sim_backend_kind();
}

void nev_board_sim_inject_touch(int16_t x, int16_t y, nev_touch_action_t action) {
    nev_mutex_lock(&s_lock);
    s_touch.x = x;
    s_touch.y = y;
    s_touch.action = (uint8_t)action;
    s_touch.finger = 0;
    s_touch_active = (action != NEV_TOUCH_NONE && action != NEV_TOUCH_UP);
    nev_mutex_unlock(&s_lock);
}

void nev_board_sim_inject_buttons(uint8_t mask) {
    s_buttons = mask;
}

void nev_board_sim_inject_imu(const nev_imu_sample_t *sample) {
    if (!sample) return;
    nev_mutex_lock(&s_lock);
    s_imu = *sample;
    nev_mutex_unlock(&s_lock);
}

void nev_board_sim_request_quit(void) {
    s_quit = true;
}

uint32_t nev_board_sim_flush_count(void) {
    return s_flush_count;
}
uint32_t nev_board_sim_pixels_written(void) {
    return s_pixels_written;
}

nev_err_t nev_board_sim_save_ppm(const char *path) {
    if (!s_ready || !path) return NEV_ERR_INVALID_ARG;

    FILE *f = fopen(path, "wb");
    if (!f) return NEV_ERR_NOT_FOUND;
    fprintf(f, "P6\n%d %d\n255\n", NEV_DISPLAY_WIDTH, NEV_DISPLAY_HEIGHT);

    /* RGB565 -> RGB888, replicating high bits into the low ones so that full
     * scale maps to 255 rather than 248. */
    for (int i = 0; i < NEV_DISPLAY_WIDTH * NEV_DISPLAY_HEIGHT; i++) {
        uint16_t px = s_fb[i];
        uint8_t r = (uint8_t)((px >> 11) & 0x1F);
        uint8_t g = (uint8_t)((px >> 5) & 0x3F);
        uint8_t b = (uint8_t)(px & 0x1F);
        uint8_t rgb[3] = {(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                          (uint8_t)((b << 3) | (b >> 2))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    NEV_LOGI(TAG, "framebuffer saved to %s", path);
    return NEV_OK;
}
