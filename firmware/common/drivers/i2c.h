/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_I2C_H
#define IGROW_DRIVERS_I2C_H

#include <stdbool.h>
#include <stdint.h>

/* Blocking I2C1 master on PB6/PB7 (AF4), standard mode 100 kHz — the sensor-
 * module bus (INA226, TMP117). 7-bit addresses; 16-bit registers are MSB-first
 * as both target chips use. All calls return 0 on success, <0 on NACK/timeout. */
void i2c_init(void);

/* True if a device ACKs its address (used for presence-probing, ADR-0014 d8). */
bool i2c_probe(uint8_t addr7);

int i2c_write_reg16(uint8_t addr7, uint8_t reg, uint16_t value);
int i2c_read_reg16(uint8_t addr7, uint8_t reg, uint16_t *out);

#endif /* IGROW_DRIVERS_I2C_H */
