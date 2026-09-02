/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "tasks.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "can_port.h"
#include "console.h"
#include "cyphal_rx.h"
#include "lvgl_port.h"
#include "eval_board.h"
#include "model.h"
#include "selftest.h"
#include "ui_main.h"

static QueueHandle_t s_can_q;

static void controls_poll(void);

/*
 * HAL_Delay is __weak, and the stock one busy-waits on the tick. Under a
 * scheduler that is wrong twice over: it burns the whole slice at the calling
 * task's priority instead of yielding, and it is reached from inside the BSP
 * (SDRAM, LCD and touch init all delay), so avoiding it at our own call sites
 * would not be enough.
 *
 * Before the scheduler runs there is nothing to yield to, so the polling
 * version is still the right one -- which is the case during board bring-up,
 * where most of the BSP's delays happen.
 */
void HAL_Delay(uint32_t Delay)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        /* Never round down to zero: a caller asking for a delay wants at least
         * one tick, not a no-op. */
        vTaskDelay(pdMS_TO_TICKS(Delay) ? pdMS_TO_TICKS(Delay) : 1U);
        return;
    }
    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < Delay) {
    }
}

/* --- idle measurement -----------------------------------------------------
 * Counts idle-hook entries per second against the count observed when nothing
 * else was running. Crude, but it answers the only question being asked: how
 * much of this core is actually spoken for. */
static volatile uint32_t s_idle_ticks;
static volatile uint32_t s_idle_per_sec;
static volatile uint32_t s_idle_reference = 1;
static volatile uint8_t  s_idle_percent;

void vApplicationIdleHook(void)
{
    s_idle_ticks++;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    console_printf("\r\nSTACK OVERFLOW in task %s\r\n", name);
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    console_printf("\r\nFreeRTOS heap exhausted\r\n");
    for (;;) {
    }
}

uint8_t tasks_cpu_idle_percent(void)
{
    return s_idle_percent;
}

bool tasks_can_request(const can_req_t *req)
{
    if (s_can_q == NULL) {
        return false;
    }
    return xQueueSend(s_can_q, req, 0) == pdPASS;
}

/* --- the task that owns the interface ------------------------------------- */

/* Set by the console, consumed once by the CAN task. Bench instrumentation for
 * SERVICE-TOOL V1 only; nothing in normal operation writes it. */
static volatile bool s_stall_request;

static void can_task(void *arg)
{
    (void)arg;
    TickType_t last_stats = xTaskGetTickCount();

    for (;;) {
        can_req_t req;
        while (xQueueReceive(s_can_q, &req, 0) == pdPASS) {
            switch (req.kind) {
            case CAN_REQ_SET_MODE:
                can_hunt_abort();
                (void)can_init_mode((uint8_t)req.arg0, (can_mode_t)req.arg1);
                break;
            case CAN_REQ_HUNT:
                can_hunt_start(req.arg0);
                break;
            case CAN_REQ_GETINFO:
                (void)cyphal_request_getinfo((uint8_t)req.arg0);
                break;
            case CAN_REQ_RESTART:
                (void)cyphal_send_restart((uint8_t)req.arg0);
                break;
            default:
                break;
            }
        }

        /* Deliberate overflow for V1/S2: stop draining and let the ISR fill the
         * ring past its end, so the lost-frame counter has something to count.
         *
         * Stall until the loss actually happens rather than for a fixed time.
         * How long a 512-entry ring takes to fill depends entirely on the bus
         * rate -- 540 ms at 950 frames/s, but 14 s at the 36 frames/s the two
         * nodes produce on their own -- so a fixed delay either proves nothing
         * or blocks far longer than it needs to. */
        if (s_stall_request) {
            s_stall_request              = false;
            const uint32_t lost_at_start = can_stats()->rx_lost_ring;
            const TickType_t t0          = xTaskGetTickCount();
            bool             overflowed  = false;
            while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(25000)) {
                vTaskDelay(pdMS_TO_TICKS(50));
                if (can_stats()->rx_lost_ring != lost_at_start) {
                    overflowed = true;
                    break;
                }
            }
            console_printf("stall ended after %lu ms: %s\r\n",
                           (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS),
                           overflowed ? "ring overflowed, loss counted" : "no overflow in 25 s");
        }

        /* The model is written here and read by the UI task. */
        model_lock();
        cyphal_rx_spin();
        model_unlock();

        /* The sweep used to end without saying so, and left the interface on
         * whichever pair it tried last -- so a board with no bus reported
         * nothing at all and then sat on candidate 2. Report both outcomes and
         * go back to candidate 0, which is the pair the firmware documents. */
        {
            static can_hunt_state_t   last_hunt = CAN_HUNT_IDLE;
            const can_hunt_state_t    now       = can_hunt_step();
            if (now != last_hunt) {
                if (now == CAN_HUNT_FOUND) {
                    console_printf("CAN frames on %s\r\n",
                                   can_profile_name(can_hunt_current()));
                } else if (now == CAN_HUNT_EXHAUSTED) {
                    console_printf("no CAN frames on any candidate pin pair - "
                                   "back on %s (listen-only)\r\n",
                                   can_profile_name(0));
                    (void)can_init(0, true);
                }
                last_hunt = now;
            }
        }

        if ((xTaskGetTickCount() - last_stats) >= pdMS_TO_TICKS(200)) {
            last_stats = xTaskGetTickCount();
            can_stats_poll();
        }

        /* 2 ms: the ring absorbs bursts, so this only has to keep up on
         * average, and sleeping is what gives the idle measurement meaning. */
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* --- work that blocks ------------------------------------------------------ */

static void app_task(void *arg)
{
    (void)arg;
    TickType_t last_idle = xTaskGetTickCount();
    uint32_t   last_count = 0;

    for (;;) {
        /* Drain what has arrived, not one character per pass. At 115200 a line
         * of input lands in under 2 ms while this loop runs every 5, so reading
         * one character a pass overruns the receiver on any burst -- a pasted
         * identifier, or fast typing. Bounded so a stuck line cannot starve the
         * rest of the task. */
        char cmd;
        for (unsigned drained = 0; drained < 64U; drained++) {
        cmd = console_poll_command();
        if (cmd == 0) {
            break;
        }
        /* Line entry claims every character while it is open, or typing an
         * instance would fire commands letter by letter. */
        if (console_feed_line_input(cmd)) {
            continue;
        }
        if (cmd == 's') {
            (void)selftest_toggle();
        } else if (cmd == 'd') {
            model_lock();
            console_dump_model();
            model_unlock();
        } else if (cmd == 'h') {
            const can_req_t r = {.kind = CAN_REQ_HUNT, .arg0 = 3000U};
            (void)tasks_can_request(&r);
        } else if (cmd == 'c') {
            uint32_t touches = 0;
            uint32_t joys    = 0;
            panel_input_counts(&touches, &joys);
            console_printf("cpu idle %u%%   touch presses %lu   joystick presses %lu\r\n",
                           (unsigned)tasks_cpu_idle_percent(), (unsigned long)touches,
                           (unsigned long)joys);
        } else if (cmd == 'b') {
            const bool on = !panel_display_is_on();
            panel_display_set(on);
            console_printf("display %s\r\n", on ? "on" : "off");
        } else if ((cmd == 'p') || (cmd == 'n') || (cmd == 'f') || (cmd == 'o') ||
                   (cmd == 'e') || (cmd == 'R') || (cmd == 'i') || (cmd == 'Q')) {
            console_protocol_command(cmd);
        } else if (cmd == 'w') {
            const bool on = !lvgl_port_repaint_storm_is_on();
            lvgl_port_repaint_storm(on);
            console_printf("worst-case repaint %s\r\n", on ? "ON" : "off");
        } else if (cmd == 'x') {
            s_stall_request = true;
            console_printf("stalling the receive drain until the ring overflows\r\n");
        } else if (cmd == '?') {
            console_printf("s=self-test  d=dump  h=hunt  c=cpu idle  b=display on/off  "
                           "w=worst-case repaint  x=stall drain\r\n");
            console_printf("protocol:  p=show  n=pass  f=fail  R=restart  i=instance  "
                           "Q=emit record  o=out-of-order attempt  e=edit attempt\r\n");
        }
        }

        selftest_spin();
        controls_poll();

        if ((xTaskGetTickCount() - last_idle) >= pdMS_TO_TICKS(1000)) {
            last_idle      = xTaskGetTickCount();
            s_idle_per_sec = s_idle_ticks - last_count;
            last_count     = s_idle_ticks;
            if (s_idle_per_sec > s_idle_reference) {
                s_idle_reference = s_idle_per_sec; /* the quietest second seen */
            }
            s_idle_percent = (uint8_t)((s_idle_per_sec * 100U) / s_idle_reference);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* --- board controls -------------------------------------------------------
 * Polled from the task that already handles slow work. Both push-buttons blank
 * and restore the display. Neither is part of navigation: the whole UI is
 * reachable from the joystick alone, so the panel does not depend on a button
 * an operator might not find.
 *
 * Debounced the same way as the joystick, and for the same reason: these are
 * mechanical contacts read once per pass. */
#define BTN_DEBOUNCE 2

static void controls_poll(void)
{
    static bool    stable[2];
    static uint8_t agree[2];
    /* The first poll records the level and acts on nothing. Without this a pin
     * that reads as down at startup -- floating, or genuinely held -- is an
     * edge against the zero-initialised state, and the panel blanks its own
     * display on every boot before anyone has touched it. */
    static bool    primed;

    const Button_TypeDef buttons[2] = {BUTTON_WAKEUP, BUTTON_USER};
    /* PC13 is the tamper button; the BSP maps BUTTON_USER to the same pin. */
    const char *const    names[2]   = {"PA0 wakeup", "PC13 tamper"};

    for (unsigned i = 0; i < 2U; i++) {
        const bool down = (BSP_PB_GetState(buttons[i]) == 1);
        if (down == stable[i]) {
            agree[i] = 0;
            continue;
        }
        if (++agree[i] < BTN_DEBOUNCE) {
            continue;
        }
        agree[i]  = 0;
        stable[i] = down;
        /* Act on the press, not the level, or holding it repeats every pass. */
        if (primed && down) {
            const bool on = !panel_display_is_on();
            panel_display_set(on);
            console_printf("button %s: display %s\r\n", names[i], on ? "on" : "off");
        }
    }
    primed = true;
}

/* --- everything LVGL ------------------------------------------------------- */

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        lvgl_port_spin();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void tasks_start(void)
{
    /* Every FreeRTOS object is created here, in the last moments before the
     * scheduler starts, because creating one earlier leaves interrupts masked
     * (see model_lock_init). */
    model_lock_init();

    s_can_q = xQueueCreate(8, sizeof(can_req_t));
    configASSERT(s_can_q != NULL);

    (void)xTaskCreate(can_task, "can", TASK_STACK_CAN, NULL, TASK_PRIO_CAN, NULL);
    (void)xTaskCreate(app_task, "app", TASK_STACK_APP, NULL, TASK_PRIO_APP, NULL);
    (void)xTaskCreate(ui_task, "ui", TASK_STACK_UI, NULL, TASK_PRIO_UI, NULL);

    vTaskStartScheduler();

    /* Only reached if the scheduler could not start. */
    console_printf("scheduler did not start\r\n");
    for (;;) {
    }
}
