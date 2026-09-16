/*
 * NEVOS simulator entry point.
 *
 * Three screens:
 *   default    the M3 shell — status bar, home grid, apps
 *   --persona  the M2 face, driven by the mood machine
 *   --boot     the M1 boot screen — wordmark, spinning arc, frame counter
 *
 * In every case what is on screen is fed by bus traffic rather than by reaching
 * into a service, so the running simulator demonstrates the event bus and not
 * only the test suite does.
 *
 *   build/host/nevos_sim                          the shell
 *   build/host/nevos_sim --app settings           straight into an app
 *   build/host/nevos_sim --persona --script       the face, scripted events
 *   build/host/nevos_sim --boot --frames 90 --shot out.ppm
 */
#include "nev_board/board_sim.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_appkit/shell.h"
#include "nev_kernel/nev_store.h"
#include "nev_persona/persona.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_time.h"
#include "nev_services/display_service.h"
#include "nev_services/input_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG          "nevos"

/* Theme tokens live in nev_appkit from M3; these are placeholders until then. */
#define COLOR_BG     lv_color_hex(0x0B0E14)
#define COLOR_INK    lv_color_hex(0xE6EDF3)
#define COLOR_MUTED  lv_color_hex(0x6E7681)
#define COLOR_ACCENT lv_color_hex(0x4C8DFF)

static lv_obj_t *s_counter_label;
static lv_obj_t *s_arc;
static lv_obj_t *s_mood_label;

/* ------------------------------------------------------------- boot screen */

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

static void apply_frame_stats(const nev_p_frame_t *f) {
    /*
     * Only the boot screen owns this label. Guarding here rather than trusting
     * every call site: FRAME_STATS is published once a second, so a caller that
     * forgets crashes about thirty frames in, which is long enough after start
     * to look like something else entirely.
     */
    if (!s_counter_label) return;

    lv_label_set_text_fmt(s_counter_label, "frame %u  " LV_SYMBOL_BULLET "  %u.%u fps",
                          (unsigned)f->frame, (unsigned)(f->fps_q4 / 16u),
                          (unsigned)((f->fps_q4 % 16u) * 10u / 16u));
    lv_obj_align(s_counter_label, LV_ALIGN_CENTER, 0, 62);
}

/* ----------------------------------------------------------- persona screen */

static void build_persona_screen(nev_mood_t start_mood) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    NEV_CHECK(nev_persona_init(screen) == NEV_OK);
    nev_persona_set_mood(start_mood, 255, 0);

    s_mood_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_mood_label, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_align(s_mood_label, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_move_foreground(s_mood_label);
}

/*
 * A scripted sequence of the events the persona reacts to. Published on the bus
 * as the real producers would, so this exercises the whole path — bus, mapping
 * table, mood machine, renderer — rather than just calling set_mood.
 */
typedef struct {
    uint32_t at_ms;
    uint16_t type;
    uint8_t source;
    uint8_t arg;
} scripted_event_t;

static const scripted_event_t kScript[] = {
    {1200, NEV_EVT_INPUT_GESTURE_TAP, NEV_SRC_INPUT, 200},
    {4200, NEV_EVT_INPUT_GESTURE_SHAKE, NEV_SRC_INPUT, 0},
    {7000, NEV_EVT_BRIDGE_AGENT_TOKEN, NEV_SRC_BRIDGE, 0},
    {10500, NEV_EVT_BRIDGE_AGENT_DONE, NEV_SRC_BRIDGE, 0},
    {12500, NEV_EVT_GAME_HIGHSCORE_BEAT, NEV_SRC_GAME, 0},
    {17000, NEV_EVT_POWER_IDLE_ENTER, NEV_SRC_POWER, 0},
    {21000, NEV_EVT_POWER_IDLE_EXIT, NEV_SRC_POWER, 0},
};

static void run_script(uint32_t now_ms, size_t *cursor) {
    while (*cursor < sizeof(kScript) / sizeof(kScript[0]) && now_ms >= kScript[*cursor].at_ms) {
        const scripted_event_t *s = &kScript[*cursor];
        nev_event_t ev = nev_event_make(s->type, s->source);
        ev.p.gesture.strength = s->arg;
        (void)nev_bus_publish(&ev);
        NEV_LOGI(TAG, "script: %s", nev_evt_name(s->type));
        (*cursor)++;
    }
}

/* ------------------------------------------------------------ input driving */

/*
 * Headless input injection, so a game can be verified and captured without a
 * person to tap the screen. It drives the board's injection API, which means
 * the events travel the real path — board, input_service, LVGL and the bus —
 * rather than calling into the game directly.
 */
/*
 * Drives a tap to start, then repeated swipes in varying directions.
 *
 * Swipes, not the single hardware button. Button A only turns left, so a driver
 * built on it can only ever spiral: the first version of this ran 900 frames
 * without eating once, and "passed" while exercising almost none of the game.
 *
 * Injection goes through the board, so the input travels the real path —
 * board, input_service, LVGL's gesture recogniser and the bus — rather than
 * calling into the game directly.
 */
#define SWIPE_FRAMES 6
#define SWIPE_GAP    22
#define SWIPE_PX     90

static void drive_play(uint32_t frame) {
    /* A tap is a press and a release on separate frames: LVGL has to see the
     * indev held and then let go before it reports a click. */
    if (frame == 10) nev_board_sim_inject_touch(240, 250, NEV_TOUCH_DOWN);
    if (frame == 12) nev_board_sim_inject_touch(240, 250, NEV_TOUCH_UP);
    if (frame < 20) return;

    static const int8_t kDir[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    static uint32_t lcg = 0x2545F491u;
    static uint8_t dir = 0;

    const uint32_t phase = (frame - 20) % (SWIPE_FRAMES + SWIPE_GAP);
    if (phase == 0) {
        lcg = lcg * 1664525u + 1013904223u;
        dir = (uint8_t)((lcg >> 24) & 3u);
        nev_board_sim_inject_touch(240, 250, NEV_TOUCH_DOWN);
    } else if (phase < SWIPE_FRAMES) {
        /* Travel far enough to clear LVGL's gesture threshold. */
        const int16_t step = (int16_t)(SWIPE_PX * (int)phase / (SWIPE_FRAMES - 1));
        nev_board_sim_inject_touch((int16_t)(240 + kDir[dir][0] * step),
                                   (int16_t)(250 + kDir[dir][1] * step), NEV_TOUCH_MOVE);
    } else if (phase == SWIPE_FRAMES) {
        nev_board_sim_inject_touch((int16_t)(240 + kDir[dir][0] * SWIPE_PX),
                                   (int16_t)(250 + kDir[dir][1] * SWIPE_PX), NEV_TOUCH_UP);
    }
}

/* --------------------------------------------------------------- arguments */

typedef struct {
    uint32_t max_frames;
    const char *shot_path;
    const char *strip_dir;
    uint32_t strip_every;
    bool persona;
    bool boot;
    bool script;
    const char *mood_name;
    const char *app_id;
    const char *settings_path;
    bool play;
    bool bridge;
    /* One scripted tap, so a screenshot can show what a button does rather
     * than only what a screen looks like before anyone touches it. */
    int32_t tap_x, tap_y;
    uint32_t tap_frame;
} options_t;

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--persona] [--boot] [--script] [--mood NAME]\n"
            "          [--app ID] [--settings PATH] [--play]\n"
            "          [--bridge] [--tap X,Y,FRAME] [--frames N] [--shot PATH.ppm]\n"
            "          [--strip DIR] [--strip-every N]\n",
            argv0);
    exit(2);
}

static options_t parse_args(int argc, char **argv) {
    options_t o = {.strip_every = 6};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--frames") && i + 1 < argc)
            o.max_frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--shot") && i + 1 < argc)
            o.shot_path = argv[++i];
        else if (!strcmp(a, "--strip") && i + 1 < argc)
            o.strip_dir = argv[++i];
        else if (!strcmp(a, "--strip-every") && i + 1 < argc)
            o.strip_every = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--mood") && i + 1 < argc)
            o.mood_name = argv[++i];
        else if (!strcmp(a, "--persona"))
            o.persona = true;
        else if (!strcmp(a, "--boot"))
            o.boot = true;
        else if (!strcmp(a, "--play"))
            o.play = true;
        else if (!strcmp(a, "--bridge"))
            o.bridge = true;
        else if (!strcmp(a, "--tap") && i + 1 < argc) {
            unsigned x = 0, y = 0, f = 0;
            if (sscanf(argv[++i], "%u,%u,%u", &x, &y, &f) != 3) usage(argv[0]);
            o.tap_x = (int32_t)x;
            o.tap_y = (int32_t)y;
            o.tap_frame = f;
        } else if (!strcmp(a, "--app") && i + 1 < argc)
            o.app_id = argv[++i];
        else if (!strcmp(a, "--settings") && i + 1 < argc)
            o.settings_path = argv[++i];
        else if (!strcmp(a, "--script")) {
            o.persona = true;
            o.script = true;
        } else
            usage(argv[0]);
    }
    if (o.strip_every == 0) o.strip_every = 1;
    return o;
}

int main(int argc, char **argv) {
    options_t opt = parse_args(argc, argv);
    nev_log_set_level(NEV_LOG_INFO);
    NEV_LOGI(TAG, "NEVOS starting");

    if (nev_bus_init() != NEV_OK) return 1;
    if (nev_blob_pool_init() != NEV_OK) return 1;
    if (nev_store_init(opt.settings_path ? opt.settings_path : "nevos-settings.txt") != NEV_OK)
        return 1;
    if (nev_board_init() != NEV_OK) return 1;
    if (display_service_init() != NEV_OK) return 1;
    if (input_service_init() != NEV_OK) return 1;

    /*
     * The simulator knows what time it is; the device does not until the daemon
     * tells it. Seeding it here means the shell and clock can be seen working
     * without pretending the device has an RTC it does not have — except with
     * --bridge, where leaving it unset is the point: the daemon sets it, and
     * that path should be exercised rather than papered over.
     */
    if (!opt.bridge) nev_wallclock_set((uint64_t)time(NULL));

    /*
     * Opt-in, because it reaches the network.
     *
     * With it, the simulator finds a real nevosd on this machine and pairs with
     * it — the same code the device runs, against the same daemon. Without it,
     * headless runs stay hermetic, which is what CI needs.
     */
    if (opt.bridge) {
        nev_bridge_init();
        NEV_LOGI(TAG, "bridge enabled; looking for a daemon");
    }

    (void)nev_store_set_num(NEV_SET_BOOT_COUNT, nev_store_num(NEV_SET_BOOT_COUNT) + 1);

    const nev_sub_cfg_t cfg = {.name = "shell",
                               .domains = NEV_DOM(DISPLAY) | NEV_DOM(SYS) | NEV_DOM(PERSONA) |
                                          NEV_DOM(GAME),
                               .depth = 8,
                               .full_policy = NEV_FULL_DROP_OLDEST,
                               .coalesce = true};
    nev_sub_t *sub = nev_bus_subscribe(&cfg);
    if (!sub) return 1;

    nev_mood_t start_mood = NEV_MOOD_IDLE;
    if (opt.mood_name && !nev_mood_from_name(opt.mood_name, &start_mood)) {
        NEV_LOGE(TAG, "unknown mood '%s'", opt.mood_name);
        return 2;
    }

    const bool shell_mode = !opt.persona && !opt.boot;
    if (opt.persona) {
        build_persona_screen(start_mood);
    } else if (shell_mode) {
        if (nev_shell_init(lv_screen_active()) != NEV_OK) return 1;
        if (opt.app_id && nev_shell_launch(opt.app_id) != NEV_OK) {
            NEV_LOGE(TAG, "no app '%s'", opt.app_id);
            return 2;
        }
    } else {
        build_boot_screen();
        start_arc_animation();
    }
    (void)nev_bus_publish_type(NEV_EVT_SYS_BOOT_DONE, NEV_SRC_KERNEL);

    size_t script_cursor = 0;
    uint32_t strip_index = 0;
    uint32_t frame_no = 0;
    bool running = true;

    while (running) {
        const uint32_t now_ms = nev_now_ms();

        if (opt.script) run_script(now_ms, &script_cursor);
        if (opt.play) drive_play(frame_no++);
        if (opt.tap_frame != 0) {
            /* Down and up on consecutive frames: LVGL needs to see both edges
             * to call it a click. */
            if (frame_no == opt.tap_frame) {
                nev_board_sim_inject_touch((int16_t)opt.tap_x, (int16_t)opt.tap_y, NEV_TOUCH_DOWN);
            } else if (frame_no == opt.tap_frame + 1) {
                nev_board_sim_inject_touch((int16_t)opt.tap_x, (int16_t)opt.tap_y, NEV_TOUCH_UP);
            }
            if (!opt.play) frame_no++;
        }
        input_service_poll(now_ms);
        /* On the device this runs on the network task, not here. In the
         * simulator there is one thread, and the bridge never blocks, so the
         * frame budget survives it. */
        if (opt.bridge) nev_bridge_poll();
        nev_store_tick(now_ms);
        if (opt.persona) nev_persona_tick(now_ms);
        if (shell_mode) nev_shell_tick(now_ms);

        running = display_service_frame();

        /* Bounded drain: a burst must never eat the frame budget. */
        nev_event_t ev;
        int budget = 8;
        while (budget-- > 0 && nev_bus_recv(sub, &ev, NEV_NO_WAIT)) {
            if (ev.type == NEV_EVT_DISPLAY_FRAME_STATS) apply_frame_stats(&ev.p.frame);
            if (ev.type == NEV_EVT_PERSONA_MOOD_CHANGED && s_mood_label) {
                lv_label_set_text(s_mood_label, nev_mood_name((nev_mood_t)ev.p.mood.mood));
                lv_obj_align(s_mood_label, LV_ALIGN_BOTTOM_MID, 0, -16);
            }
            /* Headless runs have no screen to watch, so the game's own events
             * are the only way to see whether it is being played. */
            if (ev.type == NEV_EVT_GAME_SCORE || ev.type == NEV_EVT_GAME_OVER) {
                NEV_LOGI(TAG, "%s score=%u best=%u", nev_evt_name(ev.type),
                         (unsigned)ev.p.score.score, (unsigned)ev.p.score.best);
            }
            if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
        }

        display_stats_t st;
        display_service_stats(&st);

        if (opt.strip_dir && st.frames % opt.strip_every == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/f%03u.ppm", opt.strip_dir, (unsigned)strip_index++);
            (void)nev_board_sim_save_ppm(path);
        }
        if (opt.max_frames && st.frames >= opt.max_frames) running = false;
    }

    display_stats_t st;
    display_service_stats(&st);
    NEV_LOGI(TAG, "%u frames — avg %u us, worst %u us, %u over budget", (unsigned)st.frames,
             (unsigned)st.avg_us, (unsigned)st.worst_us, (unsigned)st.overruns);

    int rc = 0;
    if (opt.shot_path && nev_board_sim_save_ppm(opt.shot_path) != NEV_OK) rc = 1;
    if (nev_board_sim_flush_count() == 0) {
        NEV_LOGE(TAG, "no pixels reached the display — the render path is broken");
        rc = 1;
    }

    if (opt.bridge) nev_bridge_stop();
    if (opt.persona) nev_persona_deinit();
    if (shell_mode) nev_shell_deinit();
    nev_store_deinit();
    input_service_deinit();
    display_service_deinit();
    nev_board_deinit();
    nev_bus_deinit();
    nev_blob_pool_deinit();
    return rc;
}
