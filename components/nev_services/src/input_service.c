#include "nev_services/input_service.h"
#include "nev_board/board.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "lvgl.h"

#define TAG "input"

static lv_indev_t *s_pointer;
static uint8_t s_buttons;  /* debounced state as last published */
static uint8_t s_raw_last; /* last raw read, for debouncing     */
static uint32_t s_raw_since_ms;
static bool s_ready;

/*
 * LVGL pulls this whenever it wants the pointer. Reading the board here rather
 * than caching a polled value means LVGL and the bus never disagree about where
 * the finger is.
 */
static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    nev_touch_t t;
    if (nev_board_touch_read(&t) && t.action != NEV_TOUCH_UP) {
        data->point.x = t.x;
        data->point.y = t.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

nev_err_t input_service_init(void) {
    if (s_ready) return NEV_OK;

    s_pointer = lv_indev_create();
    if (!s_pointer) return NEV_ERR_NO_MEM;
    lv_indev_set_type(s_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_pointer, pointer_read_cb);

    s_buttons = 0;
    s_raw_last = 0;
    s_ready = true;
    NEV_LOGI(TAG, "touch and buttons ready");
    return NEV_OK;
}

void input_service_deinit(void) {
    if (!s_ready) return;
    if (s_pointer) lv_indev_delete(s_pointer);
    s_pointer = NULL;
    s_ready = false;
}

static void publish_button(uint16_t type, uint8_t id) {
    nev_event_t ev = nev_event_make(type, NEV_SRC_INPUT);
    ev.p.button.id = id;
    (void)nev_bus_publish(&ev);
}

void input_service_poll(uint32_t now_ms) {
    if (!s_ready) return;

    const uint8_t raw = nev_board_buttons_read();

    /*
     * Debounce in time, not by counting reads: the frame rate is not a
     * guaranteed sampling rate, and a contact that bounces for 20 ms should be
     * ignored for 20 ms regardless of how many frames that happens to be.
     */
    if (raw != s_raw_last) {
        s_raw_last = raw;
        s_raw_since_ms = now_ms;
        return;
    }
    if (raw == s_buttons) return;
    if ((uint32_t)(now_ms - s_raw_since_ms) < NEV_BTN_DEBOUNCE_MS) return;

    const uint8_t changed = (uint8_t)(raw ^ s_buttons);
    for (uint8_t bit = 0; bit < 2; bit++) {
        const uint8_t mask = (uint8_t)(1u << bit);
        if (!(changed & mask)) continue;
        publish_button((raw & mask) ? NEV_EVT_INPUT_BUTTON_DOWN : NEV_EVT_INPUT_BUTTON_UP, bit);
    }
    s_buttons = raw;
}
