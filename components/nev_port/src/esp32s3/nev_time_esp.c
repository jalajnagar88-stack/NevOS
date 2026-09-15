#include "nev_port/nev_time.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint64_t nev_now_us(void) {
    return (uint64_t)esp_timer_get_time();
}

uint32_t nev_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void nev_sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void nev_sleep_until_us(uint64_t deadline_us) {
    const uint64_t now = nev_now_us();
    if (deadline_us <= now) return;
    const uint64_t remain_us = deadline_us - now;

    /*
     * The FreeRTOS tick is 1 ms at best, so a sub-tick remainder cannot be
     * slept through: rounding it up to one tick would overshoot the frame
     * budget it is meant to protect. Yield instead and let the scheduler run
     * whatever else is ready.
     */
    if (remain_us < 1000u) {
        taskYIELD();
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(remain_us / 1000u));
}
