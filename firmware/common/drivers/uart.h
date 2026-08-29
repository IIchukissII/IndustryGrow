/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_UART_H
#define IGROW_DRIVERS_UART_H

#include <stdint.h>

/* Debug console on USART1 (PA9/PA10, AF7), 8N1 at E0001_DBG_BAUD. Bench only. */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/* Small formatters (no newlib printf / syscalls pulled in). */
/* Block until the last character has left the shift register. Needed before
 * anything changes the clock the baud rate is derived from. */
void uart_flush(void);

void uart_put_u32(uint32_t v);
void uart_put_bin3(uint8_t v); /* low 3 bits, e.g. module-ID strap pattern */

#endif /* IGROW_DRIVERS_UART_H */
