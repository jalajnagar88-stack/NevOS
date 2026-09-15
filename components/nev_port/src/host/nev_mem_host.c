/*
 * Host allocator with simulated ESP32-S3 budgets.
 *
 * Every block carries a header recording its capability class and size, so the
 * accountant can charge and refund the right budget. This exists so that
 * "we ran out of PSRAM" is a bug reproducible on a laptop rather than one that
 * only appears once the hardware arrives.
 */
#include "nev_port/nev_mem.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_sync.h"
#include <stdlib.h>
#include <string.h>

#define MEM_MAGIC 0x4E45564Du /* "NEVM" */

typedef struct {
    uint32_t magic;
    uint32_t caps;
    size_t   size;
    size_t   pad; /* keep the payload 16-byte aligned */
} mem_hdr_t;

static size_t      s_used_internal;
static size_t      s_used_psram;
static nev_mutex_t s_lock;
static bool        s_lock_ready;

static void lock_init_once(void) {
    if (!s_lock_ready) {
        nev_mutex_init(&s_lock);
        s_lock_ready = true;
    }
}

/* PSRAM unless the caller explicitly demands internal or DMA-capable memory. */
static bool wants_internal(uint32_t caps) {
    return (caps & (NEV_MEM_INTERNAL | NEV_MEM_DMA)) != 0;
}

void *nev_malloc(size_t size, uint32_t caps) {
    if (size == 0) return NULL;
    lock_init_once();

    bool   internal = wants_internal(caps);
    size_t budget = internal ? NEV_SIM_INTERNAL_BYTES : NEV_SIM_PSRAM_BYTES;

    nev_mutex_lock(&s_lock);
    size_t *used = internal ? &s_used_internal : &s_used_psram;
    if (*used + size > budget) {
        nev_mutex_unlock(&s_lock);
        return NULL; /* simulated exhaustion */
    }
    *used += size;
    nev_mutex_unlock(&s_lock);

    mem_hdr_t *h = malloc(sizeof(mem_hdr_t) + size);
    if (!h) {
        nev_mutex_lock(&s_lock);
        *used -= size;
        nev_mutex_unlock(&s_lock);
        return NULL;
    }
    h->magic = MEM_MAGIC;
    h->caps = caps;
    h->size = size;
    h->pad = 0;
    return (uint8_t *)h + sizeof(mem_hdr_t);
}

void *nev_calloc(size_t count, size_t size, uint32_t caps) {
    if (count != 0 && size > SIZE_MAX / count) return NULL;
    size_t total = count * size;
    void  *p = nev_malloc(total, caps);
    if (p) memset(p, 0, total);
    return p;
}

void nev_free(void *ptr) {
    if (!ptr) return;
    mem_hdr_t *h = (mem_hdr_t *)((uint8_t *)ptr - sizeof(mem_hdr_t));
    NEV_CHECK(h->magic == MEM_MAGIC); /* double free or corrupted heap */
    h->magic = 0;

    nev_mutex_lock(&s_lock);
    if (wants_internal(h->caps)) {
        s_used_internal -= h->size;
    } else {
        s_used_psram -= h->size;
    }
    nev_mutex_unlock(&s_lock);
    free(h);
}

size_t nev_mem_used_bytes(uint32_t caps) {
    return wants_internal(caps) ? s_used_internal : s_used_psram;
}

size_t nev_mem_free_bytes(uint32_t caps) {
    return wants_internal(caps) ? NEV_SIM_INTERNAL_BYTES - s_used_internal
                                : NEV_SIM_PSRAM_BYTES - s_used_psram;
}
