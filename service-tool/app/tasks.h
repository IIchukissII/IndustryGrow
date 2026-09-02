/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The task set, and the one rule that makes it safe.
 *
 *   can   high    Owns the FDCAN interface. The ONLY caller of can_init*(),
 *                 can_tx() and the libcanard instance. Drains the receive ring
 *                 and writes the model. Everything else asks it, by posting a
 *                 request; nothing else touches the interface.
 *   app   medium  Work that blocks: protocol execution, the self-test
 *                 publishers, media import and export.
 *   ui    low     Every LVGL call, and nothing else. Keeping LVGL to one task
 *                 is why no lv_lock() is needed anywhere.
 *
 * Single ownership of the interface is the point. The pin sweep and the
 * self-test both used to re-initialise FDCAN from the same loop, and the sweep
 * silently undid the self-test's mode a few seconds later. Routing every mode
 * change through one owner makes that class of bug unrepresentable rather than
 * merely fixed.
 *
 * The model is written by `can` and read by `ui`, so it is guarded -- see
 * model_lock(). The receive ISR calls no kernel API at all, which is why it may
 * sit above configMAX_SYSCALL_INTERRUPT_PRIORITY and is never masked.
 */
#ifndef IGROW_TASKS_H
#define IGROW_TASKS_H

#include <stdbool.h>
#include <stdint.h>

/* Priorities, low number = low priority (FreeRTOS convention). */
#define TASK_PRIO_UI  2
#define TASK_PRIO_APP 3
#define TASK_PRIO_CAN 5

/* Words, not bytes. The UI task carries LVGL plus the liveness sort array and
 * the plot history buffer, which are a few kB of locals on their own. */
#define TASK_STACK_UI  2048
#define TASK_STACK_APP 1024
#define TASK_STACK_CAN 1024

/* What may be asked of the task that owns the interface. */
typedef enum {
    CAN_REQ_SET_MODE = 0, /* arg0 = profile, arg1 = can_mode_t */
    CAN_REQ_HUNT,         /* arg0 = dwell ms */
    CAN_REQ_GETINFO,      /* arg0 = target node-ID */
    CAN_REQ_RESTART,      /* arg0 = target node-ID */
} can_req_kind_t;

typedef struct {
    can_req_kind_t kind;
    uint32_t       arg0;
    uint32_t       arg1;
} can_req_t;

/* Post a request to the owning task. Safe from any task; not from an ISR.
 * Returns false if the queue is full. */
bool tasks_can_request(const can_req_t *req);

/* Create the tasks and start the scheduler. Does not return. */
void tasks_start(void);

/* Percentage of the last second the CPU spent in the idle task, 0..100.
 * This is the measurement that decides whether a second core is justified. */
uint8_t tasks_cpu_idle_percent(void);

#endif /* IGROW_TASKS_H */
