/*
 * NEVOS L4 — the app contract.
 *
 * An app is a folder with a descriptor. Adding one creates a directory and
 * touches nothing else: no central list to edit, no registration to forget, no
 * merge conflict between two people adding apps in the same week.
 *
 * See docs/adding-an-app.md — one page, start to finish.
 */
#ifndef NEV_APPKIT_APP_H
#define NEV_APPKIT_APP_H

#include "nev_kernel/nev_event.h"
#include "nev_port/nev_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEV_APP_ID_MAX 16

typedef enum {
    NEV_APP_CAT_GAME = 0,
    NEV_APP_CAT_PRODUCTIVITY,
    NEV_APP_CAT_SYSTEM,
    NEV_APP_CAT_COUNT
} nev_app_category_t;

const char *nev_app_category_name(nev_app_category_t c);

typedef struct {
    /* Stable and persisted (the shell remembers the last app), so renaming one
     * orphans that. Lowercase, no spaces. */
    const char *id;
    const char *name;
    const char *icon; /* an LV_SYMBOL_* string */
    nev_app_category_t category;

    /* Declared, not measured. The lifecycle manager uses it to decide what to
     * evict under pressure; an app that lies here will be the one that gets
     * closed at the wrong moment. */
    uint16_t memory_budget_kb;

    /* ADR 0008: most of NEVOS works without the daemon. An app that genuinely
     * cannot is marked here, and the shell says so on the home grid rather than
     * letting it launch and fail. */
    bool requires_bridge;

    /*
     * Build your UI under `root`, which is a full-size container the shell owns.
     * Do not touch the screen or the status bar.
     */
    nev_err_t (*on_launch)(lv_obj_t *root);

    /* Foreground lost. Stop timers and release anything expensive; your objects
     * survive, so on_resume does not have to rebuild them. */
    void (*on_suspend)(void);
    void (*on_resume)(void);

    /* Objects under `root` are destroyed for you after this returns. Release
     * only what the shell cannot see: blobs, files, bus subscriptions. */
    void (*on_close)(void);

    /* Delivered only while in the foreground. Runs on the render task, so it
     * must return in under 5 ms (ARCHITECTURE.md §4). */
    void (*on_event)(const nev_event_t *ev);
    void (*on_tick)(uint32_t now_ms);
} nev_app_desc_t;

/*
 * Registration.
 *
 * A constructor rather than a linker section. The section trick
 * (__start_SECNAME / __stop_SECNAME) is ELF-only and does not work on Mach-O,
 * and the primary development machine for the simulator is a Mac. A constructor
 * is portable across both, works identically on ESP-IDF, and still means no
 * central list.
 */
void nev_app_register(const nev_app_desc_t *desc);

#define NEV_APP_REGISTER(desc_symbol)                                                              \
    __attribute__((constructor)) static void nev_app_autoreg_##desc_symbol(void) {                 \
        nev_app_register(&desc_symbol);                                                            \
    }

#define NEV_APP_MAX_ORDERED 24

size_t nev_app_count(void);
const nev_app_desc_t *nev_app_at(size_t index);
const nev_app_desc_t *nev_app_find(const char *id);

/* Registered apps in the order the home grid should show them: system last,
 * and stable within a category so the grid does not reshuffle between boots. */
size_t nev_app_ordered(const nev_app_desc_t **out, size_t max);

/* Tests only: forget every registration. Constructors have already run by the
 * time main() starts, so a test that needs a known set clears first. */
void nev_app_registry_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_APPKIT_APP_H */
