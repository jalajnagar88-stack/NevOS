/*
 * Device settings backend: ESP-IDF NVS.
 *
 * Values are stored as strings, matching the host backend, so both targets
 * survive schema changes the same way: an unknown key on load is ignored and a
 * missing one takes its default.
 *
 * NVS writes are buffered until nvs_commit, which is what makes nev_store's
 * write-back debounce worth having — a dragged slider becomes one flash write.
 */
#include "nev_kernel/nev_store_backend.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define TAG "store.nvs"

static nvs_handle_t s_handle;
static bool s_open;
static char s_namespace[16];

static nev_err_t from_esp(esp_err_t e) {
    switch (e) {
        case ESP_OK:
            return NEV_OK;
        case ESP_ERR_NVS_NOT_FOUND:
            return NEV_ERR_NOT_FOUND;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        case ESP_ERR_NVS_PAGE_FULL:
            return NEV_ERR_NO_SPACE;
        case ESP_ERR_NVS_INVALID_HANDLE:
        case ESP_ERR_NVS_INVALID_STATE:
            return NEV_ERR_INVALID_STATE;
        default:
            return NEV_ERR_INVALID_ARG;
    }
}

static nev_err_t nvs_open_backend(const char *path) {
    snprintf(s_namespace, sizeof(s_namespace), "%s", path && *path ? path : "nevos");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* A partition that cannot be mounted is erased and remade rather than
         * left to fail every boot: settings are recoverable, a boot loop is not. */
        NEV_LOGW(TAG, "NVS partition unusable, reformatting");
        if (nvs_flash_erase() == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return from_esp(err);

    err = nvs_open(s_namespace, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) return from_esp(err);

    s_open = true;
    return NEV_OK;
}

static void nvs_close_backend(void) {
    if (!s_open) return;
    nvs_close(s_handle);
    s_open = false;
}

static nev_err_t nvs_load(const char *key, char *out, size_t out_len) {
    if (!s_open || !key || !out) return NEV_ERR_INVALID_ARG;
    size_t len = out_len;
    return from_esp(nvs_get_str(s_handle, key, out, &len));
}

static nev_err_t nvs_save(const char *key, const char *value) {
    if (!s_open || !key || !value) return NEV_ERR_INVALID_ARG;
    return from_esp(nvs_set_str(s_handle, key, value));
}

static nev_err_t nvs_flush_backend(void) {
    if (!s_open) return NEV_ERR_INVALID_STATE;
    return from_esp(nvs_commit(s_handle));
}

static nev_err_t nvs_erase_backend(void) {
    if (!s_open) return NEV_ERR_INVALID_STATE;
    nev_err_t rc = from_esp(nvs_erase_all(s_handle));
    if (rc == NEV_OK) rc = from_esp(nvs_commit(s_handle));
    return rc;
}

static const nev_store_backend_t kBackend = {
    .open = nvs_open_backend,
    .close = nvs_close_backend,
    .load = nvs_load,
    .save = nvs_save,
    .flush = nvs_flush_backend,
    .erase_all = nvs_erase_backend,
};

const nev_store_backend_t *nev_store_backend(void) {
    return &kBackend;
}
