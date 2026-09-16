/*
 * NEVOS — settings.
 *
 * The app that proves persistence works: every control here writes to
 * nev_store, and nev_store writes back to flash on a debounce. Nothing in this
 * file knows where the values are kept.
 */
#include "nev_appkit/app.h"
#include "nev_appkit/shell.h"
#include "nev_appkit/ui_kit.h"
#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_store.h"
#include "nev_services/audio_service.h"
#include "nev_port/nev_log.h"

#define TAG "settings"

static lv_obj_t *s_brightness_value;
static lv_obj_t *s_volume_value;

/*
 * One handler for every numeric setting. The setting id travels as the
 * widget's user data, so adding a row never means adding a callback.
 */
static void num_setting_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    const nev_setting_t key = (nev_setting_t)(uintptr_t)lv_event_get_user_data(e);
    const int32_t value = lv_slider_get_value(slider);

    (void)nev_store_set_num(key, (uint32_t)value);

    if (key == NEV_SET_BRIGHTNESS && s_brightness_value) {
        lv_label_set_text_fmt(s_brightness_value, "%d%%", (int)value);
    } else if (key == NEV_SET_VOLUME && s_volume_value) {
        lv_label_set_text_fmt(s_volume_value, "%d%%", (int)value);
    }
}

static void toggle_setting_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    const nev_setting_t key = (nev_setting_t)(uintptr_t)lv_event_get_user_data(e);
    (void)nev_store_set_num(key, lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1u : 0u);
}

static void factory_reset_confirmed(lv_event_t *e) {
    (void)e;
    NEV_LOGW(TAG, "factory reset requested from settings");

    /*
     * Stop before erasing, in this order, because a factory reset is what
     * someone does before giving the device away.
     *
     * Clearing the stored token alone was not enough: the bridge holds its copy
     * in memory and the microphone might be live, so the device would have gone
     * on recording and sending to a computer it had supposedly been unpaired
     * from until somebody power-cycled it. "Restart to apply" is a fine thing
     * to say about a brightness setting and not about this.
     */
    audio_service_stop();
    nev_bridge_forget();

    if (nev_store_factory_reset() == NEV_OK) {
        nev_ui_toast("Erased. Restart to finish.", 3000);
        /* Deliberately not restarting here: yanking the UI out from under the
         * user mid-tap is worse than telling them. The restart is theirs. */
        nev_shell_go_home();
    } else {
        nev_ui_toast("Reset failed", 2500);
    }
}

static void factory_reset_cb(lv_event_t *e) {
    (void)e;
    nev_ui_modal("Factory reset",
                 "Erases Wi-Fi, pairing and every setting. High scores go too. "
                 "This cannot be undone.",
                 "Erase", true, factory_reset_confirmed, NULL);
}

static void not_yet_cb(lv_event_t *e) {
    nev_ui_toast((const char *)lv_event_get_user_data(e), 2000);
}

static nev_err_t settings_launch(lv_obj_t *root) {
    lv_obj_t *list = nev_ui_list(root);

    lv_obj_t *b_row =
        nev_ui_slider_row(list, "Brightness", 5, 100, (int32_t)nev_store_num(NEV_SET_BRIGHTNESS),
                          num_setting_cb, (void *)(uintptr_t)NEV_SET_BRIGHTNESS);
    (void)b_row;

    nev_ui_slider_row(list, "Volume", 0, 100, (int32_t)nev_store_num(NEV_SET_VOLUME),
                      num_setting_cb, (void *)(uintptr_t)NEV_SET_VOLUME);

    nev_ui_toggle_row(list, "Sounds", nev_store_num(NEV_SET_SOUND_ENABLED) != 0, toggle_setting_cb,
                      (void *)(uintptr_t)NEV_SET_SOUND_ENABLED);

    nev_ui_slider_row(list, "Personality", 0, 100, (int32_t)nev_store_num(NEV_SET_PERSONA_ENERGY),
                      num_setting_cb, (void *)(uintptr_t)NEV_SET_PERSONA_ENERGY);

    nev_ui_toggle_row(list, "24-hour clock", nev_store_num(NEV_SET_TIME_24H) != 0,
                      toggle_setting_cb, (void *)(uintptr_t)NEV_SET_TIME_24H);

    lv_obj_t *name_row = nev_ui_row(list, "Device name");
    nev_ui_row_value(name_row, nev_store_str(NEV_SET_DEVICE_NAME));

    /* These land with the services that back them; saying so is better than a
     * row that looks live and does nothing. */
    nev_ui_action_row(list, "Wi-Fi", LV_SYMBOL_WIFI, not_yet_cb, (void *)"Wi-Fi arrives with M5");
    nev_ui_action_row(list, "Pair with computer", LV_SYMBOL_SHUFFLE, not_yet_cb,
                      (void *)"Pairing arrives with M6");
    nev_ui_action_row(list, "Check for updates", LV_SYMBOL_DOWNLOAD, not_yet_cb,
                      (void *)"Updates arrive with M5");

    lv_obj_t *reset =
        nev_ui_action_row(list, "Factory reset", LV_SYMBOL_TRASH, factory_reset_cb, NULL);
    lv_obj_set_style_bg_color(reset, NEV_COL_SURFACE, LV_PART_MAIN);
    lv_obj_t *reset_label = lv_obj_get_child(reset, 0);
    if (reset_label) lv_obj_set_style_text_color(reset_label, NEV_COL_DANGER, LV_PART_MAIN);

    return NEV_OK;
}

static void settings_close(void) {
    s_brightness_value = NULL;
    s_volume_value = NULL;
    /* Leaving settings is a natural moment to make sure nothing is pending. */
    (void)nev_store_commit();
}

static void settings_suspend(void) {
    (void)nev_store_commit();
}

static const nev_app_desc_t kSettingsApp = {
    .id = "settings",
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .category = NEV_APP_CAT_SYSTEM,
    .memory_budget_kb = 24,
    .requires_bridge = false,
    .on_launch = settings_launch,
    .on_suspend = settings_suspend,
    .on_close = settings_close,
};

NEV_APP_REGISTER(kSettingsApp);
