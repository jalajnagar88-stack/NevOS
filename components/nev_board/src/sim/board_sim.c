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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "nev_port/nev_time.h"

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

/*
 * Mains and a full pack by default, which is what a simulator on a laptop
 * actually is. `nev_board_sim_set_battery` overrides it, because the cases
 * worth looking at — dimming all the way to dark, the low-battery timeout, the
 * moment a charger is pulled out — cannot happen on a device that is always
 * plugged in, and a fake discharge curve would put them on a timer nobody wants
 * to wait for.
 */
static uint8_t s_batt_percent = 100;
static bool s_batt_charging = true;

void nev_board_sim_set_battery(uint8_t percent, bool charging) {
    s_batt_percent = percent > 100 ? 100 : percent;
    s_batt_charging = charging;
}

void nev_board_power_state(nev_power_state_t *out) {
    if (!out) return;
    out->percent = s_batt_percent;
    /* A crude but monotonic map from percent to a plausible cell voltage, so
     * anything reading millivolts sees a number that moves the right way. */
    out->millivolts = (uint16_t)(NEV_BATT_EMPTY_MV +
                                 (NEV_BATT_FULL_MV - NEV_BATT_EMPTY_MV) * s_batt_percent / 100);
    out->charging = s_batt_charging;
    out->battery_present = true;
}

/* ------------------------------------------------------------------- Wi-Fi */

/*
 * The simulator is already on a network — it is a program on a laptop — so
 * "connecting" is bookkeeping. It still goes through ASSOCIATED for one call,
 * because a state machine that only ever sees the happy path in testing is a
 * state machine whose unhappy paths are untested.
 */
static nev_wifi_status_t s_wifi = NEV_WIFI_DOWN;
static int s_wifi_polls;

nev_err_t nev_board_wifi_connect(const char *ssid, const char *password) {
    (void)password;
    NEV_LOGI("board", "wifi: pretending to join '%s'", ssid ? ssid : "");
    s_wifi = NEV_WIFI_ASSOCIATED;
    s_wifi_polls = 0;
    return NEV_OK;
}

void nev_board_wifi_disconnect(void) {
    s_wifi = NEV_WIFI_DOWN;
}

nev_wifi_status_t nev_board_wifi_status(void) {
    if (s_wifi == NEV_WIFI_ASSOCIATED && ++s_wifi_polls > 1) s_wifi = NEV_WIFI_ONLINE;
    return s_wifi;
}

/* ------------------------------------------------------------------ audio */

/*
 * The simulator has no microphone, so it produces silence — but at the right
 * rate.
 *
 * Rate is the part that matters. Everything above this reads "how much audio
 * has arrived" and paces itself by it: the chunker, the wire, the daemon's
 * segment timer. Handing back an unlimited supply of samples would let a
 * one-second meeting transcribe an hour, and handing back none would make the
 * whole path untestable without hardware.
 *
 * Silence rather than a tone because the mock transcriber ignores the samples
 * entirely, and a buzzing simulator would be a thing to switch off.
 */
static bool s_audio_running;
static uint64_t s_audio_started_us;
static uint64_t s_audio_delivered;

nev_err_t nev_board_audio_start(void) {
    s_audio_running = true;
    s_audio_started_us = nev_now_us();
    s_audio_delivered = 0;
    return NEV_OK;
}

void nev_board_audio_stop(void) {
    s_audio_running = false;
}

bool nev_board_audio_is_running(void) {
    return s_audio_running;
}

size_t nev_board_audio_read(int16_t *out, size_t max_samples) {
    if (!s_audio_running || !out || max_samples == 0) return 0;

    const uint64_t elapsed_us = nev_now_us() - s_audio_started_us;
    const uint64_t owed = elapsed_us * NEV_AUDIO_SAMPLE_RATE / 1000000u;
    if (owed <= s_audio_delivered) return 0;

    size_t n = (size_t)(owed - s_audio_delivered);
    if (n > max_samples) n = max_samples;
    memset(out, 0, n * sizeof(int16_t));
    s_audio_delivered += n;
    return n;
}

/*
 * Playback on the simulator writes a WAV instead of making a noise.
 *
 * A build machine has no speaker and CI has no ears, so the useful thing to do
 * with generated audio is make it inspectable: NEVOS_SIM_AUDIO_OUT names a file
 * and every sample the system plays lands in it, in order. That turns "does the
 * timer make a sound when it finishes" into a file you can listen to once and a
 * length you can assert on.
 */
static FILE *s_wav;
static uint32_t s_wav_samples;

static void wav_write_header(FILE *f, uint32_t samples) {
    const uint32_t data_len = samples * 2u;
    const uint32_t rate = NEV_AUDIO_SAMPLE_RATE;
    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4);
    const uint32_t riff = 36u + data_len;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    const uint32_t fmt_len = 16, byte_rate = rate * 2u;
    const uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
    memcpy(h + 16, &fmt_len, 4);
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_len, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), f);
}

nev_err_t nev_board_audio_out_start(void) {
    if (s_wav) return NEV_OK;
    const char *path = getenv("NEVOS_SIM_AUDIO_OUT");
    if (!path) return NEV_OK; /* silence, and nothing to write */

    s_wav = fopen(path, "wb");
    if (!s_wav) {
        NEV_LOGW("board", "could not open %s for audio", path);
        return NEV_OK; /* not fatal: sound is never load-bearing */
    }
    s_wav_samples = 0;
    wav_write_header(s_wav, 0);
    NEV_LOGI("board", "audio out -> %s", path);
    return NEV_OK;
}

void nev_board_audio_out_stop(void) {
    if (!s_wav) return;
    /* The header carries the length, which is only known now. */
    wav_write_header(s_wav, s_wav_samples);
    fclose(s_wav);
    s_wav = NULL;
}

size_t nev_board_audio_out_write(const int16_t *samples, size_t count) {
    if (!samples || count == 0) return 0;
    if (s_wav) {
        fwrite(samples, sizeof(int16_t), count, s_wav);
        s_wav_samples += (uint32_t)count;
    }
    /* Accepts everything: there is no real driver queue to fill, and a
     * simulator that applied backpressure would be inventing a constraint the
     * hardware has not yet told us about. */
    return count;
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
