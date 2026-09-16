#include "nev_kernel/nev_events.h"
#include <stdio.h>

const char *nev_domain_name(uint8_t domain_id) {
    switch (domain_id) {
#define NEV_X(name, value)                                                                         \
    case value:                                                                                    \
        return #name;
        NEV_DOMAIN_LIST(NEV_X)
#undef NEV_X
        default:
            break;
    }
    return "?";
}

bool nev_evt_is_coalescable(uint16_t type) {
    switch (type) {
        /* Each of these is "the current value of something". Losing an older
         * one costs nothing, because the newer one says everything it said. */
        case NEV_EVT_DISPLAY_FRAME_STATS:
        case NEV_EVT_INPUT_TOUCH:
        case NEV_EVT_INPUT_GESTURE_TILT:
        case NEV_EVT_POWER_BATTERY:
        case NEV_EVT_SYS_HEAP_STATS:
        case NEV_EVT_PERSONA_MOOD_CHANGED:
        /* The schema says a partial transcript "replaces any previous partial
         * for this session", so this one is a snapshot by definition. */
        case NEV_EVT_BRIDGE_TRANSCRIPT_PARTIAL:
            return true;
        default:
            return false;
    }
}

const char *nev_evt_name(uint16_t type) {
    switch (type) {
#define NEV_X(domain, name, code)                                                                  \
    case NEV_TYPE(domain, code):                                                                   \
        return #domain "." #name;
        NEV_EVENT_LIST(NEV_X)
#undef NEV_X
        default:
            break;
    }
    /*
     * Unknown type: render it rather than returning NULL, so a trace from a
     * newer firmware is still readable by an older tool. Small rotating buffer
     * because callers treat this as a string literal.
     */
    static char buf[4][16];
    static uint8_t slot;
    slot = (uint8_t)((slot + 1u) & 3u);
    snprintf(buf[slot], sizeof(buf[slot]), "%s.0x%02X", nev_domain_name(NEV_TYPE_DOMAIN_ID(type)),
             NEV_TYPE_CODE(type));
    return buf[slot];
}
