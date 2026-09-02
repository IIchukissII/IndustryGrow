/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fault reporting. The handler must be `naked`: with a normal prologue the
 * compiler clobbers LR before it can be tested, and the recovered frame points
 * at nothing -- which is exactly the wrong answer delivered confidently.
 */
#include <stdint.h>

#include "console.h"
#include "stm32h7xx_hal.h"

__attribute__((used)) void fault_report(uint32_t *frame, uint32_t exc_return)
{
    const uint32_t cfsr = SCB->CFSR;
    console_printf("\r\nHARDFAULT CFSR=%08lX HFSR=%08lX BFAR=%08lX MMFAR=%08lX EXC=%08lX\r\n",
                   (unsigned long)cfsr, (unsigned long)SCB->HFSR, (unsigned long)SCB->BFAR,
                   (unsigned long)SCB->MMFAR, (unsigned long)exc_return);
    console_printf("  PC=%08lX LR=%08lX PSR=%08lX\r\n", (unsigned long)frame[6],
                   (unsigned long)frame[5], (unsigned long)frame[7]);
    console_printf("  R0=%08lX R1=%08lX R2=%08lX R3=%08lX R12=%08lX\r\n", (unsigned long)frame[0],
                   (unsigned long)frame[1], (unsigned long)frame[2], (unsigned long)frame[3],
                   (unsigned long)frame[4]);
    console_printf("  IPSR=%lu (0 = fault came from thread mode)\r\n",
                   (unsigned long)(frame[7] & 0x1FFU));
    for (;;) {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst   lr, #4        \n"
                   "ite   eq            \n"
                   "mrseq r0, msp       \n"
                   "mrsne r0, psp       \n"
                   "mov   r1, lr        \n"
                   "b     fault_report  \n");
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile("tst   lr, #4        \n"
                   "ite   eq            \n"
                   "mrseq r0, msp       \n"
                   "mrsne r0, psp       \n"
                   "mov   r1, lr        \n"
                   "b     fault_report  \n");
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile("tst   lr, #4        \n"
                   "ite   eq            \n"
                   "mrseq r0, msp       \n"
                   "mrsne r0, psp       \n"
                   "mov   r1, lr        \n"
                   "b     fault_report  \n");
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("tst   lr, #4        \n"
                   "ite   eq            \n"
                   "mrseq r0, msp       \n"
                   "mrsne r0, psp       \n"
                   "mov   r1, lr        \n"
                   "b     fault_report  \n");
}
