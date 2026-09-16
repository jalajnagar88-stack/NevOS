/*
 * NEVOS L1 — the event.
 *
 * Exactly 32 bytes, POD, no owning pointers. Fixed size is what makes the
 * subscriber rings allocation-free and the enqueue a single-stride memcpy.
 * Payloads larger than 16 bytes travel as a blob handle (nev_blob.h).
 */
#ifndef NEV_KERNEL_NEV_EVENT_H
#define NEV_KERNEL_NEV_EVENT_H

#include "nev_kernel/nev_events.h"
#include "nev_port/nev_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEV_EVENT_PAYLOAD_BYTES 16

/* flags */
#define NEV_EVF_BLOB            (1u << 0) /* p.blob.handle owns a reference; receiver must release */

/* ------------------------------------------------------------- payload types */

/*
 * The touch action as it travels on the bus.
 *
 * Deliberately not the board's nev_touch_action_t: that lives at L0, which apps
 * may not include, and the event vocabulary is a contract that should outlive
 * any particular driver's enum. input_service maps between them with an
 * explicit switch, so a divergence is a compile error rather than a silently
 * renumbered field.
 */
typedef enum {
    NEV_TOUCH_ACT_NONE = 0,
    NEV_TOUCH_ACT_DOWN,
    NEV_TOUCH_ACT_MOVE,
    NEV_TOUCH_ACT_UP,
} nev_touch_action_code_t;

typedef struct {
    int16_t x, y;
    uint8_t action; /* nev_touch_action_code_t */
    uint8_t finger;
} nev_p_touch_t;

typedef struct {
    uint8_t id;     /* 0 = A, 1 = B */
    uint8_t repeat; /* auto-repeat count, 0 on first edge */
} nev_p_button_t;

typedef struct {
    uint8_t kind;
    uint8_t strength; /* 0..255 */
    int16_t axis_x, axis_y;
} nev_p_gesture_t;

typedef struct {
    uint8_t mood;
    uint8_t intensity; /* 0..255 */
    uint16_t duration_ms;
} nev_p_mood_t;

typedef struct {
    uint16_t handle;
    uint16_t chunk_seq;
    uint32_t len;
} nev_p_blob_t;

/*
 * A chunk of captured audio.
 *
 * `handle` is first, and must stay first: the bus retains and releases blobs
 * through p.blob.handle, and this works only because the two structs share that
 * initial member. The assert below pins it — a field inserted above it would
 * otherwise leak every audio buffer the microphone ever produced.
 */
typedef struct {
    uint16_t handle;
    uint16_t seq;     /* within this capture                         */
    uint32_t session; /* groups chunks into one utterance or meeting */
    uint16_t samples;
    uint8_t kind;  /* nev_audio_kind_t                            */
    uint8_t final; /* last chunk of this capture                  */
} nev_p_audio_t;

/* What a capture is for. Matches audio_chunk.kind on the wire. */
typedef enum {
    NEV_AUDIO_KIND_NOTE = 0,
    NEV_AUDIO_KIND_TRANSCRIPT = 1,
} nev_audio_kind_t;

typedef struct {
    uint32_t frame;
    uint16_t render_us;
    uint16_t flush_us;
    uint16_t fps_q4; /* fps in 1/16ths, so 30.0 fps reads as 480 */
    uint16_t overruns;
} nev_p_frame_t;

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
    uint8_t charging;
} nev_p_battery_t;

typedef struct {
    uint8_t sub_index;
    uint8_t reserved;
    uint16_t event_type; /* the type that could not be delivered */
    uint32_t dropped_total;
} nev_p_overflow_t;

typedef struct {
    uint32_t internal_free;
    uint32_t psram_free;
    uint32_t internal_low_water;
} nev_p_heap_t;

typedef struct {
    uint32_t score;
    uint32_t best;
    uint16_t app_id;
} nev_p_score_t;

typedef union {
    uint8_t raw[NEV_EVENT_PAYLOAD_BYTES];
    int32_t i32[4];
    uint32_t u32[4];
    float f32[4];

    nev_p_touch_t touch;
    nev_p_button_t button;
    nev_p_gesture_t gesture;
    nev_p_mood_t mood;
    nev_p_blob_t blob;
    nev_p_audio_t audio;
    nev_p_frame_t frame;
    nev_p_battery_t battery;
    nev_p_overflow_t overflow;
    nev_p_heap_t heap;
    nev_p_score_t score;
} nev_payload_t;

/* --------------------------------------------------------------- the event */

typedef struct {
    uint16_t type;  /* NEV_EVT_*                                  */
    uint8_t flags;  /* NEV_EVF_*                                  */
    uint8_t source; /* enum nev_source — publisher, for tracing   */
    uint32_t seq;   /* assigned by the bus, monotonic             */
    uint64_t ts_us; /* assigned by the bus, monotonic since boot  */
    nev_payload_t p;
} nev_event_t;

_Static_assert(sizeof(nev_payload_t) == NEV_EVENT_PAYLOAD_BYTES, "payload must stay 16 bytes");
_Static_assert(sizeof(nev_event_t) == 32, "event must stay 32 bytes");
_Static_assert(_Alignof(nev_event_t) == 8, "event alignment changed");

/* Every payload struct must fit. Add yours here when you add one. */
#define NEV_PAYLOAD_FITS(T) _Static_assert(sizeof(T) <= NEV_EVENT_PAYLOAD_BYTES, #T " too large")
NEV_PAYLOAD_FITS(nev_p_touch_t);
NEV_PAYLOAD_FITS(nev_p_button_t);
NEV_PAYLOAD_FITS(nev_p_gesture_t);
NEV_PAYLOAD_FITS(nev_p_mood_t);
NEV_PAYLOAD_FITS(nev_p_blob_t);
NEV_PAYLOAD_FITS(nev_p_audio_t);
_Static_assert(offsetof(nev_p_audio_t, handle) == offsetof(nev_p_blob_t, handle),
               "the bus refcounts blobs through p.blob.handle; audio must share that offset");
NEV_PAYLOAD_FITS(nev_p_frame_t);
NEV_PAYLOAD_FITS(nev_p_battery_t);
NEV_PAYLOAD_FITS(nev_p_overflow_t);
NEV_PAYLOAD_FITS(nev_p_heap_t);
NEV_PAYLOAD_FITS(nev_p_score_t);

/* seq and ts_us are filled in by the bus; leave them zero. */
static inline nev_event_t nev_event_make(uint16_t type, uint8_t source) {
    nev_event_t ev = {0};
    ev.type = type;
    ev.source = source;
    return ev;
}

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_EVENT_H */
