/*
 * NEVOS — clock.
 *
 * Ambient mode: large time, date, and a slow second ring. Deliberately quiet —
 * this is what the device shows when nobody is using it, so it has to be
 * readable from across a desk and boring enough to ignore.
 */
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_time.h"
#include <stdio.h>

static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_ring;
static lv_obj_t *s_hint;
static uint32_t s_painted_second = 0xFFFFFFFFu;

/* Days since the Unix epoch to a civil date. Howard Hinnant's algorithm,
 * shifted to a March-based year so leap days land at the end. No libc time
 * functions: newlib's are large, and this is exact and allocation-free. */
static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint64_t doe = (uint64_t)(z - era * 146097);
    const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yr = (int64_t)yoe + era * 400;
    const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint64_t mp = (5 * doy + 2) / 153;
    *d = (unsigned)(doy - (153 * mp + 2) / 5 + 1);
    *m = (unsigned)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yr + (*m <= 2));
}

static void repaint(void) {
    if (!nev_wallclock_is_set()) {
        lv_label_set_text(s_time, "--:--");
        lv_label_set_text(s_date, "");
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        nev_ui_progress_ring_set(s_ring, 0);
        return;
    }
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    const uint64_t now = nev_wallclock_now();
    const uint32_t sec_of_day = (uint32_t)(now % 86400u);
    const uint32_t h = sec_of_day / 3600u;
    const uint32_t m = (sec_of_day / 60u) % 60u;
    const uint32_t s = sec_of_day % 60u;

    if (nev_store_is_ready() && !nev_store_num(NEV_SET_TIME_24H)) {
        uint32_t h12 = h % 12;
        if (h12 == 0) h12 = 12;
        lv_label_set_text_fmt(s_time, "%u:%02u", (unsigned)h12, (unsigned)m);
    } else {
        lv_label_set_text_fmt(s_time, "%02u:%02u", (unsigned)h, (unsigned)m);
    }

    static const char *kMonth[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int year;
    unsigned month, day;
    civil_from_days((int64_t)(now / 86400u), &year, &month, &day);
    if (month >= 1 && month <= 12) {
        lv_label_set_text_fmt(s_date, "%u %s %d", day, kMonth[month - 1], year);
    }

    /* A minute ring rather than a second hand: it moves visibly but never
     * demands attention, which is the whole brief for ambient mode. */
    nev_ui_progress_ring_set(s_ring, (int32_t)((m * 60u + s) * 100u / 3600u));
}

static nev_err_t clock_launch(lv_obj_t *root) {
    s_ring = nev_ui_progress_ring(root, 320);
    lv_obj_center(s_ring);
    lv_obj_set_style_arc_width(s_ring, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 4, LV_PART_INDICATOR);

    s_time = nev_ui_label(root, "--:--", NEV_FONT_DISPLAY, NEV_COL_INK);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -18);

    s_date = nev_ui_label(root, "", NEV_FONT_BODY, NEV_COL_INK_MUTED);
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, 30);

    s_hint = nev_ui_label(root, "Waiting for the time", NEV_FONT_BODY, NEV_COL_INK_FAINT);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 30);

    s_painted_second = 0xFFFFFFFFu;
    repaint();
    return NEV_OK;
}

static void clock_tick(uint32_t now_ms) {
    (void)now_ms;
    /* Repaint once a second, not once a frame: at 30 fps that is 29 redraws of
     * an unchanged string, and every one of them dirties a rectangle. */
    const uint32_t sec = (uint32_t)nev_wallclock_now();
    if (sec == s_painted_second) return;
    s_painted_second = sec;
    repaint();
}

static void clock_close(void) {
    s_time = s_date = s_ring = s_hint = NULL;
}

static const nev_app_desc_t kClockApp = {
    .id = "clock",
    .name = "Clock",
    .icon = LV_SYMBOL_BELL,
    .category = NEV_APP_CAT_SYSTEM,
    .memory_budget_kb = 12,
    .requires_bridge = false,
    .on_launch = clock_launch,
    .on_tick = clock_tick,
    .on_close = clock_close,
};

NEV_APP_REGISTER(kClockApp);
