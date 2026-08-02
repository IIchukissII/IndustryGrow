/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_WATCHDOG_H
#define IGROW_PLATFORM_WATCHDOG_H

#include <stdint.h>

/* Independent watchdog (IWDG) plus reset-cause reporting, for every node type.
 *
 * The IWDG runs off the LSI, so it survives a stalled or misconfigured main
 * clock -- the failure a WWDG on the APB bus would not catch. It recovers a
 * hung node; it cannot prevent an externally asserted reset (a debug probe on
 * NRST, or a brown-out), which is what the reset cause is for. */

/* Latch and CLEAR the reset-cause flags. Call once, before anything else --
 * the flags are sticky and accumulate until cleared, so leaving them uncleared
 * makes every later reading the union of all resets since power-on rather than
 * the cause of the last one. */
void watchdog_capture_reset_cause(void);

/* RCC_CSR bits 31..24 as latched at boot, for the Heartbeat's
 * vendor_specific_status_code: the gateway then sees WHY a node restarted
 * without anyone attaching a debugger. */
uint8_t watchdog_reset_cause(void);

/* Human-readable form of the dominant flag, for the bring-up console. */
const char *watchdog_reset_cause_str(void);

/* Start the IWDG. Call AFTER init: probing I2C, the ATECC and the CAN
 * self-test are slow and must not race the first timeout. Once started the
 * IWDG cannot be stopped -- by design; that is what makes it independent. */
void watchdog_start(void);

/* Reload the counter. Call from exactly one place in the main loop: kicking
 * from an interrupt would keep a hung foreground alive and defeat the point. */
void watchdog_kick(void);

#endif /* IGROW_PLATFORM_WATCHDOG_H */
