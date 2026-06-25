/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_TMP117_H
#define IGROW_DRIVERS_TMP117_H

#include <stdint.h>

/* TI TMP117 cabinet/enclosure air temperature (report only; ADR-0018 d11).
 * Default I2C address 0x48 (ADD0 = GND). */
#define TMP117_ADDR 0x48u

/* Read temperature in kelvin (SI on the wire, ADR-0005 d3). Returns 0 on
 * success. Power-on default mode is continuous conversion, so no init needed. */
int tmp117_read_kelvin(float *kelvin);

#endif /* IGROW_DRIVERS_TMP117_H */
