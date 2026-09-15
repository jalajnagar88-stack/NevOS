#include "nev_port/nev_mem.h"
#include "esp_heap_caps.h"

/*
 * Maps NEVOS capability flags onto ESP-IDF heap capabilities.
 *
 * Default is PSRAM: on an N16R8 the 8 MB of PSRAM is the plentiful resource and
 * the 512 KB of internal SRAM is the scarce one, so anything that has not
 * explicitly asked for speed or DMA belongs in PSRAM.
 */
static uint32_t to_heap_caps(uint32_t caps) {
    uint32_t out = 0;
    if (caps & NEV_MEM_DMA)
        out |= MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    else if (caps & NEV_MEM_INTERNAL)
        out |= MALLOC_CAP_INTERNAL;
    else if (caps & NEV_MEM_PSRAM)
        out |= MALLOC_CAP_SPIRAM;
    else
        out |= MALLOC_CAP_SPIRAM;
    return out | MALLOC_CAP_8BIT;
}

void *nev_malloc(size_t size, uint32_t caps) {
    if (size == 0) return NULL;
    return heap_caps_malloc(size, to_heap_caps(caps));
}

void *nev_calloc(size_t count, size_t size, uint32_t caps) {
    if (count == 0 || size == 0) return NULL;
    return heap_caps_calloc(count, size, to_heap_caps(caps));
}

void nev_free(void *ptr) {
    heap_caps_free(ptr);
}

size_t nev_mem_free_bytes(uint32_t caps) {
    return heap_caps_get_free_size(to_heap_caps(caps));
}

size_t nev_mem_used_bytes(uint32_t caps) {
    const uint32_t hc = to_heap_caps(caps);
    return heap_caps_get_total_size(hc) - heap_caps_get_free_size(hc);
}
