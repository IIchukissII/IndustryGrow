/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_INA226_H
#define IGROW_DRIVERS_INA226_H

#include <stdbool.h>
#include <stdint.h>

/* TI INA226 on the +12 V SELV sensor bus (ADR-0018 d5). One device on the
 * board. E0006 straps A1 to VS and A0 to GND (U2 pads 1 and 2 sit on the rails
 * the schematic names STRAP_0 / STRAP_1), which is address 0x44 — not the
 * 0x40 that A1=A0=GND would give. */
#define INA226_ADDR 0x44u

/* Configure averaging/conversion + calibration. Returns 0 on success.
 * Calibration depends on the shunt resistor and full-scale current, which are
 * E0006 BOM values (ADR-0000) — set via the macros in ina226.c. */
int ina226_init(void);

/* Read source-side bus voltage [V], current [A], power [W]. Any pointer may be
 * NULL. Returns 0 on success. Current/power are only meaningful if the shunt
 * macros match the populated shunt. */
int ina226_read(float *bus_v, float *current_a, float *power_w);

#endif /* IGROW_DRIVERS_INA226_H */
