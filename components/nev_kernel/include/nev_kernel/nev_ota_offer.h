/* NEVOS L1 — the shape of an update offer, as it crosses the bus. */
#ifndef NEV_KERNEL_NEV_OTA_OFFER_H
#define NEV_KERNEL_NEV_OTA_OFFER_H

#include "nev_kernel/nev_sha256.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The blob carried by BRIDGE.OTA_OFFER.
 *
 * It lives at L1 rather than with the code that judges it (nev_services) or the
 * code that receives it off the wire (nev_bridge) because it is neither of
 * their property: it is the payload of an event, and the event vocabulary is
 * the one language every layer is allowed to speak. The bridge fills it in, the
 * OTA service reads it, and neither of them has to include the other — which is
 * what peer isolation above L3 requires.
 *
 * No URL. The offer on the wire has one and the device deliberately does not
 * carry it across the bus: nothing can fetch it, and when something can it
 * should come down the link the device is already authenticated on rather than
 * out to an address it was handed. See ota_service.h.
 */
/* The wire allows 24 characters for a version, so 25 with its NUL. Sized to
 * the schema rather than a round number: one byte short and every offer from a
 * daemon that uses the full field arrives silently truncated. */
#define NEV_OTA_VERSION_MAX 25

typedef struct {
    char version[NEV_OTA_VERSION_MAX];
    uint32_t size_bytes;
    uint8_t sha256[NEV_SHA256_DIGEST_LEN];
} nev_ota_offer_t;

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_OTA_OFFER_H */
