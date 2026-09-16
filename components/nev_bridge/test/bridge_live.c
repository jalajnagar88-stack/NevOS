/*
 * The bridge against a real daemon.
 *
 * Not a unit test: it needs nevosd running on this machine. It is the thing
 * that proves the parts fit — mDNS discovery, the WebSocket handshake, the
 * pairing round trip, an agent turn arriving as bus events — using the same
 * code the device runs, because there is no device-specific code in the path.
 *
 *   ./tools/build.sh bridge-live
 *
 * It completes the pairing by calling the daemon's control API itself, which
 * is what the desktop tray does when a human types the code.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "nev_bridge/nev_bridge.h"
#include "nev_kernel/nev_blob.h"
#include "nev_kernel/nev_bus.h"
#include "nev_kernel/nev_store.h"
#include "nev_port/nev_log.h"
#include "nev_port/nev_net.h"
#include "nev_port/nev_time.h"

#define CONTROL_PORT    4822
#define STEP_TIMEOUT_MS 20000

static int fail(const char *what) {
    printf("FAIL: %s\n", what);
    return 1;
}

/*
 * POSTs the pairing code to the daemon's loopback control API.
 *
 * Blocking, briefly, which nothing in the firmware may do — this is the test
 * harness standing in for a person at a keyboard, not device code.
 */
static bool enter_code_on_the_daemon(const char *code) {
    char body[64];
    int body_len = snprintf(body, sizeof(body), "{\"code\":\"%s\"}", code);

    char request[256];
    int n = snprintf(request, sizeof(request),
                     "POST /api/pair HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n%s",
                     body_len, body);

    nev_socket_t sock = nev_net_connect("127.0.0.1", CONTROL_PORT);
    if (sock == NEV_SOCKET_INVALID) return false;

    uint32_t started = nev_now_ms();
    while (nev_net_connect_poll(sock) == NEV_CONN_PENDING) {
        if (nev_elapsed_ms(started) > 2000) {
            nev_net_close(sock);
            return false;
        }
        nev_sleep_ms(5);
    }

    int sent = 0;
    while (sent < n) {
        int wrote = nev_net_send(sock, (const uint8_t *)request + sent, (size_t)(n - sent));
        if (wrote < 0) {
            nev_net_close(sock);
            return false;
        }
        sent += wrote;
        if (wrote == 0) nev_sleep_ms(5);
    }

    char reply[256] = {0};
    size_t got = 0;
    started = nev_now_ms();
    while (got + 1 < sizeof(reply) && nev_elapsed_ms(started) < 2000) {
        int r = nev_net_recv(sock, (uint8_t *)reply + got, sizeof(reply) - got - 1);
        if (r < 0) break;
        if (r == 0) {
            nev_sleep_ms(5);
            continue;
        }
        got += (size_t)r;
    }
    nev_net_close(sock);
    return strstr(reply, "\"granted\":true") != NULL;
}

/* Runs the bridge until `want` is reached or the clock runs out. */
static bool pump_until(nev_bridge_state_t want, nev_sub_t *sub) {
    uint32_t started = nev_now_ms();
    while (nev_elapsed_ms(started) < STEP_TIMEOUT_MS) {
        nev_bridge_poll();

        nev_event_t ev;
        while (nev_bus_recv(sub, &ev, 0)) {
            printf("  event %s\n", nev_evt_name(ev.type));
            if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
        }
        if (nev_bridge_state() == want) return true;
        nev_sleep_ms(10);
    }
    return false;
}

int main(void) {
    char store_path[256];
    snprintf(store_path, sizeof(store_path), "/tmp/nevos-bridge-live-%d.json", (int)getpid());
    remove(store_path);

    if (nev_bus_init() != NEV_OK) return fail("bus");
    if (nev_blob_pool_init() != NEV_OK) return fail("blob pool");
    if (nev_store_init(store_path) != NEV_OK) return fail("store");

    nev_sub_cfg_t cfg = {
        .name = "live",
        .domains = NEV_DOM_ALL,
        .depth = 32,
    };
    nev_sub_t *sub = nev_bus_subscribe(&cfg);
    if (!sub) return fail("subscribe");

    nev_bridge_init();
    printf("device id: %s\n", nev_bridge_device_id());

    printf("discovering and connecting...\n");
    if (!pump_until(NEV_BRIDGE_PAIRING, sub)) return fail("no pairing prompt (is nevosd running?)");

    const char *code = nev_bridge_pairing_code();
    if (!code) return fail("no pairing code");
    printf("pairing code on screen: %s\n", code);
    printf("daemon: %s\n", nev_bridge_daemon_name());

    /* Before anyone enters it, the daemon must not have paired us. */
    if (nev_bridge_is_paired()) return fail("paired before the code was entered");

    if (!enter_code_on_the_daemon(code)) return fail("the daemon refused the code");
    printf("code entered on the daemon\n");

    if (!pump_until(NEV_BRIDGE_READY, sub)) return fail("never became ready");
    if (!nev_bridge_is_paired()) return fail("ready but no token stored");
    printf("paired and connected\n");

    /* The wall clock comes from the daemon; before this it was unset. */
    if (!nev_wallclock_is_set()) return fail("the daemon did not set the clock");
    printf("clock set: %llu\n", (unsigned long long)nev_wallclock_now());

    printf("asking the agent...\n");
    if (!nev_bridge_ask(1, "is the kettle on?", "agent"))
        return fail("could not send the question");

    bool got_token = false, got_done = false, got_mood = false;
    uint32_t started = nev_now_ms();
    char reply[512] = {0};
    while (!got_done && nev_elapsed_ms(started) < STEP_TIMEOUT_MS) {
        nev_bridge_poll();
        nev_event_t ev;
        while (nev_bus_recv(sub, &ev, 0)) {
            if (ev.type == NEV_EVT_BRIDGE_AGENT_TOKEN) {
                got_token = true;
                strncat(reply, (const char *)nev_blob_data(ev.p.blob.handle),
                        sizeof(reply) - strlen(reply) - 1);
            } else if (ev.type == NEV_EVT_BRIDGE_AGENT_DONE) {
                got_done = true;
            } else if (ev.type == NEV_EVT_BRIDGE_MOOD_HINT) {
                got_mood = true;
                printf("  mood hint: %u\n", (unsigned)ev.p.mood.mood);
            }
            if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
        }
        nev_sleep_ms(10);
    }

    if (!got_token) return fail("no agent tokens arrived");
    if (!got_done) return fail("the turn never finished");
    printf("reply: %s\n", reply);
    if (!got_mood) printf("  (no mood hint; the reply did not suggest one)\n");

    /* Reconnecting with the stored token must not ask to pair again. */
    printf("reconnecting with the stored token...\n");
    nev_bridge_stop();
    nev_bridge_init();
    if (!pump_until(NEV_BRIDGE_READY, sub)) return fail("could not reconnect with the token");
    printf("reconnected without pairing\n");

    nev_bridge_stop();
    nev_store_deinit();
    nev_bus_deinit();
    nev_blob_pool_deinit();
    remove(store_path);

    printf("\nOK — discovery, pairing, an agent turn, and a reconnect all work.\n");
    return 0;
}
