/* NEVOS L2 — deciding whether an update is worth taking, and whether it arrived intact. */
#ifndef NEV_SERVICES_OTA_CORE_H
#define NEV_SERVICES_OTA_CORE_H

#include "nev_kernel/nev_ota_offer.h"
#include "nev_kernel/nev_sha256.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The half of OTA that has no flash in it: is this offer newer, does it fit,
 * and are these bytes the ones that were offered.
 *
 * Writing to a partition and asking the bootloader to swap is a dozen ESP-IDF
 * calls that can only run on the device. Everything that decides whether to
 * make those calls is here, where it can be tested — including the cases that
 * matter most and are hardest to stage on hardware: a truncated download, a
 * flipped bit, an offer that is actually a downgrade.
 */

typedef enum {
    NEV_OTA_IDLE = 0,
    NEV_OTA_OFFERED,     /* an update is available and worth taking */
    NEV_OTA_DOWNLOADING, /* bytes arriving                          */
    NEV_OTA_READY,       /* verified; the bootloader can be pointed at it */
    NEV_OTA_FAILED,      /* see reason                              */
} nev_ota_state_t;

typedef enum {
    NEV_OTA_REJECT_NONE = 0,
    NEV_OTA_REJECT_NOT_NEWER, /* same version, or older            */
    NEV_OTA_REJECT_TOO_BIG,   /* will not fit the OTA partition    */
    NEV_OTA_REJECT_MALFORMED, /* unparseable version, zero size    */
    NEV_OTA_REJECT_SHORT,     /* fewer bytes arrived than promised */
    NEV_OTA_REJECT_LONG,      /* more bytes arrived than promised  */
    NEV_OTA_REJECT_DIGEST,    /* the bytes are not the ones offered */
} nev_ota_reject_t;

/* nev_ota_offer_t is the bus payload, so it lives in nev_kernel. */

typedef struct {
    uint32_t partition_bytes; /* what the image has to fit into */
    char current_version[NEV_OTA_VERSION_MAX];

    nev_ota_state_t state;
    nev_ota_reject_t reject;
    nev_ota_offer_t offer;

    nev_sha256_t hash;
    uint32_t received;
} nev_ota_core_t;

void nev_ota_core_init(nev_ota_core_t *c, const char *current_version, uint32_t partition_bytes);

/* Evaluates an offer. True when it is worth downloading. */
bool nev_ota_core_offer(nev_ota_core_t *c, const nev_ota_offer_t *offer);

/* Accepts the offer and prepares to receive. */
void nev_ota_core_begin(nev_ota_core_t *c);

/* Feeds downloaded bytes. False when the image has already overrun its
 * promised size, which means the download is wrong and should stop now. */
bool nev_ota_core_feed(nev_ota_core_t *c, const uint8_t *data, size_t len);

/* 0..100. */
uint8_t nev_ota_core_progress(const nev_ota_core_t *c);

/* Finishes: checks length and digest. True when the image may be installed. */
bool nev_ota_core_finish(nev_ota_core_t *c);

void nev_ota_core_abort(nev_ota_core_t *c, nev_ota_reject_t why);

nev_ota_state_t nev_ota_core_state(const nev_ota_core_t *c);
const char *nev_ota_reject_reason(nev_ota_reject_t why);

/*
 * Compares two dotted versions. Returns <0, 0 or >0.
 *
 * Numeric per component, not lexicographic: "0.10.0" is newer than "0.9.0",
 * and a string comparison says the opposite. That bug ships an update that
 * refuses to install and is diagnosed months later.
 */
int nev_ota_version_compare(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_OTA_CORE_H */
