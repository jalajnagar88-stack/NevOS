/*
 * NEVOS simulator entry point.
 *
 * This is the M1 boot screen: a wordmark, a live frame counter, and a spinning
 * arc. The counter is deliberately NOT read from display_service directly — it
 * arrives as a DISPLAY.FRAME_STATS event over the bus, so that the thing on
 * screen proves the bus is carrying traffic in the running system and not only
 * under test.
 *
 * Run:
 *   build/host/nevos_sim                      window, until closed
 *   build/host/nevos_sim --frames 90 --shot out.ppm
 */
#include "nev_board/board_sim.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_time.h"
#include "nev_services/display_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG          "nevos"

/* Theme tokens live in nev_appkit from M3; these are placeholders until then. */
#define COLOR_BG     lv_color_hex(0x0B0E14)
#define COLOR_INK    lv_color_hex(0xE6EDF3)
#define COLOR_MUTED  lv_color_hex(0x6E7681)
#define COLOR_ACCENT lv_color_hex(0x4C8DFF)

static lv_obj_t *s_counter_label;
static lv_obj_t *s_arc;

static void build_boot_screen(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_arc = lv_arc_create(screen);
    lv_obj_set_size(s_arc, 300, 300);
    lv_obj_center(s_arc);
    lv_arc_set_rotation(s_arc, 0);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_angles(s_arc, 0, 60);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x1C2128), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "NEVOS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_INK, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 6, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "M1 " LV_SYMBOL_BULLET " kernel + event bus");
    lv_obj_set_style_text_color(subtitle, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 14);

    s_counter_label = lv_label_create(screen);
    lv_label_set_text(s_counter_label, "frame 0");
    lv_obj_set_style_text_font(s_counter_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_counter_label, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_align(s_counter_label, LV_ALIGN_CENTER, 0, 62);

    lv_obj_t *board = lv_label_create(screen);
    lv_label_set_text_fmt(board, "%s", nev_board_name());
    lv_obj_set_style_text_color(board, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_align(board, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void arc_spin_cb(void *obj, int32_t value) {
    lv_arc_set_angles(obj, value, value + 60);
}

static void start_arc_animation(void) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arc);
    lv_anim_set_exec_cb(&a, arc_spin_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 2400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* The screen is driven by bus traffic, not by reaching into display_service. */
static void apply_frame_stats(const nev_p_frame_t *f) {
    lv_label_set_text_fmt(s_counter_label, "frame %u  " LV_SYMBOL_BULLET "  %u.%u fps",
                          (unsigned)f->frame, (unsigned)(f->fps_q4 / 16u),
                          (unsigned)((f->fps_q4 % 16u) * 10u / 16u));
    lv_obj_align(s_counter_label, LV_ALIGN_CENTER, 0, 62);
}

typedef struct {
    uint32_t max_frames; /* 0 = run until the window closes */
    const char *shot_path;
} options_t;

static options_t parse_args(int argc, char **argv) {
    options_t o = {0};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            o.max_frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            o.shot_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--frames N] [--shot PATH.ppm]\n", argv[0]);
            exit(2);
        }
    }
    return o;
}

int main(int argc, char **argv) {
    options_t opt = parse_args(argc, argv);
    nev_log_set_level(NEV_LOG_INFO);
    NEV_LOGI(TAG, "NEVOS starting");

    if (nev_bus_init() != NEV_OK) return 1;
    if (nev_blob_pool_init() != NEV_OK) return 1;
    if (nev_board_init() != NEV_OK) return 1;
    if (display_service_init() != NEV_OK) return 1;

    nev_sub_cfg_t cfg = {.name = "boot",
                         .domains = NEV_DOM(DISPLAY) | NEV_DOM(SYS),
                         .depth = 8,
                         .full_policy = NEV_FULL_DROP_OLDEST,
                         .coalesce = true};
    nev_sub_t *sub = nev_bus_subscribe(&cfg);
    if (!sub) return 1;

    build_boot_screen();
    start_arc_animation();
    (void)nev_bus_publish_type(NEV_EVT_SYS_BOOT_DONE, NEV_SRC_KERNEL);

    bool running = true;
    while (running) {
        running = display_service_frame();

        /* Bounded drain: a burst must never eat the frame budget. */
        nev_event_t ev;
        int budget = 8;
        while (budget-- > 0 && nev_bus_recv(sub, &ev, NEV_NO_WAIT)) {
            if (ev.type == NEV_EVT_DISPLAY_FRAME_STATS) apply_frame_stats(&ev.p.frame);
            if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
        }

        display_stats_t st;
        display_service_stats(&st);
        if (opt.max_frames && st.frames >= opt.max_frames) running = false;
    }

    display_stats_t st;
    display_service_stats(&st);
    NEV_LOGI(TAG, "%u frames — avg %u us, worst %u us, %u over budget", (unsigned)st.frames,
             (unsigned)st.avg_us, (unsigned)st.worst_us, (unsigned)st.overruns);
    NEV_LOGI(TAG, "%u flushes, %u pixels written", (unsigned)nev_board_sim_flush_count(),
             (unsigned)nev_board_sim_pixels_written());

    int rc = 0;
    if (opt.shot_path && nev_board_sim_save_ppm(opt.shot_path) != NEV_OK) {
        NEV_LOGE(TAG, "could not write %s", opt.shot_path);
        rc = 1;
    }

    /* A run that drew nothing is a failure even if it exited cleanly. */
    if (nev_board_sim_flush_count() == 0) {
        NEV_LOGE(TAG, "no pixels reached the display — the render path is broken");
        rc = 1;
    }

    display_service_deinit();
    nev_board_deinit();
    nev_bus_deinit();
    nev_blob_pool_deinit();
    return rc;
}
