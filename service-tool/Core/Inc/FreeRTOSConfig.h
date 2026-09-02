/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * FreeRTOS for the panel.
 *
 * The reason for a scheduler here is not tidiness: LVGL's render time is
 * unbounded and the bus has deadlines, and the functions still to be built
 * (protocol execution, media import/export) block for tens of milliseconds.
 * A superloop turns each of those into a hand-rolled state machine, which is
 * what produced the pin-hunt/loopback collision.
 *
 * The SysTick, PendSV and SVC handlers are mapped onto the port's own, so the
 * HAL takes its time base from TIM6 instead (see stm32h7xx_hal_timebase_tim.c).
 * HAL_GetTick() therefore keeps working unchanged, which matters because the
 * model, the liveness thresholds and LVGL all read it.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configMAX_TASK_NAME_LEN                 12
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_TASK_NOTIFICATIONS            1

/* 48 kB in DTCM. Tasks are static in count and created once at start-up, so
 * this covers the queues and the mutex, not a churning allocator. */
#define configTOTAL_HEAP_SIZE                   ((size_t)(48 * 1024))
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

/* Run-time stats: the second core is a real question and it should be settled
 * on a measured number, not a preference. This is how idle time gets counted. */
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (2)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_eTaskGetState                   1

/* Cortex-M7 uses 4 priority bits. Anything numerically at or above
 * configMAX_SYSCALL_INTERRUPT_PRIORITY may call FreeRTOS "FromISR" APIs;
 * anything below it may not, and is never masked by a critical section.
 *
 * The FDCAN receive interrupt deliberately calls NO FreeRTOS API -- it lifts
 * frames into a plain ring and returns -- so it is free to sit above the
 * syscall ceiling and is never delayed by the kernel. */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* An assert that spins in silence is the same mistake as a fault handler that
 * spins in silence: it costs the same to write and tells you nothing. */
extern void console_emergency(const char *msg, const char *file, unsigned line);
#define configASSERT(x) \
    if ((x) == 0) { \
        console_emergency("FreeRTOS assert", __FILE__, __LINE__); \
        taskDISABLE_INTERRUPTS(); \
        for (;;) { \
        } \
    }

/* The port supplies these; the vector table names them the Cortex way. */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
