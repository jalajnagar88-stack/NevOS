/*
 * NEVOS L1 — the event bus.
 *
 * The only sanctioned way for NEVOS subsystems to communicate. Full contract,
 * including the ordering guarantees and the blob ownership protocol, is in
 * docs/event-bus.md. Read that before adding a subscriber.
 *
 * Summary of the properties this implementation is required to have:
 *   - nev_bus_publish() never blocks and never runs subscriber code.
 *   - Subscriber rings are statically allocated; publish does not allocate.
 *   - A full ring drops per its own policy and counts the drop; it never stalls
 *     the publisher. There is no blocking overflow policy, by design.
 */
#ifndef NEV_KERNEL_NEV_BUS_H
#define NEV_KERNEL_NEV_BUS_H

#include "nev_kernel/nev_event.h"
#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NEV_BUS_MAX_SUBS
#define NEV_BUS_MAX_SUBS 16
#endif

#ifndef NEV_BUS_MAX_DEPTH
#define NEV_BUS_MAX_DEPTH 32
#endif

#define NEV_SUB_NAME_MAX 16

typedef enum {
    /* Discard the arriving event. History matters more than currency. */
    NEV_FULL_DROP_NEWEST = 0,
    /* Evict the oldest, enqueue the new one. The latest value is the useful one. */
    NEV_FULL_DROP_OLDEST,
    /* NEV_FULL_BLOCK deliberately does not exist. See docs/event-bus.md §5. */
} nev_full_policy_t;

typedef struct {
    const char       *name;    /* copied; appears in overflow warnings   */
    uint32_t          domains; /* NEV_DOM(X) | NEV_DOM(Y), or NEV_DOM_ALL */
    uint8_t           depth;   /* 1..NEV_BUS_MAX_DEPTH                    */
    nev_full_policy_t full_policy;
    bool coalesce; /* replace a queued event of the same type instead of enqueueing */
} nev_sub_cfg_t;

typedef struct nev_sub nev_sub_t;

typedef struct {
    uint32_t received;
    uint32_t dropped;
    uint32_t coalesced;
    uint8_t  depth;
    uint8_t  queued;
    uint8_t  high_water;
} nev_sub_stats_t;

typedef struct {
    uint32_t published;
    uint32_t delivered;
    uint32_t dropped;
    uint8_t  sub_count;
} nev_bus_stats_t;

nev_err_t nev_bus_init(void);
void      nev_bus_deinit(void); /* tears down every subscription; for tests and shutdown */
bool      nev_bus_is_ready(void);

/*
 * Subscribe. Callable only during init, before publishing begins: subscriber
 * slots are a fixed static array and there is no unsubscribe. Returns NULL if
 * the configuration is invalid or all slots are taken.
 */
nev_sub_t *nev_bus_subscribe(const nev_sub_cfg_t *cfg);

/*
 * Publish. Never blocks. Safe from any task.
 *
 * Returns NEV_OK when every matching subscriber accepted the event,
 * NEV_ERR_DROPPED when at least one did not. Ignoring the result is normal and
 * safe; the drop is counted and reported either way.
 *
 * If ev->flags has NEV_EVF_BLOB, the bus retains the blob once per accepted
 * delivery. The caller still owns its own reference and must release it after
 * this returns. See docs/event-bus.md §7.
 */
nev_err_t nev_bus_publish(nev_event_t *ev);

/* Convenience for the common payload-free case. */
nev_err_t nev_bus_publish_type(uint16_t type, uint8_t source);

/*
 * Receive one event. timeout_ms may be NEV_NO_WAIT or NEV_WAIT_FOREVER.
 * Returns false on timeout. If the event carries NEV_EVF_BLOB the caller must
 * nev_blob_release() the handle on every path, including error paths.
 */
bool nev_bus_recv(nev_sub_t *sub, nev_event_t *out, uint32_t timeout_ms);

/* Discard everything queued for this subscriber, releasing any blob references. */
void nev_bus_flush(nev_sub_t *sub);

void       nev_bus_stats(nev_bus_stats_t *out);
void       nev_sub_stats(const nev_sub_t *sub, nev_sub_stats_t *out);
const char *nev_sub_name(const nev_sub_t *sub);

/*
 * Publish SYS_BUS_OVERFLOW for any subscriber that has dropped events since the
 * last sweep, rate-limited to one report per subscriber per call. nev_sys calls
 * this once a second; tests call it directly.
 */
void nev_bus_report_overflows(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_BUS_H */
