/* Deciding what an update offer means. See ota_service.h. */
#include "nev_services/ota_service.h"

#include <stdio.h>
#include <string.h>

#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_version.h"
#include "nev_port/nev_log.h"

#define TAG                 "ota"

/*
 * What an image has to fit into.
 *
 * targets/esp32s3/partitions.csv gives each OTA slot 5 MB; A/B is what halves
 * a 16 MB flash. It is a constant here rather than read from the partition
 * table because there is no partition table on a host, and an offer arriving at
 * a simulator should be judged by the same arithmetic the device would use —
 * otherwise the one place the rejection paths can be exercised is the one place
 * they behave differently.
 */
#define OTA_PARTITION_BYTES (5u * 1024u * 1024u)

static nev_ota_core_t s_core;
static nev_sub_t *s_sub;
static char s_reason[48];
static bool s_ready;

static void publish_reason(uint16_t type, const char *text) {
    const size_t len = strlen(text) + 1;
    uint8_t *data = NULL;
    nev_blob_t handle = nev_blob_alloc(len, &data);
    if (handle == NEV_BLOB_NONE) {
        NEV_LOGW(TAG, "no blob for %s", nev_evt_name(type));
        return;
    }
    memcpy(data, text, len);

    nev_event_t ev = nev_event_make(type, NEV_SRC_OTA);
    ev.flags |= NEV_EVF_BLOB;
    ev.p.blob.handle = handle;
    ev.p.blob.len = (uint32_t)len;
    (void)nev_bus_publish(&ev);
    nev_blob_release(handle); /* step 3 of the ownership protocol */
}

static void judge(const nev_ota_offer_t *offer) {
    /*
     * A fresh core per offer. The alternative — reusing whatever state the last
     * offer left behind — means a daemon that offers twice gets a different
     * answer the second time for reasons nobody can see.
     */
    nev_ota_core_init(&s_core, NEVOS_VERSION, OTA_PARTITION_BYTES);

    if (nev_ota_core_offer(&s_core, offer)) {
        NEV_LOGI(TAG, "update %s offered (%u bytes)", offer->version, (unsigned)offer->size_bytes);
        s_reason[0] = '\0';
        publish_reason(NEV_EVT_OTA_AVAILABLE, offer->version);
        return;
    }

    /*
     * Turned down, and said out loud. "Not newer" is the common case and is not
     * an error — a daemon offering the version already running is a daemon
     * doing its job — but a digest that is the wrong length or an image that
     * will not fit is somebody's build being broken, and a device that silently
     * ignores those is a device nobody can debug.
     */
    const char *why = nev_ota_reject_reason(s_core.reject);
    snprintf(s_reason, sizeof(s_reason), "%s", why);
    NEV_LOGI(TAG, "update %s refused: %s", offer->version, why);
    publish_reason(NEV_EVT_OTA_FAILED, why);
}

nev_err_t ota_service_init(void) {
    if (s_ready) return NEV_OK;

    nev_ota_core_init(&s_core, NEVOS_VERSION, OTA_PARTITION_BYTES);
    s_reason[0] = '\0';

    const nev_sub_cfg_t cfg = {
        .name = "ota",
        .domains = NEV_DOM(BRIDGE),
        /* Offers arrive at most once per connection. Two slots is one in hand
         * and one spare; anything more is memory held for an event that comes
         * a few times a day. */
        .depth = 2,
        .full_policy = NEV_FULL_DROP_OLDEST,
    };
    s_sub = nev_bus_subscribe(&cfg);
    if (!s_sub) return NEV_ERR_NO_MEM;

    s_ready = true;
    return NEV_OK;
}

void ota_service_deinit(void) {
    if (!s_ready) return;
    s_ready = false;
    s_sub = NULL;
}

void ota_service_tick(uint32_t now_ms) {
    (void)now_ms;
    if (!s_ready) return;

    nev_event_t ev;
    while (nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        if (ev.type == NEV_EVT_BRIDGE_OTA_OFFER && (ev.flags & NEV_EVF_BLOB) &&
            ev.p.blob.len == sizeof(nev_ota_offer_t)) {
            /*
             * Copied out before anything else happens to it. The blob belongs
             * to the bus and is released at the bottom of this loop, and
             * judging an offer through a pointer into a buffer that is about to
             * be freed is the kind of bug that works until the pool is busy.
             */
            nev_ota_offer_t offer;
            memcpy(&offer, nev_blob_data(ev.p.blob.handle), sizeof(offer));
            judge(&offer);
        }
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }
}

nev_ota_state_t ota_service_state(void) {
    return nev_ota_core_state(&s_core);
}

const char *ota_service_offered_version(void) {
    return nev_ota_core_state(&s_core) == NEV_OTA_OFFERED ? s_core.offer.version : "";
}

const char *ota_service_last_reason(void) {
    return s_reason;
}
