/*
 * NEVOS L1 — event bus implementation.
 *
 * Invariants this file must uphold (docs/event-bus.md):
 *   - publish never blocks and never calls into subscriber code
 *   - no allocation after nev_bus_init
 *   - a subscriber's semaphore count always equals its ring count
 *   - every blob reference the bus takes is released exactly once
 */
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_blob.h"
#include "nev_port/nev_assert.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_sync.h"
#include "nev_port/nev_time.h"
#include <stdio.h>
#include <string.h>

#define TAG "bus"

struct nev_sub {
    bool in_use;
    char name[NEV_SUB_NAME_MAX];
    uint32_t domains;
    uint8_t depth;
    nev_full_policy_t policy;
    bool coalesce;

    nev_event_t ring[NEV_BUS_MAX_DEPTH];
    uint8_t head, tail, count, high_water;

    uint32_t received, dropped, coalesced, dropped_reported;
    uint16_t last_dropped_type;

    nev_sem_t sem;
};

static struct nev_sub s_subs[NEV_BUS_MAX_SUBS];
static nev_mutex_t s_lock;
static bool s_ready;
static uint32_t s_seq;
static nev_bus_stats_t s_stats;

/* Each domain has exactly one legitimate producer. Checked in debug builds. */
static uint8_t expected_source(uint8_t domain_id) {
    switch (domain_id) {
        case NEV_DOM_ID_SYS:
            return NEV_SRC_KERNEL;
        case NEV_DOM_ID_INPUT:
            return NEV_SRC_INPUT;
        case NEV_DOM_ID_DISPLAY:
            return NEV_SRC_DISPLAY;
        case NEV_DOM_ID_AUDIO:
            return NEV_SRC_AUDIO;
        case NEV_DOM_ID_NET:
            return NEV_SRC_NET;
        case NEV_DOM_ID_PERSONA:
            return NEV_SRC_PERSONA;
        case NEV_DOM_ID_APP:
            return NEV_SRC_APPKIT;
        case NEV_DOM_ID_BRIDGE:
            return NEV_SRC_BRIDGE;
        case NEV_DOM_ID_POWER:
            return NEV_SRC_POWER;
        case NEV_DOM_ID_STORAGE:
            return NEV_SRC_STORE;
        case NEV_DOM_ID_GAME:
            return NEV_SRC_GAME;
        case NEV_DOM_ID_OTA:
            return NEV_SRC_OTA;
        default:
            return NEV_SRC_UNKNOWN;
    }
}

nev_err_t nev_bus_init(void) {
    if (s_ready) return NEV_OK;
    NEV_TRY(nev_mutex_init(&s_lock));
    memset(s_subs, 0, sizeof(s_subs));
    memset(&s_stats, 0, sizeof(s_stats));
    s_seq = 0;
    s_ready = true;
    NEV_LOGI(TAG, "ready: %d slots x %d events (%u B static)", NEV_BUS_MAX_SUBS, NEV_BUS_MAX_DEPTH,
             (unsigned)sizeof(s_subs));
    return NEV_OK;
}

void nev_bus_deinit(void) {
    if (!s_ready) return;
    for (int i = 0; i < NEV_BUS_MAX_SUBS; i++) {
        if (!s_subs[i].in_use) continue;
        nev_bus_flush(&s_subs[i]);
        nev_sem_deinit(&s_subs[i].sem);
        s_subs[i].in_use = false;
    }
    nev_mutex_deinit(&s_lock);
    s_ready = false;
}

bool nev_bus_is_ready(void) {
    return s_ready;
}

nev_sub_t *nev_bus_subscribe(const nev_sub_cfg_t *cfg) {
    if (!s_ready || !cfg || !cfg->name) return NULL;
    if (cfg->depth == 0 || cfg->depth > NEV_BUS_MAX_DEPTH) {
        NEV_LOGE(TAG, "'%s': depth %u out of range 1..%d", cfg->name, cfg->depth,
                 NEV_BUS_MAX_DEPTH);
        return NULL;
    }
    if (cfg->domains == NEV_DOM_NONE) {
        NEV_LOGE(TAG, "'%s': empty domain mask would never receive anything", cfg->name);
        return NULL;
    }

    nev_mutex_lock(&s_lock);
    struct nev_sub *sub = NULL;
    for (int i = 0; i < NEV_BUS_MAX_SUBS; i++) {
        if (!s_subs[i].in_use) {
            sub = &s_subs[i];
            break;
        }
    }
    if (!sub) {
        nev_mutex_unlock(&s_lock);
        NEV_LOGE(TAG, "'%s': all %d subscriber slots taken", cfg->name, NEV_BUS_MAX_SUBS);
        return NULL;
    }

    memset(sub, 0, sizeof(*sub));
    snprintf(sub->name, sizeof(sub->name), "%s", cfg->name);
    sub->domains = cfg->domains;
    sub->depth = cfg->depth;
    sub->policy = cfg->full_policy;
    sub->coalesce = cfg->coalesce;

    if (nev_sem_init(&sub->sem, cfg->depth) != NEV_OK) {
        nev_mutex_unlock(&s_lock);
        NEV_LOGE(TAG, "'%s': semaphore init failed", cfg->name);
        return NULL;
    }
    sub->in_use = true;
    s_stats.sub_count++;
    nev_mutex_unlock(&s_lock);

    NEV_LOGI(TAG, "+ '%s' depth=%u mask=0x%08X%s", sub->name, sub->depth, (unsigned)sub->domains,
             sub->coalesce ? " coalescing" : "");
    return sub;
}

typedef enum {
    ENQ_DROPPED = 0, /* not delivered                               */
    ENQ_REPLACED,    /* delivered, ring count unchanged (no signal) */
    ENQ_QUEUED,      /* delivered, ring count grew (signal)         */
} enq_result_t;

/* Caller holds s_lock. */
static enq_result_t enqueue(struct nev_sub *sub, const nev_event_t *ev) {
    const bool carries_blob = (ev->flags & NEV_EVF_BLOB) != 0;

    if (sub->coalesce) {
        for (uint8_t k = 0; k < sub->count; k++) {
            uint8_t idx = (uint8_t)((sub->tail + k) % sub->depth);
            nev_event_t *slot = &sub->ring[idx];
            if (slot->type != ev->type) continue;

            if (slot->flags & NEV_EVF_BLOB) nev_blob_release(slot->p.blob.handle);
            *slot = *ev;
            if (carries_blob) nev_blob_retain(ev->p.blob.handle);
            sub->coalesced++;
            sub->received++;
            return ENQ_REPLACED;
        }
    }

    bool evicted = false;
    if (sub->count == sub->depth) {
        if (sub->policy == NEV_FULL_DROP_NEWEST) {
            sub->dropped++;
            sub->last_dropped_type = ev->type;
            return ENQ_DROPPED;
        }
        /* DROP_OLDEST: the evicted event's blob reference dies with it. */
        nev_event_t *oldest = &sub->ring[sub->tail];
        if (oldest->flags & NEV_EVF_BLOB) nev_blob_release(oldest->p.blob.handle);
        sub->last_dropped_type = oldest->type;
        sub->tail = (uint8_t)((sub->tail + 1) % sub->depth);
        sub->count--;
        sub->dropped++;
        evicted = true;
    }

    sub->ring[sub->head] = *ev;
    sub->head = (uint8_t)((sub->head + 1) % sub->depth);
    sub->count++;
    if (sub->count > sub->high_water) sub->high_water = sub->count;
    sub->received++;
    if (carries_blob) nev_blob_retain(ev->p.blob.handle);

    /* An evict-then-enqueue leaves the count where it was, so the semaphore
     * already has a token for this slot. Signalling again would desynchronise
     * the count from the ring. */
    return evicted ? ENQ_REPLACED : ENQ_QUEUED;
}

nev_err_t nev_bus_publish(nev_event_t *ev) {
    NEV_REQUIRE(ev != NULL, NEV_ERR_INVALID_ARG);
    if (!s_ready) return NEV_ERR_INVALID_STATE;

    NEV_ASSERT(ev->source == NEV_SRC_TEST ||
               ev->source == expected_source(NEV_TYPE_DOMAIN_ID(ev->type)));

    struct nev_sub *to_signal[NEV_BUS_MAX_SUBS];
    uint8_t signal_count = 0;
    bool any_dropped = false;

    nev_mutex_lock(&s_lock);
    ev->seq = ++s_seq;
    ev->ts_us = nev_now_us();
    s_stats.published++;

    const uint32_t mask = NEV_TYPE_DOMAIN_MASK(ev->type);
    for (int i = 0; i < NEV_BUS_MAX_SUBS; i++) {
        struct nev_sub *sub = &s_subs[i];
        if (!sub->in_use || (sub->domains & mask) == 0) continue;

        switch (enqueue(sub, ev)) {
            case ENQ_QUEUED:
                to_signal[signal_count++] = sub;
                s_stats.delivered++;
                break;
            case ENQ_REPLACED:
                s_stats.delivered++;
                break;
            case ENQ_DROPPED:
                s_stats.dropped++;
                any_dropped = true;
                break;
        }
    }
    nev_mutex_unlock(&s_lock);

    /* Signalling outside the lock keeps the critical section to a memcpy. */
    for (uint8_t i = 0; i < signal_count; i++)
        nev_sem_give(&to_signal[i]->sem);

    return any_dropped ? NEV_ERR_DROPPED : NEV_OK;
}

nev_err_t nev_bus_publish_type(uint16_t type, uint8_t source) {
    nev_event_t ev = nev_event_make(type, source);
    return nev_bus_publish(&ev);
}

bool nev_bus_recv(nev_sub_t *sub, nev_event_t *out, uint32_t timeout_ms) {
    if (!s_ready || !sub || !out || !sub->in_use) return false;
    if (!nev_sem_take(&sub->sem, timeout_ms)) return false;

    nev_mutex_lock(&s_lock);
    bool got = sub->count > 0;
    if (got) {
        *out = sub->ring[sub->tail];
        sub->tail = (uint8_t)((sub->tail + 1) % sub->depth);
        sub->count--;
    }
    nev_mutex_unlock(&s_lock);
    return got;
}

void nev_bus_flush(nev_sub_t *sub) {
    if (!s_ready || !sub) return;
    nev_mutex_lock(&s_lock);
    while (sub->count > 0) {
        nev_event_t *e = &sub->ring[sub->tail];
        if (e->flags & NEV_EVF_BLOB) nev_blob_release(e->p.blob.handle);
        sub->tail = (uint8_t)((sub->tail + 1) % sub->depth);
        sub->count--;
    }
    sub->head = sub->tail = 0;
    nev_mutex_unlock(&s_lock);
    while (nev_sem_take(&sub->sem, NEV_NO_WAIT)) {
    }
}

void nev_bus_stats(nev_bus_stats_t *out) {
    if (!out) return;
    nev_mutex_lock(&s_lock);
    *out = s_stats;
    nev_mutex_unlock(&s_lock);
}

void nev_sub_stats(const nev_sub_t *sub, nev_sub_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!sub) return;
    nev_mutex_lock(&s_lock);
    out->received = sub->received;
    out->dropped = sub->dropped;
    out->coalesced = sub->coalesced;
    out->depth = sub->depth;
    out->queued = sub->count;
    out->high_water = sub->high_water;
    nev_mutex_unlock(&s_lock);
}

const char *nev_sub_name(const nev_sub_t *sub) {
    return sub ? sub->name : "?";
}

void nev_bus_report_overflows(void) {
    if (!s_ready) return;

    struct {
        uint8_t index;
        uint16_t type;
        uint32_t total;
        uint32_t delta;
        char name[NEV_SUB_NAME_MAX];
    } pending[NEV_BUS_MAX_SUBS];
    uint8_t n = 0;

    /* Snapshot under the lock; publish outside it, since publishing re-enters. */
    nev_mutex_lock(&s_lock);
    for (int i = 0; i < NEV_BUS_MAX_SUBS; i++) {
        struct nev_sub *sub = &s_subs[i];
        if (!sub->in_use || sub->dropped == sub->dropped_reported) continue;
        pending[n].index = (uint8_t)i;
        pending[n].type = sub->last_dropped_type;
        pending[n].total = sub->dropped;
        pending[n].delta = sub->dropped - sub->dropped_reported;
        memcpy(pending[n].name, sub->name, sizeof(pending[n].name));
        sub->dropped_reported = sub->dropped;
        n++;
    }
    nev_mutex_unlock(&s_lock);

    for (uint8_t i = 0; i < n; i++) {
        NEV_LOGW(TAG, "'%s' dropped %u (%u total), last was %s", pending[i].name,
                 (unsigned)pending[i].delta, (unsigned)pending[i].total,
                 nev_evt_name(pending[i].type));

        nev_event_t ev = nev_event_make(NEV_EVT_SYS_BUS_OVERFLOW, NEV_SRC_KERNEL);
        ev.p.overflow.sub_index = pending[i].index;
        ev.p.overflow.event_type = pending[i].type;
        ev.p.overflow.dropped_total = pending[i].total;
        (void)nev_bus_publish(&ev);
    }
}
