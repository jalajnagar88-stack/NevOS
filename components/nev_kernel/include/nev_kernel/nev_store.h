/*
 * NEVOS L1 — settings.
 *
 * Every setting is declared once, in the table below, with its type, its
 * default and its valid range. Nothing else in NEVOS may invent a key: a
 * setting that exists only as a string literal in the app that writes it is a
 * setting nobody can find, validate, reset or migrate.
 *
 * Values live in RAM and are written back to the backing store on a debounce,
 * so dragging a brightness slider costs one flash write rather than sixty.
 *
 * Backends: NVS on the device, a key=value file on the host. Values are stored
 * as text in both, which means schema evolution is free — an unknown key on
 * load is ignored, a missing one takes its default — and a host settings file
 * can be read and edited by a person.
 */
#ifndef NEV_KERNEL_NEV_STORE_H
#define NEV_KERNEL_NEV_STORE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEV_STORE_STR_MAX 64
#define NEV_STORE_KEY_MAX 24

/*
 * X(id, type, key, default, min, max)
 *
 * Defaults are written as text so that one table can carry both numbers and
 * strings. min/max bound the value for numbers and the length for strings; a
 * write outside the range is clamped, not rejected, because a setting that
 * silently refuses to change is worse than one that saturates.
 *
 * Keys are persisted, so renaming one orphans the stored value. Add, don't rename.
 */
#define NEV_SETTING_LIST(X)                                                                        \
    /* display */                                                                                  \
    X(BRIGHTNESS, U8, "brightness", "70", 5, 100)                                                  \
    X(SLEEP_TIMEOUT_S, U16, "sleep_after_s", "120", 10, 3600)                                      \
    /* audio */                                                                                    \
    X(VOLUME, U8, "volume", "60", 0, 100)                                                          \
    X(SOUND_ENABLED, BOOL, "sound_on", "1", 0, 1)                                                  \
    /* persona */                                                                                  \
    X(PERSONA_ENERGY, U8, "persona_energy", "70", 0, 100)                                          \
    /* clock */                                                                                    \
    X(TIME_24H, BOOL, "time_24h", "1", 0, 1)                                                       \
    /* identity and pairing */                                                                     \
    X(DEVICE_NAME, STR, "device_name", "NEVOS", 1, 24)                                             \
    X(WIFI_SSID, STR, "wifi_ssid", "", 0, 32)                                                      \
    X(PAIR_TOKEN, STR, "pair_token", "", 0, 63)                                                    \
    /* game high scores, owned by the shared table in game_engine */                               \
    X(HS_SNAKE, U32, "hs_snake", "0", 0, 999999)                                                   \
    X(SNAKE_WRAP, BOOL, "snake_wrap", "0", 0, 1)                                                   \
    X(HS_BREAKOUT, U32, "hs_breakout", "0", 0, 999999)                                             \
    X(HS_RUNNER, U32, "hs_runner", "0", 0, 999999)                                                 \
    X(HS_MATCH, U32, "hs_match", "0", 0, 999999)                                                   \
    X(HS_REFLEX, U32, "hs_reflex", "0", 0, 999999)                                                 \
    /* shell */                                                                                    \
    X(LAST_APP, STR, "last_app", "", 0, 16)                                                        \
    X(BOOT_COUNT, U32, "boot_count", "0", 0, 4000000)

typedef enum {
#define NEV_X(id, type, key, def, lo, hi) NEV_SET_##id,
    NEV_SETTING_LIST(NEV_X)
#undef NEV_X
        NEV_SETTING_COUNT
} nev_setting_t;

typedef enum {
    NEV_SETTING_U8,
    NEV_SETTING_U16,
    NEV_SETTING_U32,
    NEV_SETTING_BOOL,
    NEV_SETTING_STR,
} nev_setting_type_t;

typedef struct {
    const char *key;
    const char *default_text;
    nev_setting_type_t type;
    uint32_t min;
    uint32_t max;
} nev_setting_desc_t;

const nev_setting_desc_t *nev_store_describe(nev_setting_t key);
bool nev_store_key_from_name(const char *name, nev_setting_t *out);

/*
 * `path` is a file path on the host and an NVS namespace on the device. A
 * missing or unreadable store is not an error: every setting takes its default
 * and the device boots. Refusing to start because a settings file is corrupt
 * would make a bad flash write into a brick.
 */
nev_err_t nev_store_init(const char *path);
void nev_store_deinit(void);
bool nev_store_is_ready(void);

uint32_t nev_store_num(nev_setting_t key);
const char *nev_store_str(nev_setting_t key);

/* Clamped to the declared range, then published as STORAGE.SETTING_CHANGED if
 * the value actually changed. Writing the same value is a no-op. */
nev_err_t nev_store_set_num(nev_setting_t key, uint32_t value);
nev_err_t nev_store_set_str(nev_setting_t key, const char *value);

/*
 * Write back pending changes once they have settled. Called once a second by
 * nev_sys; call nev_store_commit() directly to force one, for example before a
 * deliberate reboot.
 */
void nev_store_tick(uint32_t now_ms);
nev_err_t nev_store_commit(void);
bool nev_store_is_dirty(void);

/* Every setting back to its default, and the backing store erased. */
nev_err_t nev_store_factory_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_STORE_H */
