/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Text output on the board's ST-LINK virtual COM port (USART1, 115200 8N1 --
 * COM4 on the workstation).
 *
 * This exists so the CAN side of the panel is testable with no display at all.
 * If the LCD daughterboard is not the one the BSP expects, or the panel init
 * fails for any reason, the firmware carries on and reports everything here
 * instead: bus state, node list, values. Two unknowns are being brought up at
 * once tomorrow, and neither should be able to block the other.
 *
 * Pins come from the BSP's own COM1_* macros, so they cannot drift from the
 * board definition.
 */
#ifndef IGROW_CONSOLE_H
#define IGROW_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

bool console_init(void);
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void console_write(const char *s);

/* One-line-per-signal dump of everything the model holds. Called on a timer
 * from the main loop when the display is not up, and on demand otherwise. */
void console_dump_model(void);

/* True when the character was consumed by an active line entry, so the command
 * dispatch must offer every character here first. */
bool console_feed_line_input(char c);

/* Drive a protocol run from the console: p show, n pass, f fail, R restart,
 * plus the two attempts V4 requires -- o a step out of order, e an edit of a
 * result already recorded. Both are refused by the executor, not by the UI. */
void console_protocol_command(char c);

/* Banner naming what came up and what did not. */
void console_report_boot(bool display_ok, bool touch_ok, bool joy_ok, bool sdram_ok);

/* The CAN line, reported separately because it can only be true once the
 * interface has actually been brought up. */
void console_report_can(void);

/* Addresses answering on I2C1 (PB6/PB7), the bus the touch controller and the
 * IO expander sit on. The DISPLAY is not on it -- the OTM8009A is configured
 * over the DSI command channel -- so this diagnoses touch, not the panel. */
void console_i2c_scan(void);

/* One command character if the host has sent one, else 0. Exposes nothing the
 * panel's own buttons do not; it exists so a verification can be driven and
 * observed from the same terminal that records it. */
char console_poll_command(void);

/* Write straight to the UART data register, polling the TXE flag. Uses no
 * interrupts, no HAL timeout and no tick -- so it still works from a fault
 * handler or a kernel assert, where console_printf() would hang waiting for a
 * clock that is no longer advancing. */
void console_emergency(const char *msg, const char *file, unsigned line);

#endif /* IGROW_CONSOLE_H */
