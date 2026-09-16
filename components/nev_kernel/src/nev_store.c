#include "nev_kernel/nev_store.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store_backend.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG                "store"

/* How long a value must sit unchanged before it is written back. Long enough
 * that dragging a slider is one write, short enough that a user who changes a
 * setting and immediately unplugs the device keeps the change. */
#define WRITEBACK_DELAY_MS 600u

static const nev_setting_desc_t kDesc[NEV_SETTING_COUNT] = {
#define NEV_X(id, type, key, def, lo, hi) [NEV_SET_##id] = {key, def, NEV_SETTING_##type, lo, hi},
    NEV_SETTING_LIST(NEV_X)
#undef NEV_X
};

typedef struct {
    uint32_t num;
    char str[NEV_STORE_STR_MAX];
    bool dirty;
} slot_t;

static slot_t s_slot[NEV_SETTING_COUNT];
static bool s_ready;
static bool s_dirty;
static uint32_t s_dirty_since_ms;
static const nev_store_backend_t *s_backend;

const nev_setting_desc_t *nev_store_describe(nev_setting_t key) {
    return (key >= 0 && key < NEV_SETTING_COUNT) ? &kDesc[key] : NULL;
}

bool nev_store_key_from_name(const char *name, nev_setting_t *out) {
    if (!name || !out) return false;
    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        if (strcmp(kDesc[i].key, name) == 0) {
            *out = (nev_setting_t)i;
            return true;
        }
    }
    return false;
}

static bool is_text(nev_setting_t key) {
    return kDesc[key].type == NEV_SETTING_STR;
}

static uint32_t clamp_num(nev_setting_t key, uint32_t v) {
    if (v < kDesc[key].min) return kDesc[key].min;
    if (v > kDesc[key].max) return kDesc[key].max;
    return v;
}

static void set_text_clamped(nev_setting_t key, const char *value) {
    const size_t limit =
        kDesc[key].max < NEV_STORE_STR_MAX ? kDesc[key].max : NEV_STORE_STR_MAX - 1;
    snprintf(s_slot[key].str, limit + 1, "%s", value ? value : "");
}

static void apply_default(nev_setting_t key) {
    if (is_text(key)) {
        set_text_clamped(key, kDesc[key].default_text);
    } else {
        s_slot[key].num = clamp_num(key, (uint32_t)strtoul(kDesc[key].default_text, NULL, 10));
    }
    s_slot[key].dirty = false;
}

static void publish_changed(nev_setting_t key) {
    if (!nev_bus_is_ready()) return;
    nev_event_t ev = nev_event_make(NEV_EVT_STORAGE_SETTING_CHANGED, NEV_SRC_STORE);
    ev.p.u32[0] = (uint32_t)key;
    ev.p.u32[1] = is_text(key) ? 0u : s_slot[key].num;
    (void)nev_bus_publish(&ev);
}

static void mark_dirty(nev_setting_t key) {
    s_slot[key].dirty = true;
    if (!s_dirty) s_dirty_since_ms = 0; /* set on the next tick */
    s_dirty = true;
    s_dirty_since_ms = 0;
}

nev_err_t nev_store_init(const char *path) {
    if (s_ready) return NEV_OK;
    s_backend = nev_store_backend();
    NEV_CHECK(s_backend != NULL);

    for (int i = 0; i < NEV_SETTING_COUNT; i++)
        apply_default((nev_setting_t)i);

    /*
     * A missing or unreadable store is not an error. Every setting has a
     * default, so the device boots with sane values; refusing to start because
     * a settings file is corrupt would turn a bad flash write into a brick.
     */
    nev_err_t rc = s_backend->open(path);
    if (rc != NEV_OK) {
        NEV_LOGW(TAG, "backend unavailable (%s) — running on defaults", nev_err_str(rc));
        s_ready = true;
        return NEV_OK;
    }

    int restored = 0;
    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        char text[NEV_STORE_STR_MAX];
        if (s_backend->load(kDesc[i].key, text, sizeof(text)) != NEV_OK) continue;
        if (is_text((nev_setting_t)i)) {
            set_text_clamped((nev_setting_t)i, text);
        } else {
            s_slot[i].num = clamp_num((nev_setting_t)i, (uint32_t)strtoul(text, NULL, 10));
        }
        restored++;
    }

    s_ready = true;
    s_dirty = false;
    NEV_LOGI(TAG, "%d of %d settings restored, rest on defaults", restored, NEV_SETTING_COUNT);
    return NEV_OK;
}

void nev_store_deinit(void) {
    if (!s_ready) return;
    (void)nev_store_commit();
    if (s_backend) s_backend->close();
    s_ready = false;
    s_dirty = false;
    memset(s_slot, 0, sizeof(s_slot));
}

bool nev_store_is_ready(void) {
    return s_ready;
}
bool nev_store_is_dirty(void) {
    return s_dirty;
}

uint32_t nev_store_num(nev_setting_t key) {
    if (key < 0 || key >= NEV_SETTING_COUNT) return 0;
    NEV_ASSERT(!is_text(key)); /* asked a string for its number */
    return s_slot[key].num;
}

const char *nev_store_str(nev_setting_t key) {
    if (key < 0 || key >= NEV_SETTING_COUNT) return "";
    NEV_ASSERT(is_text(key)); /* asked a number for its text */
    return s_slot[key].str;
}

nev_err_t nev_store_set_num(nev_setting_t key, uint32_t value) {
    NEV_REQUIRE(key >= 0 && key < NEV_SETTING_COUNT, NEV_ERR_INVALID_ARG);
    NEV_REQUIRE(!is_text(key), NEV_ERR_INVALID_ARG);
    if (!s_ready) return NEV_ERR_INVALID_STATE;

    const uint32_t v = clamp_num(key, value);
    if (v == s_slot[key].num) return NEV_OK; /* writing the same value is free */

    s_slot[key].num = v;
    mark_dirty(key);
    publish_changed(key);
    return NEV_OK;
}

nev_err_t nev_store_set_str(nev_setting_t key, const char *value) {
    NEV_REQUIRE(key >= 0 && key < NEV_SETTING_COUNT, NEV_ERR_INVALID_ARG);
    NEV_REQUIRE(is_text(key), NEV_ERR_INVALID_ARG);
    if (!s_ready) return NEV_ERR_INVALID_STATE;

    /*
     * Too long is refused, not truncated.
     *
     * Numbers saturate, and that is right for a brightness of 200. A string is
     * different: the first one stored here was a 64-character pairing token
     * against a 63-character limit, and truncating it produced a token that was
     * silently, permanently wrong — the device re-paired on every reconnect and
     * nothing reported an error. A value that does not fit is a bug in the
     * caller, and it should hear about it.
     */
    const char *text = value ? value : "";
    const size_t limit =
        kDesc[key].max < NEV_STORE_STR_MAX ? kDesc[key].max : NEV_STORE_STR_MAX - 1;
    if (strlen(text) > limit) return NEV_ERR_INVALID_ARG;

    char candidate[NEV_STORE_STR_MAX];
    snprintf(candidate, limit + 1, "%s", text);
    if (strcmp(candidate, s_slot[key].str) == 0) return NEV_OK;

    memcpy(s_slot[key].str, candidate,
           sizeof(candidate) < sizeof(s_slot[key].str) ? sizeof(candidate)
                                                       : sizeof(s_slot[key].str));
    s_slot[key].str[NEV_STORE_STR_MAX - 1] = '\0';
    mark_dirty(key);
    publish_changed(key);
    return NEV_OK;
}

nev_err_t nev_store_commit(void) {
    if (!s_ready || !s_dirty || !s_backend) return NEV_OK;

    char text[NEV_STORE_STR_MAX];
    nev_err_t worst = NEV_OK;
    int written = 0;

    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        if (!s_slot[i].dirty) continue;
        if (is_text((nev_setting_t)i)) {
            snprintf(text, sizeof(text), "%s", s_slot[i].str);
        } else {
            snprintf(text, sizeof(text), "%u", (unsigned)s_slot[i].num);
        }
        nev_err_t rc = s_backend->save(kDesc[i].key, text);
        if (rc != NEV_OK) {
            NEV_LOGE(TAG, "'%s' did not persist: %s", kDesc[i].key, nev_err_str(rc));
            worst = rc;
            continue; /* keep it dirty so the next commit retries */
        }
        s_slot[i].dirty = false;
        written++;
    }

    nev_err_t frc = s_backend->flush();
    if (frc != NEV_OK) worst = frc;

    if (worst == NEV_OK) {
        s_dirty = false;
        NEV_LOGD(TAG, "%d setting(s) written back", written);
    }
    return worst;
}

void nev_store_tick(uint32_t now_ms) {
    if (!s_ready || !s_dirty) return;
    if (s_dirty_since_ms == 0) {
        s_dirty_since_ms = now_ms ? now_ms : 1;
        return;
    }
    if ((uint32_t)(now_ms - s_dirty_since_ms) >= WRITEBACK_DELAY_MS) {
        s_dirty_since_ms = 0;
        (void)nev_store_commit();
    }
}

nev_err_t nev_store_factory_reset(void) {
    if (!s_ready) return NEV_ERR_INVALID_STATE;
    NEV_LOGW(TAG, "factory reset — every setting back to its default");

    for (int i = 0; i < NEV_SETTING_COUNT; i++) {
        apply_default((nev_setting_t)i);
        publish_changed((nev_setting_t)i);
    }
    s_dirty = false;
    s_dirty_since_ms = 0;

    nev_err_t rc = s_backend ? s_backend->erase_all() : NEV_OK;
    if (rc == NEV_OK && s_backend) rc = s_backend->flush();
    return rc;
}
