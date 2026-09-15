#include "nev_port/nev_sync.h"
#include "nev_port/nev_assert.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Mutexes are recursive to match the host implementation, so a re-entrant call
 * behaves identically on both targets rather than deadlocking on one of them.
 */

nev_err_t nev_mutex_init(nev_mutex_t *m) {
    NEV_REQUIRE(m != NULL, NEV_ERR_INVALID_ARG);
    SemaphoreHandle_t h = xSemaphoreCreateRecursiveMutex();
    if (!h) return NEV_ERR_NO_MEM;
    m->impl = (void *)h;
    return NEV_OK;
}

void nev_mutex_deinit(nev_mutex_t *m) {
    if (!m || !m->impl) return;
    vSemaphoreDelete((SemaphoreHandle_t)m->impl);
    m->impl = NULL;
}

void nev_mutex_lock(nev_mutex_t *m) {
    NEV_ASSERT(m && m->impl);
    xSemaphoreTakeRecursive((SemaphoreHandle_t)m->impl, portMAX_DELAY);
}

void nev_mutex_unlock(nev_mutex_t *m) {
    NEV_ASSERT(m && m->impl);
    xSemaphoreGiveRecursive((SemaphoreHandle_t)m->impl);
}

nev_err_t nev_sem_init(nev_sem_t *s, uint32_t max_count) {
    NEV_REQUIRE(s != NULL && max_count > 0, NEV_ERR_INVALID_ARG);
    SemaphoreHandle_t h = xSemaphoreCreateCounting(max_count, 0);
    if (!h) return NEV_ERR_NO_MEM;
    s->impl = (void *)h;
    return NEV_OK;
}

void nev_sem_deinit(nev_sem_t *s) {
    if (!s || !s->impl) return;
    vSemaphoreDelete((SemaphoreHandle_t)s->impl);
    s->impl = NULL;
}

void nev_sem_give(nev_sem_t *s) {
    NEV_ASSERT(s && s->impl);
    /* Saturated at max_count by FreeRTOS; a failed give means the count is
     * already at its ceiling, which is not an error here. */
    (void)xSemaphoreGive((SemaphoreHandle_t)s->impl);
}

bool nev_sem_take(nev_sem_t *s, uint32_t timeout_ms) {
    NEV_ASSERT(s && s->impl);
    TickType_t ticks;
    if (timeout_ms == NEV_WAIT_FOREVER) {
        ticks = portMAX_DELAY;
    } else if (timeout_ms == NEV_NO_WAIT) {
        ticks = 0;
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
        if (ticks == 0) ticks = 1; /* never silently downgrade a wait to a poll */
    }
    return xSemaphoreTake((SemaphoreHandle_t)s->impl, ticks) == pdTRUE;
}
