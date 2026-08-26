/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_FLASH_H
#define IGROW_PLATFORM_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* STM32F4 internal-flash erase and program, for the one store the node writes
 * at runtime: the Node-ID sector of ADR-0027 d2.
 *
 * Two properties make this different from a normal driver call.
 *
 * The flash controller stalls every flash READ for the whole duration of an
 * erase or a program (RM0090 3.6.1), so code fetched from flash does not run
 * while one is in progress -- including interrupt handlers. A 128 KB sector
 * erase is of the order of a second, and the IWDG window is ~1.4 s at
 * worst-case LSI (common/platform/watchdog.c). Both routines below therefore
 * execute from RAM with interrupts masked and reload the watchdog themselves
 * while they poll BSY.
 *
 * SysTick is masked with everything else, so millis()/micros64() lose the
 * duration of the operation. That is accepted rather than corrected: the only
 * caller is provisioning, which asks for a restart in the same breath.
 */

/* Erase one sector by its STM32 sector number. Returns 0 on success. */
int flash_erase_sector(uint8_t sector);

/* Program `count` 32-bit words at `addr` (word-aligned, inside an erased
 * region). Returns 0 on success. */
int flash_program_words(uint32_t addr, const uint32_t *words, size_t count);

#endif /* IGROW_PLATFORM_FLASH_H */
