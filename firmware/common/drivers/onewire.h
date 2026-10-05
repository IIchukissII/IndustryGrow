/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_ONEWIRE_H
#define IGROW_DRIVERS_ONEWIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 1-Wire master on the header's OW_DATA line, PA0, open drain (E0001 spec 5.2).
 * The pull-up is the module's: E0011 fits 4.7 kOhm. Standard speed, timing per
 * Maxim AN126.
 *
 * Bit-banged. Interrupts are masked for one time slot at a time -- at most
 * 70 us, or 960 us for a reset -- never for a whole byte, so the CAN FIFO and
 * SysTick keep running between slots. A byte costs about 0.6 ms of foreground
 * time; callers that read several devices spread the bytes over passes of the
 * main loop. */
void ow_init(void);

/* Reset pulse; true if at least one device answered with a presence pulse. */
bool ow_reset(void);

void ow_write_byte(uint8_t b);
uint8_t ow_read_byte(void);

/* Dallas/Maxim CRC-8 (x^8 + x^5 + x^4 + 1), as the ROM code and scratchpad use. */
uint8_t ow_crc8(const uint8_t *data, size_t len);

/* Enumerate the bus with the Search ROM algorithm (AN187). Writes up to `max`
 * CRC-valid 64-bit ROM codes, family code in the low byte, and returns how many
 * were found. Blocking, of the order of 15 ms per device -- boot only. */
size_t ow_search(uint64_t *roms, size_t max);

/* Match ROM: address one device by its code. Follows ow_reset(). */
void ow_match_rom(uint64_t rom);

#define OW_CMD_SEARCH_ROM 0xF0u
#define OW_CMD_MATCH_ROM  0x55u
#define OW_CMD_SKIP_ROM   0xCCu

#endif /* IGROW_DRIVERS_ONEWIRE_H */
