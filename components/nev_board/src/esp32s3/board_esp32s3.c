/*
 * NEVOS L0 — ESP32-S3 board support.
 *
 * M1 SCOPE: this target brings up the port layer and the kernel on real
 * silicon. The peripherals below land in M5, when the hardware is in hand and
 * boards/<name>/board_config.h can be filled in from an actual schematic.
 *
 * nev_board_init deliberately fails rather than pretending: every pin in
 * board_config.h is still NEV_PIN_UNASSIGNED, and a board layer that silently
 * returns NEV_OK while driving nothing is worse than one that says so.
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

void nev_board_tick(void) { /* nothing to pump on the device */
}

bool nev_board_quit_requested(void) {
    return false; /* the device does not exit */
}
