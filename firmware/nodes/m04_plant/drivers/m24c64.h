/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M04_M24C64_H
#define IGROW_M04_M24C64_H

#include <stdbool.h>
#include <stdint.h>

/*
 * M24C64-RMN6TP serial EEPROM, U2 on E0005 (M04 spec 4.2): 64 kbit = 8 KB,
 * 16-bit addressing, 32-byte page, 5 ms write cycle, 400 kHz at 3.3 V. Any SO8
 * 24C64 on the JEDEC pinout substitutes.
 *
 * It is a STORE, not a sensor: it measures nothing and publishes nothing. What
 * it holds is the module's own record -- the class ID at byte 0, and the
 * flat-field trim from byte 16 (M04 spec 6.7, ADR-0028 d10). The trim lives
 * here because the MLX90640 has no user cell for 768 corrections and a trim
 * that describes THIS module must not follow the carrier.
 *
 * E0-E2 are strapped to GND, which puts the device at 0x50 -- inside the
 * 0x50-0x57 block ADR-0014 rev 4 d6 reserves, occupied here by design.
 */

#define M24C64_ADDR  0x50u
#define M24C64_SIZE  8192u
#define M24C64_PAGE  32u

/* Byte 0 is the module class ID (ADR-0014 d6). A device that has never been
 * written reads 0xFF there, which is not a fault. */
#define M24C64_CLASS_ID_ADDR 0u

/* True if the device acknowledges and answers a read. There is no identity
 * register on a 24Cxx, so this is an ACK plus a transfer that completes -- less
 * than the device-ID read U1 gets, and all the part offers. */
bool m24c64_present(void);

int m24c64_read(uint16_t addr, uint8_t *buf, uint16_t len);

/* One page write, `len` bytes not crossing a 32-byte page boundary. The device
 * then acknowledges nothing for up to 5 ms; the caller waits on
 * m24c64_ready() rather than the driver blocking in it, so that a 1568-byte
 * trim commit -- 49 pages, about 250 ms of write cycles -- is spread across
 * passes of the main loop instead of holding it (M04 spec 6.7, 10). */
int m24c64_write_page(uint16_t addr, const uint8_t *buf, uint16_t len);

/* True once the internal write cycle has finished (address acknowledge poll). */
bool m24c64_ready(void);

#endif /* IGROW_M04_M24C64_H */
