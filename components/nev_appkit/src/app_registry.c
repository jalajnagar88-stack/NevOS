#include "nev_appkit/app.h"
#include "nev_port/nev_log.h"
#include <string.h>

#define TAG "appkit"

#ifndef NEV_APP_MAX
#define NEV_APP_MAX 24
#endif

static const nev_app_desc_t *s_apps[NEV_APP_MAX];
static size_t s_count;

const char *nev_app_category_name(nev_app_category_t c) {
    switch (c) {
        case NEV_APP_CAT_GAME:
            return "Play";
        case NEV_APP_CAT_PRODUCTIVITY:
            return "Work";
        case NEV_APP_CAT_SYSTEM:
            return "System";
        default:
            return "?";
    }
}

/*
 * Called from constructors, so this runs before main() and before logging is
 * configured. It must therefore not depend on anything being initialised —
 * hence a plain static array and no allocation.
 */
void nev_app_register(const nev_app_desc_t *desc) {
    if (!desc || !desc->id || !desc->name || !desc->on_launch) return;
    if (s_count >= NEV_APP_MAX) return;

    /* A duplicate id would make nev_app_find ambiguous and would corrupt the
     * "last app" the shell restores. Refuse rather than shadow. */
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->id, desc->id) == 0) return;
    }
    s_apps[s_count++] = desc;
}

size_t nev_app_count(void) {
    return s_count;
}

const nev_app_desc_t *nev_app_at(size_t index) {
    return index < s_count ? s_apps[index] : NULL;
}

const nev_app_desc_t *nev_app_find(const char *id) {
    if (!id) return NULL;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) return s_apps[i];
    }
    return NULL;
}

size_t nev_app_ordered(const nev_app_desc_t **out, size_t max) {
    if (!out) return 0;
    size_t n = 0;
    /*
     * Grouped by category, and within a category in registration order.
     * Registration order is link order, which is stable for a given build, so
     * the home grid does not reshuffle itself between boots — a grid whose
     * icons move is a grid you cannot learn.
     */
    for (int cat = 0; cat < NEV_APP_CAT_COUNT && n < max; cat++) {
        for (size_t i = 0; i < s_count && n < max; i++) {
            if (s_apps[i]->category == (nev_app_category_t)cat) out[n++] = s_apps[i];
        }
    }
    return n;
}

void nev_app_registry_reset(void) {
    memset(s_apps, 0, sizeof(s_apps));
    s_count = 0;
}
