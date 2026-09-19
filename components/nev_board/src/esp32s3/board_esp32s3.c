/*
 * NEVOS L0 — ESP32-S3 board support.
 *
 * Every function the board contract declares, and not one of them driving a
 * peripheral yet. The drivers land when a board is chosen and
 * boards/<name>/board_config.h can be filled in from an actual schematic.
 *
 * nev_board_init deliberately fails rather than pretending: every pin in
 * board_config.h is still NEV_PIN_UNASSIGNED, and a board layer that silently
 * returns NEV_OK while driving nothing is worse than one that says so.
 *
 * WHY EVERY FUNCTION IS HERE, INCLUDING THE ONES THAT DO NOTHING:
 *
 * Ten of these were simply absent, and the whole of L2 above them referenced
 * them. Nothing complained, because a declaration in a header is enough to
 * compile — a missing definition is a link error, and the firmware had never
 * been linked. The build reached 1609 objects and then failed with ten
 * undefined references to functions this file was always supposed to have.
 *
 * So they are stubs, which is honest, rather than missing, which compiles.
 * Each one returns the answer a device with no such peripheral would give, and
 * carries the note saying what replaces it.
 */
#include "nev_board/board.h"
#include "nev_port/nev_log.h"
#include <string.h>

#define TAG "board"

static bool s_display_ready;

nev_err_t nev_board_init(void) {
    NEV_LOGI(TAG, "%s: kernel-only bring-up (M1)", NEV_BOARD_NAME);

    if (!NEV_PIN_IS_SET(NEV_PIN_LCD_PCLK) || !NEV_PIN_IS_SET(NEV_PIN_I2C_SDA)) {
        NEV_LOGW(TAG, "pins in boards/%s/board_config.h are unassigned; peripherals are offline",
                 NEV_BOARD_NAME);
        NEV_LOGW(TAG, "display, touch, audio, IMU and power arrive in M5");
        s_display_ready = false;
        return NEV_ERR_UNSUPPORTED;
    }

    /* M5: RGB LCD panel, GT911 touch, I2S in/out, LSM6DS3, battery ADC. */
    s_display_ready = true;
    return NEV_OK;
}

void nev_board_deinit(void) {
    s_display_ready = false;
}

const char *nev_board_name(void) {
    return NEV_BOARD_NAME;
}

void nev_board_display_flush(const nev_rect_t *area, const uint16_t *pixels) {
    (void)area;
    (void)pixels;
    /* M5: esp_lcd_panel_draw_bitmap into the PSRAM framebuffer. */
}

void nev_board_backlight_set(uint8_t percent) {
    (void)percent; /* M5: LEDC on NEV_PIN_LCD_BL */
}

bool nev_board_touch_read(nev_touch_t *out) {
    (void)out;
    return false; /* M5: GT911 over I2C */
}

uint8_t nev_board_buttons_read(void) {
    return 0; /* M5: debounced GPIO */
}

bool nev_board_imu_read(nev_imu_sample_t *out) {
    (void)out;
    return false; /* M5: LSM6DS3 over I2C */
}

void nev_board_power_state(nev_power_state_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->millivolts = NEV_BATT_FULL_MV;
    out->percent = 100;
    out->charging = true;
    out->battery_present = false; /* M5: fuel gauge */
}

/* ------------------------------------------------------------------- radio */

/*
 * The Wi-Fi driver, which is the one stub here that is not merely a stub.
 *
 * net_core's state machine is written against a driver that reports back
 * asynchronously, so the real implementation is esp_wifi plus an event
 * handler that calls nothing here — it feeds the same status this returns.
 * Reporting REFUSED rather than DOWN is deliberate: DOWN reads as "not
 * connected yet" and the service would retry against a radio that does not
 * exist, once a second, forever.
 */
nev_err_t nev_board_wifi_connect(const char *ssid, const char *password) {
    (void)ssid;
    (void)password;
    /* Not logged at warning level per call: net_core retries on a backoff, and
     * a device with no radio would otherwise fill its log with this. */
    return NEV_ERR_UNSUPPORTED; /* M5: esp_wifi_set_config + esp_wifi_connect */
}

void nev_board_wifi_disconnect(void) {
    /* M5: esp_wifi_disconnect */
}

nev_wifi_status_t nev_board_wifi_status(void) {
    return NEV_WIFI_REFUSED; /* M5: driven by the esp_wifi event handler */
}

/* ------------------------------------------------------------------- audio */

nev_err_t nev_board_audio_start(void) {
    return NEV_ERR_UNSUPPORTED; /* M5: I2S RX channel on the MEMS microphone */
}

void nev_board_audio_stop(void) {
    /* M5: i2s_channel_disable */
}

bool nev_board_audio_is_running(void) {
    return false;
}

size_t nev_board_audio_read(int16_t *out, size_t max_samples) {
    (void)out;
    (void)max_samples;
    /*
     * Zero, not silence. The distinction matters: the audio service treats a
     * short read as "the microphone has nothing right now" and carries on,
     * which is exactly right for a device without one. Returning a buffer of
     * zeroes would instead send real, empty audio to the daemon and have it
     * transcribe nothing, repeatedly, on the user's machine.
     */
    return 0;
}

nev_err_t nev_board_audio_out_start(void) {
    return NEV_ERR_UNSUPPORTED; /* M5: I2S TX channel on the amplifier */
}

void nev_board_audio_out_stop(void) {
    /* M5: i2s_channel_disable */
}

size_t nev_board_audio_out_write(const int16_t *samples, size_t count) {
    (void)samples;
    /*
     * Claims the samples were played. The sound service renders a cue into a
     * buffer and hands it over frame by frame; a driver that accepted nothing
     * would leave it re-rendering the same slice forever and the cue would
     * never finish. Swallowing them lets the cue run its length and end.
     */
    return count;
}

/* -------------------------------------------------------------------- misc */

void nev_board_tick(void) { /* nothing to pump on the device */
}

bool nev_board_quit_requested(void) {
    return false; /* the device does not exit */
}
