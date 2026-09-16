/*
 * NEVOS L1 — the event vocabulary.
 *
 * Domains, event types, names and payload size checks all come from the two
 * X-macro tables below, so an event cannot exist without a stable numeric code
 * and a name. See docs/event-bus.md for the contract.
 *
 * Numeric codes are stable: bus traces are captured on a device and replayed on
 * a host, so renumbering an existing event invalidates recorded traces. Add new
 * codes at the end of a domain; never reuse a retired one.
 */
#ifndef NEV_KERNEL_NEV_EVENTS_H
#define NEV_KERNEL_NEV_EVENTS_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ domains */

#define NEV_DOMAIN_LIST(X)                                                                         \
    X(SYS, 0x01)                                                                                   \
    X(INPUT, 0x02)                                                                                 \
    X(DISPLAY, 0x03)                                                                               \
    X(AUDIO, 0x04)                                                                                 \
    X(NET, 0x05)                                                                                   \
    X(PERSONA, 0x06)                                                                               \
    X(APP, 0x07)                                                                                   \
    X(BRIDGE, 0x08)                                                                                \
    X(POWER, 0x09)                                                                                 \
    X(STORAGE, 0x0A)                                                                               \
    X(GAME, 0x0B)                                                                                  \
    X(OTA, 0x0C)

enum nev_domain_id {
#define NEV_X(name, value) NEV_DOM_ID_##name = value,
    NEV_DOMAIN_LIST(NEV_X)
#undef NEV_X
        NEV_DOM_ID_MAX = 0x1F
};

/* Subscription masks. One bit per domain; matching is a single AND. */
#define NEV_DOM(name)              (1u << (NEV_DOM_ID_##name - 1))
#define NEV_DOM_ALL                (0xFFFFFFFFu)
#define NEV_DOM_NONE               (0u)
#define NEV_DOM_MASK_OF(domain_id) (1u << ((domain_id) - 1))

/* ------------------------------------------------------------------- types */

#define NEV_TYPE(domain, code)     ((uint16_t)(((uint16_t)NEV_DOM_ID_##domain << 8) | (uint8_t)(code)))
#define NEV_TYPE_DOMAIN_ID(t)      ((uint8_t)((t) >> 8))
#define NEV_TYPE_CODE(t)           ((uint8_t)((t) & 0xFF))
#define NEV_TYPE_DOMAIN_MASK(t)    NEV_DOM_MASK_OF(NEV_TYPE_DOMAIN_ID(t))

/*
 * Each domain has exactly one producer (docs/event-bus.md §3). The comment on
 * each group names it; debug builds check the `source` field against it.
 */
#define NEV_EVENT_LIST(X)                                                                          \
    /* SYS — produced by nev_kernel */                                                           \
    X(SYS, BOOT_DONE, 0x01)                                                                        \
    X(SYS, SHUTDOWN, 0x02)                                                                         \
    X(SYS, TICK_1S, 0x03)                                                                          \
    X(SYS, MEM_PRESSURE, 0x04)                                                                     \
    X(SYS, BUS_OVERFLOW, 0x05)                                                                     \
    X(SYS, HEAP_STATS, 0x06)                                                                       \
    /* INPUT — produced by input_service, and by nothing else */                                 \
    X(INPUT, TOUCH, 0x01)                                                                          \
    X(INPUT, BUTTON_DOWN, 0x02)                                                                    \
    X(INPUT, BUTTON_UP, 0x03)                                                                      \
    X(INPUT, GESTURE_SHAKE, 0x04)                                                                  \
    X(INPUT, GESTURE_TAP, 0x05)                                                                    \
    X(INPUT, GESTURE_TILT, 0x06)                                                                   \
    /* DISPLAY — produced by display_service */                                                  \
    X(DISPLAY, FRAME_STATS, 0x01)                                                                  \
    X(DISPLAY, BACKLIGHT_CHANGED, 0x02)                                                            \
    /* AUDIO — produced by audio_service */                                                      \
    X(AUDIO, CAPTURE_START, 0x01)                                                                  \
    X(AUDIO, CAPTURE_STOP, 0x02)                                                                   \
    X(AUDIO, CHUNK, 0x03)                                                                          \
    X(AUDIO, VAD_BEGIN, 0x04)                                                                      \
    X(AUDIO, VAD_END, 0x05)                                                                        \
    X(AUDIO, PLAY_DONE, 0x06)                                                                      \
    /* NET — produced by net_service */                                                          \
    X(NET, WIFI_UP, 0x01)                                                                          \
    X(NET, WIFI_DOWN, 0x02)                                                                        \
    X(NET, DAEMON_FOUND, 0x03)                                                                     \
    X(NET, DAEMON_LOST, 0x04)                                                                      \
    /* PERSONA — produced by nev_persona */                                                      \
    X(PERSONA, MOOD_CHANGED, 0x01)                                                                 \
    X(PERSONA, BLINK, 0x03)                                                                        \
    /* APP — produced by nev_appkit */                                                           \
    X(APP, LAUNCH, 0x01)                                                                           \
    X(APP, SUSPEND, 0x02)                                                                          \
    X(APP, RESUME, 0x03)                                                                           \
    X(APP, CLOSE, 0x04)                                                                            \
    X(APP, NAV_HOME, 0x05)                                                                         \
    /* BRIDGE — produced by nev_bridge */                                                        \
    X(BRIDGE, PAIRED, 0x01)                                                                        \
    X(BRIDGE, CONNECTED, 0x02)                                                                     \
    X(BRIDGE, DISCONNECTED, 0x03)                                                                  \
    X(BRIDGE, TRANSCRIPT_PARTIAL, 0x04)                                                            \
    X(BRIDGE, TRANSCRIPT_FINAL, 0x05)                                                              \
    X(BRIDGE, AGENT_TOKEN, 0x06)                                                                   \
    X(BRIDGE, AGENT_DONE, 0x07)                                                                    \
    X(BRIDGE, NOTIFICATION, 0x08)                                                                  \
    X(BRIDGE, MOOD_HINT, 0x09)                                                                     \
    /* POWER — produced by power_service */                                                      \
    X(POWER, BATTERY, 0x01)                                                                        \
    X(POWER, IDLE_ENTER, 0x02)                                                                     \
    X(POWER, IDLE_EXIT, 0x03)                                                                      \
    X(POWER, CHARGING, 0x04)                                                                       \
    /* STORAGE — produced by nev_store */                                                        \
    X(STORAGE, SETTING_CHANGED, 0x01)                                                              \
    X(STORAGE, FS_READY, 0x02)                                                                     \
    /* GAME — produced by game_engine */                                                         \
    X(GAME, SCORE, 0x01)                                                                           \
    X(GAME, OVER, 0x02)                                                                            \
    X(GAME, HIGHSCORE_BEAT, 0x03)                                                                  \
    /* OTA — produced by ota_service */                                                          \
    X(OTA, AVAILABLE, 0x01)                                                                        \
    X(OTA, PROGRESS, 0x02)                                                                         \
    X(OTA, READY, 0x03)                                                                            \
    X(OTA, FAILED, 0x04)

enum nev_event_type {
#define NEV_X(domain, name, code) NEV_EVT_##domain##_##name = NEV_TYPE(domain, code),
    NEV_EVENT_LIST(NEV_X)
#undef NEV_X
        NEV_EVT_NONE = 0
};

/*
 * May a queued event of this type be overwritten by a newer one?
 *
 * Coalescing is only ever correct for an event that is a *snapshot of state*:
 * the current tilt, the latest battery reading, the best transcript so far. An
 * event that is a *fragment* — one token of a reply, a finished transcript, a
 * notification — carries content that does not exist anywhere else, and
 * replacing it deletes it.
 *
 * This was found the expensive way. A subscriber with .coalesce = true reduced
 * a streamed agent reply to its last word, and the screen said "say." where the
 * daemon had sent a sentence. Nothing was dropped, nothing overflowed, and no
 * counter moved: the event had been faithfully replaced, over and over.
 *
 * So the decision is not the subscriber's alone. The subscriber says whether it
 * is willing to coalesce; this table says which types it is safe to do it to,
 * and the default for a new event type is no.
 */
bool nev_evt_is_coalescable(uint16_t type);

/* Human-readable name, e.g. "INPUT.TOUCH". Never NULL; unknown types render as hex. */
const char *nev_evt_name(uint16_t type);
const char *nev_domain_name(uint8_t domain_id);

/* ---------------------------------------------------------------- publishers */

enum nev_source {
    NEV_SRC_UNKNOWN = 0,
    NEV_SRC_KERNEL,
    NEV_SRC_BOARD,
    NEV_SRC_DISPLAY,
    NEV_SRC_INPUT,
    NEV_SRC_AUDIO,
    NEV_SRC_NET,
    NEV_SRC_POWER,
    NEV_SRC_OTA,
    NEV_SRC_STORE,
    NEV_SRC_PERSONA,
    NEV_SRC_APPKIT,
    NEV_SRC_BRIDGE,
    NEV_SRC_GAME,
    NEV_SRC_APP,
    NEV_SRC_TEST,
};

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_EVENTS_H */
