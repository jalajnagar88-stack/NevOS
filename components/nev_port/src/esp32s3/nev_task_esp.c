#include "nev_port/nev_task.h"
#include "nev_port/nev_assert.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * nev_prio_t is already expressed on FreeRTOS's scale (0 = idle, ascending
 * urgency) and configMAX_PRIORITIES is 25 by default on ESP-IDF, so the values
 * pass through. The clamp is here so that raising a NEVOS priority later cannot
 * silently exceed the RTOS ceiling.
 */
static UBaseType_t to_rtos_priority(nev_prio_t p) {
    UBaseType_t v = (UBaseType_t)p;
    return v >= configMAX_PRIORITIES ? (UBaseType_t)(configMAX_PRIORITIES - 1) : v;
}

nev_err_t nev_task_create(nev_task_t *out, const nev_task_cfg_t *cfg) {
    NEV_REQUIRE(out && cfg && cfg->fn && cfg->name, NEV_ERR_INVALID_ARG);

    TaskHandle_t handle = NULL;

    /*
     * ESP-IDF's xTaskCreate takes the stack depth in BYTES, unlike vanilla
     * FreeRTOS which takes words. Their own header says so explicitly ("Note
     * that this differs from vanilla FreeRTOS"). Dividing by sizeof(StackType_t)
     * here would hand every task a quarter of the stack it asked for, which
     * shows up as an overflow much later and somewhere unrelated.
     *
     * Note the asymmetry with nev_task_stack_high_water below: the high-water
     * query still returns words, because ESP-IDF left that one alone.
     */
    const uint32_t depth_bytes = (uint32_t)cfg->stack_bytes;

    BaseType_t rc;
    if (cfg->core == NEV_CORE_ANY) {
        rc = xTaskCreate(cfg->fn, cfg->name, depth_bytes, cfg->arg, to_rtos_priority(cfg->priority),
                         &handle);
    } else {
        rc = xTaskCreatePinnedToCore(cfg->fn, cfg->name, depth_bytes, cfg->arg,
                                     to_rtos_priority(cfg->priority), &handle,
                                     (BaseType_t)cfg->core);
    }
    if (rc != pdPASS) return NEV_ERR_NO_MEM;

    out->impl = (void *)handle;
    return NEV_OK;
}

void nev_task_yield(void) {
    taskYIELD();
}

size_t nev_task_stack_high_water(const nev_task_t *t) {
    if (!t || !t->impl) return 0;
    /* Returned in words (prvTaskCheckFreeStackSpace divides by sizeof(StackType_t));
     * convert so callers see bytes on both targets. */
    return (size_t)uxTaskGetStackHighWaterMark((TaskHandle_t)t->impl) * sizeof(StackType_t);
}

const char *nev_task_self_name(void) {
    return pcTaskGetName(NULL);
}
