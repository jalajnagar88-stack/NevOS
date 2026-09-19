/* NEVOS L2 — what to do about an update the daemon is offering. */
#ifndef NEV_SERVICES_OTA_SERVICE_H
#define NEV_SERVICES_OTA_SERVICE_H

#include "nev_services/ota_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The only producer of OTA events.
 *
 * The bridge relays what the daemon said as BRIDGE.OTA_OFFER and stops there,
 * because deciding whether an update is worth taking is not a transport's job
 * and the bridge is a peer of this layer, not above it. This service subscribes,
 * runs the offer through ota_core, and publishes OTA.AVAILABLE when there is
 * something worth installing or OTA.FAILED with a reason when there is not.
 *
 * WHAT THIS DOES NOT DO, and why:
 *
 * It does not download and it does not install. Both halves are real work that
 * cannot be finished here. Installing means writing a partition and asking the
 * bootloader to swap, which is a dozen ESP-IDF calls that exist only on the
 * device — the board is not chosen yet, so there is no partition to write to
 * and no way to test that the write was correct.
 *
 * Downloading is the more interesting omission. The offer carries a URL, and
 * fetching a firmware image over plain HTTP from an address the device was
 * handed is precisely the kind of thing that turns a desk ornament into
 * somebody else's computer. The link to the daemon is already authenticated and
 * already open; when the download is built it should come down that, as
 * schema messages, rather than through a second unauthenticated path that
 * exists only for this. That is a protocol change with a daemon half, and it is
 * written down here rather than half-built.
 *
 * So the device notices, checks, tells the user, and stops — which is the whole
 * of what it can honestly do without a board, and it is tested.
 */

nev_err_t ota_service_init(void);
void ota_service_deinit(void);

/* Drains the bus. Called from the same loop as the other services. */
void ota_service_tick(uint32_t now_ms);

nev_ota_state_t ota_service_state(void);

/* The version being offered, or "" when there is nothing pending. */
const char *ota_service_offered_version(void);

/* Why the last offer was turned down, or "" if none was. */
const char *ota_service_last_reason(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_SERVICES_OTA_SERVICE_H */
