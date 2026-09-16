/* Device randomness: the SoC's hardware RNG. */
#include "nev_port/nev_rand.h"

#include "esp_random.h"

void nev_rand_fill(uint8_t *out, size_t len) {
    if (!out || len == 0) return;
    /*
     * esp_fill_random is only a true RNG once Wi-Fi or Bluetooth is running —
     * before that the entropy source is a ring oscillator, which the ESP-IDF
     * documentation warns is not cryptographically strong on its own.
     *
     * Both callers here are fine with that: the WebSocket mask does not need
     * to be unpredictable, and the pairing code is generated after the device
     * has associated with Wi-Fi, since there is nothing to pair with before it
     * has. If a secret is ever needed earlier than that, it must not come from
     * here.
     */
    esp_fill_random(out, len);
}
