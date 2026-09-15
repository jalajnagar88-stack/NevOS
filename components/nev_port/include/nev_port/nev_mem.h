/* NEVOS L-1 — capability-tagged allocation.
 *
 * On device these map to ESP-IDF heap capabilities. On host they are tracked
 * against simulated budgets (see NEV_SIM_* below) so that PSRAM exhaustion is
 * reproducible on a laptop instead of only on hardware.
 */
#ifndef NEV_PORT_NEV_MEM_H
#define NEV_PORT_NEV_MEM_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEV_MEM_DEFAULT = 0,
    NEV_MEM_INTERNAL = 1u << 0, /* fast SRAM; scarce */
    NEV_MEM_PSRAM = 1u << 1,    /* plentiful, slow */
    NEV_MEM_DMA = 1u << 2,      /* DMA-capable */
} nev_mem_caps_t;

/* Simulated budgets for the host target; mirrors ESP32-S3-WROOM-1-N16R8. */
#define NEV_SIM_INTERNAL_BYTES (400u * 1024u)
#define NEV_SIM_PSRAM_BYTES    (8u * 1024u * 1024u)

void *nev_malloc(size_t size, uint32_t caps);
void *nev_calloc(size_t count, size_t size, uint32_t caps);
void  nev_free(void *ptr);

size_t nev_mem_free_bytes(uint32_t caps);
size_t nev_mem_used_bytes(uint32_t caps);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PORT_NEV_MEM_H */
