/*
 * NEVOS L0 — board support.
 *
 * Everything the rest of NEVOS is allowed to know about hardware. One
 * implementation per target; nothing above this layer includes a driver header
 * or a pin number.
 *
 * This layer is below the event bus, so it does not publish. Services at L2
 * poll these functions and publish on the bus.
 */
#ifndef NEV_BOARD_BOARD_H
#define NEV_BOARD_BOARD_H

#include "board_config.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_TOUCH_NONE = 0,
    NEV_TOUCH_DOWN,
    NEV_TOUCH_MOVE,
    NEV_TOUCH_UP,
} nev_touch_action_t;

typedef struct {
    int16_t x, y;
    uint8_t action; /* nev_touch_action_t */
    uint8_t finger;
} nev_touch_t;

typedef struct {
    float accel_g[3];  /* x, y, z in g          */
    float gyro_dps[3]; /* x, y, z in deg/second */
} nev_imu_sample_t;

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
    bool charging;
    bool battery_present;
} nev_power_state_t;

#define NEV_BTN_A (1u << 0)
#define NEV_BTN_B (1u << 1)

nev_err_t nev_board_init(void);
void nev_board_deinit(void);
const char *nev_board_name(void);

/*
 * Copy a rendered strip to the panel. `pixels` is RGB565, row-major, exactly
 * nev_rect_w(area) * nev_rect_h(area) entries. Returns once the data has been
 * handed to the display; it does not wait for the panel to scan it out.
 */
void nev_board_display_flush(const nev_rect_t *area, const uint16_t *pixels);
void nev_board_backlight_set(uint8_t percent);

bool nev_board_touch_read(nev_touch_t *out); /* false when no touch is active */
uint8_t nev_board_buttons_read(void);        /* NEV_BTN_* bitmask, debounced  */
bool nev_board_imu_read(nev_imu_sample_t *out);
void nev_board_power_state(nev_power_state_t *out);

/*
 * Called once per frame from the render task. On the simulator this pumps the
 * window's event queue; on the device it is a no-op. Having it in the shared
 * API rather than behind an #ifdef in the main loop is what keeps the two
 * targets running literally the same top-level code.
 */
void nev_board_tick(void);
bool nev_board_quit_requested(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BOARD_BOARD_H */
