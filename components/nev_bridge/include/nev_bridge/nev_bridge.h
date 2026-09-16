/* NEVOS L6 — the link to the companion daemon. */
#ifndef NEV_BRIDGE_NEV_BRIDGE_H
#define NEV_BRIDGE_NEV_BRIDGE_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The bridge finds the daemon, pairs with it once, stays connected, and turns
 * the wire protocol into bus events. It is a peer of the persona rather than a
 * layer above it: nothing calls into the persona or the apps, and they do not
 * call the daemon. Everything the daemon says arrives as a BRIDGE.* event, and
 * anything an app wants to say goes out through the functions below — which is
 * a downward call from L5 to L6 and therefore legal.
 *
 * The device is fully useful with no daemon at all (ADR 0008). Every failure
 * here is a feature being unavailable, never a device that stops working: no
 * Wi-Fi, no daemon on the network, a daemon that refuses the protocol version,
 * a pairing nobody completes — all of them leave the clock, the face, the games
 * and the timer exactly as they were.
 */

typedef enum {
    NEV_BRIDGE_OFFLINE = 0, /* no network, or not started */
    NEV_BRIDGE_SEARCHING,   /* asking who serves _nevos._tcp */
    NEV_BRIDGE_CONNECTING,  /* socket and WebSocket handshake */
    NEV_BRIDGE_PAIRING,     /* showing a code, waiting for a human */
    NEV_BRIDGE_READY,       /* paired and connected */
} nev_bridge_state_t;

/* Reads the stored token and device id; generates the id on first boot. */
void nev_bridge_init(void);

/*
 * Advances the link. Called from the network task's loop, never from the
 * render task: it does socket work, and it does it without blocking.
 */
void nev_bridge_poll(void);

void nev_bridge_stop(void);

nev_bridge_state_t nev_bridge_state(void);

/* The daemon's name, for the status bar. "" when not connected. */
const char *nev_bridge_daemon_name(void);

/* The six digits to show while pairing, or NULL when not pairing. */
const char *nev_bridge_pairing_code(void);

/* This device's stable id, as the daemon knows it. */
const char *nev_bridge_device_id(void);

/* True once the device has a token — it may still be offline. */
bool nev_bridge_is_paired(void);

/*
 * Asks the agent. `turn` increments per question; the daemon echoes it on every
 * reply, so a late answer to an abandoned question can be recognised and
 * dropped. Returns false when the link is busy or down, and the caller decides
 * what that means: an app should say so rather than silently lose the question.
 */
bool nev_bridge_ask(uint32_t turn, const char *text, const char *app);

/*
 * Sends one chunk of captured audio. Returns false when the link cannot take it
 * right now, which for audio means drop it and carry on — a stalled capture is
 * worse than a gap, and the daemon is told about gaps by the sequence number.
 */
bool nev_bridge_send_audio(uint32_t session, uint32_t seq, bool final, const int16_t *pcm,
                           size_t samples);

/* Forgets the pairing. The next connection starts over with a new code. */
void nev_bridge_forget(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_BRIDGE_NEV_BRIDGE_H */
