/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_A01_DS18B20_H
#define IGROW_A01_DS18B20_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DS18B20 temperatures on the header 1-Wire bus, U1-U4 on E0011 (A01 spec 3.2).
 *
 * The four sensors share one bus and are told apart only by their ROM codes,
 * which the caller supplies per role. A cycle is one Convert T to every device
 * at once, the 750 ms 12-bit conversion, then a scratchpad read of each role
 * whose code is set. The cycle advances by ONE bus operation per call -- a
 * reset or a byte, at most about 1 ms -- so a read of four devices never holds
 * the main loop long enough to overrun the CAN receive FIFO.
 *
 * Externally powered devices only: the conversion is timed, not polled, and the
 * bus is not held high for a parasite supply. E0011 powers U1-U4 from 3V3. */
#define DS18B20_FAMILY 0x28u
#define DS18B20_ROLES  4u

/* `roms` is DS18B20_ROLES codes, 0 = role not assigned. Borrowed: the caller
 * may change a code at any time and the next cycle reads the new one. */
void ds18b20_init(const uint64_t *roms);

/* Advance the cycle by one bus operation. */
void ds18b20_poll(void);

/* Latest valid reading for `role` and when it was taken (micros64()). False if
 * the role has never produced one. A CRC failure or the 85 C power-on value is
 * not a reading. */
bool ds18b20_latest(size_t role, float *kelvin, uint64_t *at_us);

#endif /* IGROW_A01_DS18B20_H */
