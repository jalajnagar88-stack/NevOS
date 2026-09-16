#include "nev_services/input_service.h"
#include "nev_board/board.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "lvgl.h"

#define TAG "input"

static lv_indev_t *s_pointer;
static nev_touch_t s_last_touch;
static bool s_touch_down;
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
    s_touch_down = false;
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

/*
 * Touch reaches widgets through LVGL's own pull, but INPUT.TOUCH also has to
 * exist on the bus: the persona reacts to taps and the games read gestures, and
 * neither of them owns a widget tree to hang a callback on. Published on
 * transitions only — a held finger is one DOWN, not thirty a second.
 */
/* Explicit rather than a cast: if either enum is renumbered, this stops
 * compiling instead of quietly sending the wrong code. */
static uint8_t to_event_action(uint8_t board_action) {
    switch ((nev_touch_action_t)board_action) {
        case NEV_TOUCH_DOWN:
            return NEV_TOUCH_ACT_DOWN;
        case NEV_TOUCH_MOVE:
            return NEV_TOUCH_ACT_MOVE;
        case NEV_TOUCH_UP:
            return NEV_TOUCH_ACT_UP;
        case NEV_TOUCH_NONE:
            break;
    }
    return NEV_TOUCH_ACT_NONE;
}

static void publish_touch(const nev_touch_t *t) {
    nev_event_t ev = nev_event_make(NEV_EVT_INPUT_TOUCH, NEV_SRC_INPUT);
    /* nev_touch_t and nev_p_touch_t are deliberately separate types even though
     * they currently match: the event vocabulary is a contract that outlives any
     * particular driver struct, and coupling them would make a board change a
     * bus change. Copied field by field so a divergence is a compile error. */
    ev.p.touch.x = t->x;
    ev.p.touch.y = t->y;
    ev.p.touch.action = to_event_action(t->action);
    ev.p.touch.finger = t->finger;
    (void)nev_bus_publish(&ev);
}

static void poll_touch(void) {
    nev_touch_t t;
    const bool down = nev_board_touch_read(&t) && t.action != NEV_TOUCH_UP;

    if (down && !s_touch_down) {
        t.action = NEV_TOUCH_DOWN;
        publish_touch(&t);
        s_last_touch = t;
        s_touch_down = true;
    } else if (!down && s_touch_down) {
        s_last_touch.action = NEV_TOUCH_UP;
        publish_touch(&s_last_touch);
        s_touch_down = false;
    } else if (down) {
        s_last_touch = t; /* remember where the finger was, for the UP */
    }
}

void input_service_poll(uint32_t now_ms) {
    if (!s_ready) return;

    poll_touch();

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
