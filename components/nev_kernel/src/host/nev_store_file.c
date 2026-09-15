/*
 * Host settings backend: a key=value text file.
 *
 * Readable and editable by a person, which matters more than it sounds — being
 * able to open the simulator's settings in an editor and see exactly what the
 * device would have stored removes a whole category of "is it persisting?"
 * guesswork.
 *
 * Writes go to a temporary file and are renamed into place, so an interrupted
 * write leaves the previous settings intact rather than a truncated file.
 */
#include "nev_kernel/nev_store_backend.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"
#include <stdio.h>
#include <string.h>

#define TAG         "store.file"

#define MAX_ENTRIES 64
#define LINE_MAX    (NEV_STORE_KEY_MAX + NEV_STORE_STR_MAX + 4)

typedef struct {
    char key[NEV_STORE_KEY_MAX];
    char value[NEV_STORE_STR_MAX];
} entry_t;

static entry_t s_entry[MAX_ENTRIES];
static int s_count;
static char s_path[512];
static bool s_open;

/*
 * Truncation here is deliberate — an over-long key or value from a hand-edited
 * file is clipped, not rejected — but snprintf's truncation is indistinguishable
 * to the compiler from an accidental overflow, so the bound is made explicit.
 */
static void copy_bounded(char *dst, size_t dst_len, const char *src) {
    size_t n = strlen(src);
    if (n >= dst_len) n = dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static entry_t *find(const char *key) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entry[i].key, key) == 0) return &s_entry[i];
    }
    return NULL;
}

static nev_err_t file_open(const char *path) {
    snprintf(s_path, sizeof(s_path), "%s", path ? path : "nevos-settings.txt");
    s_count = 0;
    s_open = true;

    FILE *f = fopen(s_path, "r");
    if (!f) {
        NEV_LOGI(TAG, "%s does not exist yet — starting from defaults", s_path);
        return NEV_OK; /* first boot is not a failure */
    }

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f) && s_count < MAX_ENTRIES) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue; /* a malformed line is skipped, not fatal */
        *eq = '\0';

        copy_bounded(s_entry[s_count].key, sizeof(s_entry[s_count].key), line);
        copy_bounded(s_entry[s_count].value, sizeof(s_entry[s_count].value), eq + 1);
        s_count++;
    }
    fclose(f);
    NEV_LOGD(TAG, "%s: %d entries", s_path, s_count);
    return NEV_OK;
}

static void file_close(void) {
    s_open = false;
    s_count = 0;
}

static nev_err_t file_load(const char *key, char *out, size_t out_len) {
    if (!s_open || !key || !out) return NEV_ERR_INVALID_ARG;
    const entry_t *e = find(key);
    if (!e) return NEV_ERR_NOT_FOUND;
    copy_bounded(out, out_len, e->value);
    return NEV_OK;
}

static nev_err_t file_save(const char *key, const char *value) {
    if (!s_open || !key || !value) return NEV_ERR_INVALID_ARG;

    entry_t *e = find(key);
    if (!e) {
        if (s_count >= MAX_ENTRIES) return NEV_ERR_NO_SPACE;
        e = &s_entry[s_count++];
        copy_bounded(e->key, sizeof(e->key), key);
    }
    copy_bounded(e->value, sizeof(e->value), value);
    return NEV_OK;
}

static nev_err_t file_flush(void) {
    if (!s_open) return NEV_ERR_INVALID_STATE;

    char tmp[sizeof(s_path) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        NEV_LOGE(TAG, "cannot write %s", tmp);
        return NEV_ERR_NOT_FOUND;
    }
    fprintf(f, "# NEVOS settings. Generated; edits are read back on next start.\n");
    for (int i = 0; i < s_count; i++)
        fprintf(f, "%s=%s\n", s_entry[i].key, s_entry[i].value);

    if (fflush(f) != 0 || fclose(f) != 0) {
        remove(tmp);
        return NEV_ERR_NO_SPACE;
    }
    /* Atomic: an interrupted write leaves the old file untouched. */
    if (rename(tmp, s_path) != 0) {
        remove(tmp);
        return NEV_ERR_NO_SPACE;
    }
    return NEV_OK;
}

static nev_err_t file_erase_all(void) {
    s_count = 0;
    remove(s_path);
    return NEV_OK;
}

static const nev_store_backend_t kBackend = {
    .open = file_open,
    .close = file_close,
    .load = file_load,
    .save = file_save,
    .flush = file_flush,
    .erase_all = file_erase_all,
};

const nev_store_backend_t *nev_store_backend(void) {
    return &kBackend;
}
