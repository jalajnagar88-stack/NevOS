/* The half of OTA with no flash in it. See ota_core.h. */
#include "nev_services/ota_core.h"

#include <stdio.h>
#include <string.h>

int nev_ota_version_compare(const char *a, const char *b) {
    if (!a || !b) return 0;

    for (;;) {
        /* A missing component is zero, so "1.2" and "1.2.0" are the same
         * version rather than one of them being mysteriously older. */
        uint32_t na = 0, nb = 0;
        bool digits_a = false, digits_b = false;

        while (*a >= '0' && *a <= '9') {
            na = na * 10u + (uint32_t)(*a++ - '0');
            digits_a = true;
        }
        while (*b >= '0' && *b <= '9') {
            nb = nb * 10u + (uint32_t)(*b++ - '0');
            digits_b = true;
        }
        if (na != nb) return na < nb ? -1 : 1;
        if (!digits_a && !digits_b) return 0;

        /* Step over one separator each. Anything that is not a digit ends the
         * numeric part — "1.2.3-rc1" compares as 1.2.3, which is the right
         * answer for deciding whether to update. */
        if (*a == '.')
            a++;
        else if (*a)
            return 0;
        if (*b == '.')
            b++;
        else if (*b)
            return 0;
        if (!*a && !*b) return 0;
    }
}

void nev_ota_core_init(nev_ota_core_t *c, const char *current_version, uint32_t partition_bytes) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    snprintf(c->current_version, sizeof(c->current_version), "%s",
             current_version ? current_version : "0.0.0");
    c->partition_bytes = partition_bytes;
    c->state = NEV_OTA_IDLE;
}

bool nev_ota_core_offer(nev_ota_core_t *c, const nev_ota_offer_t *offer) {
    if (!c || !offer) return false;

    c->reject = NEV_OTA_REJECT_NONE;

    if (offer->version[0] == '\0' || offer->size_bytes == 0) {
        c->reject = NEV_OTA_REJECT_MALFORMED;
        return false;
    }
    if (nev_ota_version_compare(offer->version, c->current_version) <= 0) {
        /* Not an error worth showing anyone: a daemon offering the version
         * already running is the normal state of affairs. */
        c->reject = NEV_OTA_REJECT_NOT_NEWER;
        return false;
    }
    if (offer->size_bytes > c->partition_bytes) {
        c->reject = NEV_OTA_REJECT_TOO_BIG;
        return false;
    }

    c->offer = *offer;
    c->state = NEV_OTA_OFFERED;
    return true;
}

void nev_ota_core_begin(nev_ota_core_t *c) {
    if (!c || c->state != NEV_OTA_OFFERED) return;
    nev_sha256_init(&c->hash);
    c->received = 0;
    c->state = NEV_OTA_DOWNLOADING;
}

bool nev_ota_core_feed(nev_ota_core_t *c, const uint8_t *data, size_t len) {
    if (!c || c->state != NEV_OTA_DOWNLOADING) return false;
    if (!data && len) return false;

    /*
     * Refused as soon as it overruns, rather than at the end.
     *
     * The bytes are being written to a flash partition as they arrive. A
     * download that has already exceeded what was promised is wrong whatever
     * its digest turns out to be, and continuing to write is continuing to
     * scribble past the end of something.
     */
    if ((uint64_t)c->received + len > c->offer.size_bytes) {
        nev_ota_core_abort(c, NEV_OTA_REJECT_LONG);
        return false;
    }

    nev_sha256_update(&c->hash, data, len);
    c->received += (uint32_t)len;
    return true;
}

uint8_t nev_ota_core_progress(const nev_ota_core_t *c) {
    if (!c || c->offer.size_bytes == 0) return 0;
    const uint64_t pct = (uint64_t)c->received * 100u / c->offer.size_bytes;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

bool nev_ota_core_finish(nev_ota_core_t *c) {
    if (!c || c->state != NEV_OTA_DOWNLOADING) return false;

    if (c->received != c->offer.size_bytes) {
        nev_ota_core_abort(c, NEV_OTA_REJECT_SHORT);
        return false;
    }

    uint8_t digest[NEV_SHA256_DIGEST_LEN];
    nev_sha256_final(&c->hash, digest);
    if (!nev_sha256_equal(digest, c->offer.sha256)) {
        /*
         * The one check that must never be skipped. Everything upstream of here
         * is a network the device does not control, and the consequence of
         * getting this wrong is a bricked device in someone's house.
         */
        nev_ota_core_abort(c, NEV_OTA_REJECT_DIGEST);
        return false;
    }

    c->state = NEV_OTA_READY;
    return true;
}

void nev_ota_core_abort(nev_ota_core_t *c, nev_ota_reject_t why) {
    if (!c) return;
    c->state = NEV_OTA_FAILED;
    c->reject = why;
    c->received = 0;
}

nev_ota_state_t nev_ota_core_state(const nev_ota_core_t *c) {
    return c ? c->state : NEV_OTA_IDLE;
}

const char *nev_ota_reject_reason(nev_ota_reject_t why) {
    switch (why) {
        case NEV_OTA_REJECT_NONE:
            return "";
        case NEV_OTA_REJECT_NOT_NEWER:
            return "already up to date";
        case NEV_OTA_REJECT_TOO_BIG:
            return "the update does not fit";
        case NEV_OTA_REJECT_MALFORMED:
            return "the offer made no sense";
        case NEV_OTA_REJECT_SHORT:
            return "the download stopped early";
        case NEV_OTA_REJECT_LONG:
            return "the download overran";
        case NEV_OTA_REJECT_DIGEST:
            return "the update did not match its checksum";
        default:
            return "?";
    }
}
