/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_SENSIRION_H
#define IGROW_M01_SENSIRION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The wire conventions shared by M01's two Sensirion parts, U1 (SHT45) and
 * U3 (SCD41). They differ in how a command is addressed -- U1 takes a single
 * command byte, U3 a 16-bit command word -- but agree on everything after it:
 * data travels as big-endian 16-bit words, each followed by a CRC-8 byte
 * computed over that word alone.
 *
 * Kept in one place because a CRC that is right for one part and wrong for the
 * other is a bug that reads as a sensor fault. Both datasheets specify the same
 * polynomial, and this is the check that a garbled I2C read does not enter the
 * VPD computation as a plausible number.
 */

/* CRC-8, polynomial 0x31 (x^8 + x^5 + x^4 + 1), init 0xFF, no reflection, no
 * final XOR -- the checksum both datasheets specify. */
uint8_t sensirion_crc8(const uint8_t *data, size_t len);

/* Unpack `count` big-endian words from a (3 * count)-byte buffer, verifying the
 * CRC byte that follows each. Returns 0 on success, -1 if any CRC fails --
 * partial results are not written. */
int sensirion_unpack(const uint8_t *buf, uint16_t *words, size_t count);

#endif /* IGROW_M01_SENSIRION_H */
