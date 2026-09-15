#include "nev_kernel/nev_blob.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_atomic.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_mem.h"
#include "nev_port/nev_sync.h"
#include "nev_port/nev_time.h"
#include <string.h>

#define TAG               "blob"

#define CLASS_COUNT       3
#define HANDLE_CLASS(h)   ((uint8_t)((h) >> 12))
#define HANDLE_INDEX(h)   ((uint16_t)((h) & 0x0FFFu))
#define MAKE_HANDLE(c, i) ((nev_blob_t)(((uint16_t)(c) << 12) | ((uint16_t)(i) & 0x0FFFu)))

typedef struct {
    nev_atomic_u32_t refs;
    uint32_t len;
    uint32_t alloc_ms;
} blob_desc_t;

typedef struct {
    uint32_t block_size;
    uint16_t capacity;
    uint8_t *storage;    /* capacity * block_size, one PSRAM allocation */
    blob_desc_t *desc;   /* capacity descriptors                        */
    uint16_t *free_list; /* stack of free indices                       */
    uint16_t free_count;
    uint16_t high_water;
} blob_class_t;

static blob_class_t s_class[CLASS_COUNT];
static nev_mutex_t s_lock;
static bool s_ready;
static uint32_t s_alloc_failures;

static const uint32_t kSize[CLASS_COUNT] = {NEV_BLOB_SMALL_SIZE, NEV_BLOB_MEDIUM_SIZE,
                                            NEV_BLOB_LARGE_SIZE};
static const uint16_t kCount[CLASS_COUNT] = {NEV_BLOB_SMALL_COUNT, NEV_BLOB_MEDIUM_COUNT,
                                             NEV_BLOB_LARGE_COUNT};

static bool handle_valid(nev_blob_t h) {
    if (!s_ready || h == NEV_BLOB_NONE) return false;
    uint8_t c = HANDLE_CLASS(h);
    return c < CLASS_COUNT && HANDLE_INDEX(h) < s_class[c].capacity;
}

nev_err_t nev_blob_pool_init(void) {
    if (s_ready) return NEV_OK;
    NEV_TRY(nev_mutex_init(&s_lock));

    for (int c = 0; c < CLASS_COUNT; c++) {
        blob_class_t *cl = &s_class[c];
        cl->block_size = kSize[c];
        cl->capacity = kCount[c];

        /* One allocation per class, at boot. Nothing allocates after this. */
        cl->storage = nev_malloc((size_t)cl->block_size * cl->capacity, NEV_MEM_PSRAM);
        cl->desc = nev_calloc(cl->capacity, sizeof(blob_desc_t), NEV_MEM_PSRAM);
        cl->free_list = nev_calloc(cl->capacity, sizeof(uint16_t), NEV_MEM_PSRAM);
        if (!cl->storage || !cl->desc || !cl->free_list) {
            NEV_LOGE(TAG, "pool class %d (%u x %u B) did not fit", c, cl->capacity,
                     (unsigned)cl->block_size);
            nev_blob_pool_deinit();
            return NEV_ERR_NO_MEM;
        }
        for (uint16_t i = 0; i < cl->capacity; i++)
            cl->free_list[i] = cl->capacity - 1 - i;
        cl->free_count = cl->capacity;
        cl->high_water = 0;
    }
    s_alloc_failures = 0;
    s_ready = true;

    NEV_LOGI(TAG, "pool ready: %ux%uB + %ux%uB + %ux%uB = %u KB", NEV_BLOB_SMALL_COUNT,
             NEV_BLOB_SMALL_SIZE, NEV_BLOB_MEDIUM_COUNT, NEV_BLOB_MEDIUM_SIZE, NEV_BLOB_LARGE_COUNT,
             NEV_BLOB_LARGE_SIZE,
             (unsigned)((NEV_BLOB_SMALL_SIZE * NEV_BLOB_SMALL_COUNT +
                         NEV_BLOB_MEDIUM_SIZE * NEV_BLOB_MEDIUM_COUNT +
                         NEV_BLOB_LARGE_SIZE * NEV_BLOB_LARGE_COUNT) /
                        1024u));
    return NEV_OK;
}

void nev_blob_pool_deinit(void) {
    for (int c = 0; c < CLASS_COUNT; c++) {
        blob_class_t *cl = &s_class[c];
        nev_free(cl->storage);
        nev_free(cl->desc);
        nev_free(cl->free_list);
        memset(cl, 0, sizeof(*cl));
    }
    if (s_ready) nev_mutex_deinit(&s_lock);
    s_ready = false;
}

nev_blob_t nev_blob_alloc(size_t len, uint8_t **out) {
    if (!s_ready || len == 0 || len > NEV_BLOB_MAX_LEN) {
        if (s_ready) s_alloc_failures++;
        return NEV_BLOB_NONE;
    }

    /* Smallest class that fits. Never spill upward: a 600-byte payload taking a
     * 64 KB block would starve OTA for no benefit. */
    int c = 0;
    while (c < CLASS_COUNT && len > kSize[c])
        c++;
    if (c >= CLASS_COUNT) {
        s_alloc_failures++;
        return NEV_BLOB_NONE;
    }

    nev_mutex_lock(&s_lock);
    blob_class_t *cl = &s_class[c];
    if (cl->free_count == 0) {
        s_alloc_failures++;
        nev_mutex_unlock(&s_lock);
        NEV_LOGW(TAG, "class %d exhausted (%u blocks); dropping %u B", c, cl->capacity,
                 (unsigned)len);
        return NEV_BLOB_NONE;
    }
    uint16_t idx = cl->free_list[--cl->free_count];
    uint16_t in_use = (uint16_t)(cl->capacity - cl->free_count);
    if (in_use > cl->high_water) cl->high_water = in_use;
    nev_mutex_unlock(&s_lock);

    blob_desc_t *d = &cl->desc[idx];
    nev_atomic_store(&d->refs, 1);
    d->len = (uint32_t)len;
    d->alloc_ms = nev_now_ms();

    if (out) *out = cl->storage + (size_t)idx * cl->block_size;
    return MAKE_HANDLE(c, idx);
}

uint8_t *nev_blob_data(nev_blob_t h) {
    if (!handle_valid(h)) return NULL;
    blob_class_t *cl = &s_class[HANDLE_CLASS(h)];
    return cl->storage + (size_t)HANDLE_INDEX(h) * cl->block_size;
}

size_t nev_blob_len(nev_blob_t h) {
    if (!handle_valid(h)) return 0;
    return s_class[HANDLE_CLASS(h)].desc[HANDLE_INDEX(h)].len;
}

void nev_blob_retain(nev_blob_t h) {
    if (!handle_valid(h)) return;
    blob_desc_t *d = &s_class[HANDLE_CLASS(h)].desc[HANDLE_INDEX(h)];
    NEV_ASSERT(nev_atomic_load(&d->refs) > 0); /* retain after free */
    nev_atomic_inc(&d->refs);
}

void nev_blob_release(nev_blob_t h) {
    if (!handle_valid(h)) return;
    uint8_t c = HANDLE_CLASS(h);
    uint16_t idx = HANDLE_INDEX(h);
    blob_class_t *cl = &s_class[c];
    blob_desc_t *d = &cl->desc[idx];

    NEV_ASSERT(nev_atomic_load(&d->refs) > 0); /* double release */
    if (nev_atomic_dec(&d->refs) != 0) return;

    d->len = 0;
    d->alloc_ms = 0;
    nev_mutex_lock(&s_lock);
    NEV_CHECK(cl->free_count < cl->capacity); /* freed twice into the list */
    cl->free_list[cl->free_count++] = idx;
    nev_mutex_unlock(&s_lock);
}

uint32_t nev_blob_refcount(nev_blob_t h) {
    if (!handle_valid(h)) return 0;
    return nev_atomic_load(&s_class[HANDLE_CLASS(h)].desc[HANDLE_INDEX(h)].refs);
}

void nev_blob_stats(nev_blob_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_ready) return;
    nev_mutex_lock(&s_lock);
    for (int c = 0; c < CLASS_COUNT; c++) {
        out->in_use[c] = (uint16_t)(s_class[c].capacity - s_class[c].free_count);
        out->capacity[c] = s_class[c].capacity;
        out->high_water[c] = s_class[c].high_water;
    }
    out->alloc_failures = s_alloc_failures;
    nev_mutex_unlock(&s_lock);
}

bool nev_blob_all_free(void) {
    if (!s_ready) return true;
    nev_mutex_lock(&s_lock);
    bool clean = true;
    for (int c = 0; c < CLASS_COUNT && clean; c++)
        clean = s_class[c].free_count == s_class[c].capacity;
    nev_mutex_unlock(&s_lock);
    return clean;
}

void nev_blob_check_stale(uint32_t max_age_ms) {
    if (!s_ready) return;
    uint32_t now = nev_now_ms();
    for (int c = 0; c < CLASS_COUNT; c++) {
        blob_class_t *cl = &s_class[c];
        for (uint16_t i = 0; i < cl->capacity; i++) {
            blob_desc_t *d = &cl->desc[i];
            if (nev_atomic_load(&d->refs) == 0) continue;
            if (now - d->alloc_ms > max_age_ms) {
                NEV_LOGW(TAG, "blob %u/%u held %u ms with %u refs — missing release?", c, i,
                         (unsigned)(now - d->alloc_ms), (unsigned)nev_atomic_load(&d->refs));
                d->alloc_ms = now; /* warn once per interval, not once per sweep */
            }
        }
    }
}
