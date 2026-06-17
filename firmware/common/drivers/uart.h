/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_UART_H
#define IGROW_DRIVERS_UART_H

#include <stdint.h>

/* Debug console on USART1 (PA9/PA10, AF7), 8N1 at BRD_DBG_BAUD. Bench only. */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/* Small formatters (no newlib printf / syscalls pulled in). */
void uart_put_u32(uint32_t v);
void uart_put_bin3(uint8_t v); /* low 3 bits, e.g. module-ID strap pattern */

#endif /* IGROW_DRIVERS_UART_H */
