/*
 * NEVOS board definition — devkit_480 (ESP32-S3-WROOM-1-N16R8, 480x480 IPS).
 *
 * THIS IS THE ONLY FILE IN THE REPOSITORY PERMITTED TO CONTAIN A PIN NUMBER.
 * tools/ci/lint_layers.py fails the build on GPIO numbers found anywhere else.
 * Supporting another board means adding a directory here, not editing drivers.
 *
 * ---------------------------------------------------------------------------
 * PIN ASSIGNMENTS ARE UNVERIFIED. Every NEV_PIN_* below is NEV_PIN_UNASSIGNED
 * until it has been checked against the schematic of the board actually in
 * hand. They are filled in during M5 bring-up. Inventing plausible-looking
 * numbers now would produce a header that looks authoritative and is wrong,
 * which is worse than one that refuses to build. The M5 drivers static-assert
 * that the pins they use have been assigned.
 * ---------------------------------------------------------------------------
 */
#ifndef NEV_BOARD_CONFIG_H
#define NEV_BOARD_CONFIG_H

#define NEV_PIN_UNASSIGNED      (-1)
#define NEV_PIN_IS_SET(p)       ((p) >= 0)

/* ------------------------------------------------------------------ identity */
#define NEV_BOARD_NAME          "devkit_480"
#define NEV_BOARD_REVISION      1

/* ------------------------------------------------------------------- display */
#define NEV_DISPLAY_WIDTH       480
#define NEV_DISPLAY_HEIGHT      480
#define NEV_DISPLAY_BPP         16 /* RGB565 */
#define NEV_DISPLAY_HZ          60 /* panel refresh */
#define NEV_DISPLAY_TARGET_FPS  30

/*
 * LVGL renders into strips in internal SRAM and flush_cb copies them into the
 * PSRAM framebuffer the LCD peripheral scans out. See ARCHITECTURE.md §5 for
 * why full PSRAM double-buffering is not viable here.
 */
#define NEV_DISPLAY_STRIP_LINES 60
#define NEV_DISPLAY_STRIP_BYTES (NEV_DISPLAY_WIDTH * NEV_DISPLAY_STRIP_LINES * 2)
#define NEV_DISPLAY_FB_BYTES    (NEV_DISPLAY_WIDTH * NEV_DISPLAY_HEIGHT * 2)

/* RGB565 parallel interface — M5 */
#define NEV_PIN_LCD_VSYNC       NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_HSYNC       NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_DE          NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_PCLK        NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_DISP        NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_BL          NEV_PIN_UNASSIGNED
#define NEV_PIN_LCD_DATA_R                                                                         \
    {                                                                                              \
        NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED,            \
            NEV_PIN_UNASSIGNED                                                                     \
    }
#define NEV_PIN_LCD_DATA_G                                                                         \
    {                                                                                              \
        NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED,            \
            NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED                                                 \
    }
#define NEV_PIN_LCD_DATA_B                                                                         \
    {                                                                                              \
        NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED, NEV_PIN_UNASSIGNED,            \
            NEV_PIN_UNASSIGNED                                                                     \
    }

/* --------------------------------------------------------------------- touch */
/* GT911 capacitive controller over I2C */
#define NEV_TOUCH_I2C_ADDR    0x5D
#define NEV_PIN_TOUCH_INT     NEV_PIN_UNASSIGNED
#define NEV_PIN_TOUCH_RST     NEV_PIN_UNASSIGNED
#define NEV_TOUCH_MAX_POINTS  5

/* ----------------------------------------------------------------------- i2c */
#define NEV_PIN_I2C_SDA       NEV_PIN_UNASSIGNED
#define NEV_PIN_I2C_SCL       NEV_PIN_UNASSIGNED
#define NEV_I2C_FREQ_HZ       400000

/* --------------------------------------------------------------------- audio */
/* INMP441 MEMS microphone in, MAX98357A amplifier out */
#define NEV_AUDIO_SAMPLE_RATE 16000
#define NEV_AUDIO_CHANNELS    1
#define NEV_AUDIO_BITS        16
#define NEV_AUDIO_FRAME_MS    20

#define NEV_PIN_I2S_MIC_SCK   NEV_PIN_UNASSIGNED
#define NEV_PIN_I2S_MIC_WS    NEV_PIN_UNASSIGNED
#define NEV_PIN_I2S_MIC_SD    NEV_PIN_UNASSIGNED
#define NEV_PIN_I2S_AMP_BCLK  NEV_PIN_UNASSIGNED
#define NEV_PIN_I2S_AMP_LRC   NEV_PIN_UNASSIGNED
#define NEV_PIN_I2S_AMP_DIN   NEV_PIN_UNASSIGNED

/* ----------------------------------------------------------------------- imu */
/* LSM6DS3 accelerometer + gyroscope over I2C */
#define NEV_IMU_I2C_ADDR      0x6A
#define NEV_PIN_IMU_INT1      NEV_PIN_UNASSIGNED
#define NEV_IMU_SAMPLE_HZ     104

/* ------------------------------------------------------------------- buttons */
#define NEV_PIN_BTN_A         NEV_PIN_UNASSIGNED
#define NEV_PIN_BTN_B         NEV_PIN_UNASSIGNED
#define NEV_BTN_ACTIVE_LOW    1
#define NEV_BTN_DEBOUNCE_MS   20

/* --------------------------------------------------------------------- power */
#define NEV_PIN_BATT_ADC      NEV_PIN_UNASSIGNED
#define NEV_PIN_CHARGE_STAT   NEV_PIN_UNASSIGNED
#define NEV_BATT_CAPACITY_MAH 2000
#define NEV_BATT_FULL_MV      4200
#define NEV_BATT_EMPTY_MV     3300

#endif /* NEV_BOARD_CONFIG_H */
