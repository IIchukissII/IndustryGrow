/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_CLOCK_H
#define IGROW_PLATFORM_CLOCK_H

#include <stdint.h>

/* Configure SYSCLK = 168 MHz from the 8 MHz HSE (WeAct board), set bus
 * prescalers (AHB/1 = 168, APB1/4 = 42 -> CAN1, APB2/2 = 84 -> USART1), flash
 * latency, and start a 1 kHz SysTick. Call first, before any peripheral. */
void clock_init(void);

uint32_t millis(void);
void delay_ms(uint32_t ms);

/* Monotonic 64-bit microsecond clock (SysTick-derived). Used for libcanard
 * transfer-id timeouts and TX deadlines. */
uint64_t micros64(void);

#endif /* IGROW_PLATFORM_CLOCK_H */
