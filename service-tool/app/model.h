/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * What the panel has seen. One record per Node-ID, one signal record per
 * (node, subject) pair, each keeping a short history so a plot can be drawn
 * without the UI having to store anything of its own.
 *
 * Everything here is written from the CAN receive path and read from the UI
 * loop. Both run in the same thread (the main loop polls; nothing here is
 * touched from an ISR), so there is no locking and must not be any.
 */
#ifndef IGROW_MODEL_H
#define IGROW_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "subjects.h"

#define MODEL_MAX_NODES   8U
#define MODEL_MAX_SIGNALS 22U  /* one per known subject */
#define MODEL_HISTORY     240U /* samples kept per signal */

/* How alive a signal is, judged against its OWN observed publication rate
 * rather than a fixed timeout -- subjects do not all publish at the same rate,
 * and a fixed number would either nag or miss.
 *
 * This exists because health is not liveness. Defect O-75 had a node reporting
 * uavcan health NOMINAL through 37 minutes of publishing nothing: the node was
 * fine, its I2C bus was wedged, and nothing on the wire said so. The data going
 * quiet is the only signal that catches that. */
typedef enum {
    MODEL_LIVE = 0, /* arriving on time */
    MODEL_LATE,     /* overdue, could still be jitter */
    MODEL_STALE,    /* long gone -- something is wrong */
} model_liveness_t;

typedef struct {
    uint16_t        subject_id;
    igrow_reading_t last;
    uint32_t        count;     /* transfers accepted for this signal */
    uint32_t        last_ms;   /* HAL tick of the last one */
    uint32_t        period_ms; /* observed inter-arrival, smoothed */

    /* Ring of the last MODEL_HISTORY values, oldest first once wrapped.
     * Only IGROW_SIG_SCALAR and IGROW_SIG_STATE fill it. */
    float    hist[MODEL_HISTORY];
    uint16_t hist_len;  /* valid entries, saturates at MODEL_HISTORY */
    uint16_t hist_head; /* next write position */
    float    hist_min;
    float    hist_max;
} model_signal_t;

typedef struct {
    bool     used;
    uint8_t  node_id;

    /* uavcan.node.Heartbeat */
    bool     have_heartbeat;
    uint32_t uptime;
    uint8_t  health;
    uint8_t  mode;
    uint8_t  vssc;
    uint32_t hb_count;
    uint32_t hb_last_ms;

    /* uavcan.node.GetInfo, when it has been asked for and answered */
    bool     have_name;
    char     name[52];

    uint8_t        n_signals;
    model_signal_t sig[MODEL_MAX_SIGNALS];
} model_node_t;

/* The model is written by the CAN task and read by the UI task. Every access
 * from outside the CAN task takes this; it is held briefly and never across a
 * blocking call. Before the scheduler starts, both are no-ops. */
/* Creates the guard. Must be called immediately before the scheduler starts
 * and never earlier -- see the comment on the implementation. */
void model_lock_init(void);
void model_lock(void);
void model_unlock(void);

void          model_init(void);
/* Existing record for the Node-ID, creating one if there is room. NULL when
 * MODEL_MAX_NODES are already known. */
model_node_t *model_node(uint8_t node_id);
model_node_t *model_node_at(uint8_t index); /* NULL past the end */
uint8_t       model_node_count(void);

void model_on_heartbeat(uint8_t node_id, uint32_t uptime, uint8_t health, uint8_t mode,
                        uint8_t vssc, uint32_t now_ms);
void model_on_reading(uint8_t node_id, uint16_t subject_id, const igrow_reading_t *r,
                      uint32_t now_ms);
void model_on_name(uint8_t node_id, const char *name, uint8_t len);

model_signal_t *model_signal(model_node_t *n, uint16_t subject_id); /* NULL when absent */

/* Read the history oldest-to-newest into `out`, returning how many were
 * written (at most `max`). Copies rather than exposing the ring so the UI
 * cannot be caught mid-wrap. */
uint16_t model_history(const model_signal_t *s, float *out, uint16_t max);

/* Monotonic-ish generation counter, bumped on every accepted transfer. The UI
 * repaints only when it changes. */
uint32_t model_generation(void);

/* Liveness of one signal as of `now_ms`. A signal seen only once has no
 * observed period yet and is judged against a 1 Hz assumption (ADR-0005 d4). */
model_liveness_t model_liveness(const model_signal_t *s, uint32_t now_ms);

/* How many signals across every node are LATE or STALE right now. Drives the
 * header annunciation, which is the whole point: a wedged sensor has to be
 * visible from whichever tab the operator is looking at. */
uint16_t model_overdue_count(uint32_t now_ms, uint16_t *out_stale);

#endif /* IGROW_MODEL_H */
