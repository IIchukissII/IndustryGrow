/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "model.h"

#include <string.h>

#include "FreeRTOS.h"
#include "panel_mem.h"
#include "semphr.h"

/* ~190 kB of node records. Too big for internal SRAM once LVGL has its draw
 * buffers, and it is written once per transfer and read once per repaint, so
 * SDRAM is fast enough. NOLOAD: model_init() must clear it. */
static model_node_t s_nodes[MODEL_MAX_NODES] PANEL_SDRAM_BSS;
static uint32_t     s_generation;

static SemaphoreHandle_t s_mutex;

/*
 * Creating a FreeRTOS object takes a critical section, and before the scheduler
 * runs that is a one-way door: the port initialises uxCriticalNesting to
 * 0xaaaaaaaa and only zeroes it in xPortStartScheduler(), so vPortExitCritical()
 * decrements to 0xaaaaaaaa, never reaches zero, and never re-enables interrupts.
 * BASEPRI stays masked at the syscall priority, which masks the TIM6 tick at
 * priority 15, which freezes HAL_GetTick(), which hangs the next HAL_Delay().
 *
 * That is exactly how this bricked the LCD bring-up when the mutex was created
 * in model_init(). So it is created here instead, called immediately before
 * vTaskStartScheduler(), where nothing afterwards needs an interrupt.
 *
 * Recursive: the CAN task holds it while draining, and the console dump reaches
 * back into the model from inside that.
 */
void model_lock_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

void model_lock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    }
}

void model_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_mutex);
    }
}

/* Creates no kernel object -- see model_lock_init() for why that matters. It is
 * also called at run time (entering and leaving the self-test), when creating
 * one would be pointless anyway. */
void model_init(void)
{
    memset(s_nodes, 0, sizeof s_nodes);
    s_generation = 0;
}

uint32_t model_generation(void)
{
    return s_generation;
}

model_node_t *model_node(uint8_t node_id)
{
    for (unsigned i = 0; i < MODEL_MAX_NODES; i++) {
        if (s_nodes[i].used && (s_nodes[i].node_id == node_id)) {
            return &s_nodes[i];
        }
    }
    for (unsigned i = 0; i < MODEL_MAX_NODES; i++) {
        if (!s_nodes[i].used) {
            memset(&s_nodes[i], 0, sizeof s_nodes[i]);
            s_nodes[i].used    = true;
            s_nodes[i].node_id = node_id;
            return &s_nodes[i];
        }
    }
    return NULL; /* the bus has more nodes than the panel was built for */
}

model_node_t *model_node_at(uint8_t index)
{
    uint8_t seen = 0;
    for (unsigned i = 0; i < MODEL_MAX_NODES; i++) {
        if (s_nodes[i].used) {
            if (seen == index) {
                return &s_nodes[i];
            }
            seen++;
        }
    }
    return NULL;
}

uint8_t model_node_count(void)
{
    uint8_t n = 0;
    for (unsigned i = 0; i < MODEL_MAX_NODES; i++) {
        if (s_nodes[i].used) {
            n++;
        }
    }
    return n;
}

model_signal_t *model_signal(model_node_t *n, uint16_t subject_id)
{
    if (n == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < n->n_signals; i++) {
        if (n->sig[i].subject_id == subject_id) {
            return &n->sig[i];
        }
    }
    return NULL;
}

static model_signal_t *signal_get(model_node_t *n, uint16_t subject_id)
{
    model_signal_t *s = model_signal(n, subject_id);
    if (s != NULL) {
        return s;
    }
    if (n->n_signals >= MODEL_MAX_SIGNALS) {
        return NULL;
    }
    s = &n->sig[n->n_signals++];
    memset(s, 0, sizeof *s);
    s->subject_id = subject_id;
    return s;
}

void model_on_heartbeat(uint8_t node_id, uint32_t uptime, uint8_t health, uint8_t mode,
                        uint8_t vssc, uint32_t now_ms)
{
    model_node_t *n = model_node(node_id);
    if (n == NULL) {
        return;
    }
    n->have_heartbeat = true;
    n->uptime         = uptime;
    n->health         = health;
    n->mode           = mode;
    n->vssc           = vssc;
    n->hb_count++;
    n->hb_last_ms = now_ms;
    s_generation++;
}

void model_on_name(uint8_t node_id, const char *name, uint8_t len)
{
    model_node_t *n = model_node(node_id);
    if ((n == NULL) || (name == NULL)) {
        return;
    }
    if (len >= sizeof n->name) {
        len = (uint8_t)(sizeof n->name - 1U);
    }
    memcpy(n->name, name, len);
    n->name[len]  = '\0';
    n->have_name  = true;
    s_generation++;
}

void model_on_reading(uint8_t node_id, uint16_t subject_id, const igrow_reading_t *r,
                      uint32_t now_ms)
{
    model_node_t *n = model_node(node_id);
    if (n == NULL) {
        return;
    }
    model_signal_t *s = signal_get(n, subject_id);
    if (s == NULL) {
        return;
    }

    /* Observed period, updated BEFORE last_ms is overwritten. Smoothed 3:1 so
     * one late arrival does not redefine what "on time" means. */
    if (s->count > 0U) {
        const uint32_t interval = now_ms - s->last_ms;
        s->period_ms = (s->period_ms == 0U) ? interval : (((s->period_ms * 3U) + interval) / 4U);
    }

    s->last    = *r;
    s->last_ms = now_ms;
    s->count++;

    if (r->have_value) {
        s->hist[s->hist_head] = r->value;
        s->hist_head          = (uint16_t)((s->hist_head + 1U) % MODEL_HISTORY);
        if (s->hist_len < MODEL_HISTORY) {
            s->hist_len++;
        }
        /* Recompute the extremes over what is still in the ring. An
         * incremental min/max would drift once the extreme scrolls out. */
        float lo = s->hist[0];
        float hi = s->hist[0];
        for (uint16_t i = 0; i < s->hist_len; i++) {
            const float v = s->hist[i];
            if (v < lo) {
                lo = v;
            }
            if (v > hi) {
                hi = v;
            }
        }
        s->hist_min = lo;
        s->hist_max = hi;
    }
    s_generation++;
}

/* Clamped so a burst of fast arrivals cannot make a normal gap look stale, and
 * a very slow subject cannot make a genuine outage look fine. */
#define PERIOD_MIN_MS 250U
#define PERIOD_MAX_MS 60000U
#define PERIOD_ASSUMED_MS 1000U /* ADR-0005 d4: 1 Hz until observed otherwise */

model_liveness_t model_liveness(const model_signal_t *s, uint32_t now_ms)
{
    if ((s == NULL) || (s->count == 0U)) {
        return MODEL_STALE;
    }
    uint32_t period = (s->period_ms == 0U) ? PERIOD_ASSUMED_MS : s->period_ms;
    if (period < PERIOD_MIN_MS) {
        period = PERIOD_MIN_MS;
    } else if (period > PERIOD_MAX_MS) {
        period = PERIOD_MAX_MS;
    }
    const uint32_t age = now_ms - s->last_ms;
    if (age <= ((period * 5U) / 2U)) {
        return MODEL_LIVE;
    }
    if (age <= (period * 6U)) {
        return MODEL_LATE;
    }
    return MODEL_STALE;
}

uint16_t model_overdue_count(uint32_t now_ms, uint16_t *out_stale)
{
    uint16_t overdue = 0;
    uint16_t stale   = 0;
    for (unsigned i = 0; i < MODEL_MAX_NODES; i++) {
        if (!s_nodes[i].used) {
            continue;
        }
        for (uint8_t k = 0; k < s_nodes[i].n_signals; k++) {
            const model_liveness_t l = model_liveness(&s_nodes[i].sig[k], now_ms);
            if (l != MODEL_LIVE) {
                overdue++;
            }
            if (l == MODEL_STALE) {
                stale++;
            }
        }
    }
    if (out_stale != NULL) {
        *out_stale = stale;
    }
    return overdue;
}

uint16_t model_history(const model_signal_t *s, float *out, uint16_t max)
{
    if ((s == NULL) || (out == NULL) || (max == 0U)) {
        return 0;
    }
    const uint16_t n = (s->hist_len < max) ? s->hist_len : max;
    /* Walk back n from the head so the newest sample lands last. */
    uint16_t idx =
        (uint16_t)((s->hist_head + MODEL_HISTORY - n) % MODEL_HISTORY);
    for (uint16_t i = 0; i < n; i++) {
        out[i] = s->hist[idx];
        idx    = (uint16_t)((idx + 1U) % MODEL_HISTORY);
    }
    return n;
}
