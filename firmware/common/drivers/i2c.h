/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_I2C_H
#define IGROW_DRIVERS_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Blocking I2C1 master on PB6/PB7 (AF4), standard mode 100 kHz — the sensor-
 * module bus (M05: INA226, TMP117; M01: SHT45, BME688, SCD41). 7-bit addresses.
 * All calls return 0 on success, <0 on NACK/timeout.
 *
 * 100 kHz is the platform default and is set here, not by any device on a module
 * (M01 spec §5.1): every part on every module so far tolerates it. */
void i2c_init(void);

/* True if a device ACKs its address (used for presence-probing, ADR-0014 d8). */
bool i2c_probe(uint8_t addr7);

/* --- Byte-level transactions ---------------------------------------------
 * The register helpers below only reach chips whose protocol is
 * "8-bit register pointer, 16-bit big-endian value". M01's sensors are not
 * such chips: the SHT45 takes a bare command byte and answers 6 bytes, the
 * SCD41 takes a 16-bit command word and answers CRC-checked word triples, and
 * the BME688 answers register bursts of up to 23 bytes. These three cover all
 * of it. `len` must be >= 1.
 *
 * i2c_write_read() issues a repeated START between the two phases, which is
 * what the BME688 register read and the SCD41 word read both require; a
 * STOP/START pair would release the bus mid-transaction. */
int i2c_write(uint8_t addr7, const uint8_t *buf, size_t len);
int i2c_read(uint8_t addr7, uint8_t *buf, size_t len);
int i2c_write_read(uint8_t addr7, const uint8_t *wbuf, size_t wlen,
                   uint8_t *rbuf, size_t rlen);

/* --- 16-bit register helpers ---------------------------------------------
 * The M05 path (INA226, TMP117), bench-verified 2026-08-02. Kept as its own
 * code rather than re-expressed over i2c_write_read() above: the two-byte read
 * runs the same RM0090 POS sequence either way, and re-routing a verified
 * driver for hardware that is not on the bench buys nothing. Fold them in once
 * M01 has confirmed the general path against three more chips. */
int i2c_write_reg16(uint8_t addr7, uint8_t reg, uint16_t value);
int i2c_read_reg16(uint8_t addr7, uint8_t reg, uint16_t *out);

#endif /* IGROW_DRIVERS_I2C_H */
