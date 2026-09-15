/*
 * NEVOS L3 — persona: the mood machine bound to the bus and to the face.
 *
 * This file is the only part of the persona that knows about either. It
 * consumes events and drives the renderer; it exposes one imperative call
 * upward and nothing sideways.
 */
#include "nev_persona/persona.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_time.h"
#include <string.h>

#define TAG             "persona"

/* A burst of input must never eat the frame budget (ARCHITECTURE.md R3). */
#define EVENTS_PER_TICK 8

/* Below this the device is worried about itself. */
#define BATTERY_LOW_PCT 15

static nev_persona_core_t s_core;
static const nev_face_renderer_t *s_renderer;
static nev_sub_t *s_sub;
static bool s_ready;
static nev_mood_t s_last_published = NEV_MOOD_COUNT;

static void publish_mood_changed(nev_mood_t mood, uint8_t intensity) {
    nev_event_t ev = nev_event_make(NEV_EVT_PERSONA_MOOD_CHANGED, NEV_SRC_PERSONA);
    ev.p.mood.mood = (uint8_t)mood;
    ev.p.mood.intensity = intensity;
    (void)nev_bus_publish(&ev);
}

nev_err_t nev_persona_init(lv_obj_t *parent) {
    if (s_ready) return NEV_OK;

    s_renderer = nev_face_renderer_at(0);
    if (!s_renderer) return NEV_ERR_NOT_FOUND;
    NEV_TRY(s_renderer->create(parent));

    /*
     * DROP_OLDEST with coalescing: the persona cares about the latest state of
     * the world, not about replaying every gesture that happened while it was
     * busy rendering. A backlog of stale taps would make the face act out
     * things the user did seconds ago.
     */
    const nev_sub_cfg_t cfg = {
        .name = "persona",
        .domains = NEV_DOM(INPUT) | NEV_DOM(POWER) | NEV_DOM(GAME) | NEV_DOM(BRIDGE),
        .depth = 12,
        .full_policy = NEV_FULL_DROP_OLDEST,
        .coalesce = true,
    };
    s_sub = nev_bus_subscribe(&cfg);
    if (!s_sub) {
        s_renderer->destroy();
        return NEV_ERR_NO_SPACE;
    }

    nev_persona_core_init(&s_core, 0, 0xA5C3179Fu);
    s_ready = true;
    NEV_LOGI(TAG, "face '%s' ready", s_renderer->name);
    return NEV_OK;
}

void nev_persona_deinit(void) {
    if (!s_ready) return;
    if (s_renderer) s_renderer->destroy();
    s_renderer = NULL;
    s_sub = NULL;
    s_ready = false;
    s_last_published = NEV_MOOD_COUNT;
}

void nev_persona_set_mood(nev_mood_t mood, uint8_t intensity, uint32_t hold_ms) {
    if (!s_ready) return;
    nev_persona_core_set_mood(&s_core, mood, intensity, hold_ms, nev_now_ms());
}

nev_mood_t nev_persona_mood(void) {
    return nev_persona_core_mood(&s_core);
}

/*
 * How the world makes NEVOS feel. This table is the persona's half of the
 * product: everything else here is plumbing.
 */
static void handle_event(const nev_event_t *ev, uint32_t now_ms) {
    switch (ev->type) {
        /* Picked up or knocked — startled, then back to whatever it was doing. */
        case NEV_EVT_INPUT_GESTURE_SHAKE:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_CURIOUS, 255, 1600, now_ms);
            break;

        /* Tapped on the face. Intensity scales with how firm the tap was, so a
         * brush gets a flicker and a poke gets a grin. */
        case NEV_EVT_INPUT_GESTURE_TAP:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_HAPPY,
                                      (uint8_t)(140 + ev->p.gesture.strength / 2), 2200, now_ms);
            break;

        case NEV_EVT_POWER_IDLE_ENTER:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_SLEEPY, 255, 0, now_ms);
            break;
        case NEV_EVT_POWER_IDLE_EXIT:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_IDLE, 255, 0, now_ms);
            break;

        case NEV_EVT_POWER_BATTERY:
            /* Only while unplugged: being low and charging is not a worry. */
            if (!ev->p.battery.charging && ev->p.battery.percent < BATTERY_LOW_PCT) {
                nev_persona_core_set_mood(&s_core, NEV_MOOD_CONCERNED, 255, 5000, now_ms);
            }
            break;

        case NEV_EVT_GAME_HIGHSCORE_BEAT:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_CELEBRATING, 255, 3200, now_ms);
            break;
        case NEV_EVT_GAME_OVER:
            /* Not concern: losing a two-minute arcade game is not a crisis. */
            nev_persona_core_set_mood(&s_core, NEV_MOOD_CURIOUS, 170, 1800, now_ms);
            break;

        /* The agent is working. Thinking rests rather than holds, because the
         * daemon says when it is done. */
        case NEV_EVT_BRIDGE_AGENT_TOKEN:
            if (nev_persona_core_mood(&s_core) != NEV_MOOD_THINKING) {
                nev_persona_core_set_mood(&s_core, NEV_MOOD_THINKING, 255, 0, now_ms);
            }
            break;
        case NEV_EVT_BRIDGE_AGENT_DONE:
            nev_persona_core_set_mood(&s_core, NEV_MOOD_IDLE, 255, 0, now_ms);
            break;

        /* The daemon's tone drives the face, so the agent's mood and the
         * device's mood are the same thing to the user. */
        case NEV_EVT_BRIDGE_MOOD_HINT:
            nev_persona_core_set_mood(&s_core, (nev_mood_t)ev->p.mood.mood, ev->p.mood.intensity,
                                      ev->p.mood.duration_ms, now_ms);
            break;

        default:
            break;
    }
}

void nev_persona_tick(uint32_t now_ms) {
    if (!s_ready) return;

    nev_event_t ev;
    int budget = EVENTS_PER_TICK;
    while (budget-- > 0 && nev_bus_recv(s_sub, &ev, NEV_NO_WAIT)) {
        handle_event(&ev, now_ms);
        if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
    }

    s_renderer->apply(nev_persona_core_tick(&s_core, now_ms));

    const nev_mood_t mood = nev_persona_core_mood(&s_core);
    if (mood != s_last_published) {
        s_last_published = mood;
        publish_mood_changed(mood, s_core.intensity);
    }
}
