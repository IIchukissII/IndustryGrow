/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_SHT4X_H
#define IGROW_M01_SHT4X_H

#include <stdint.h>

/*
 * U1 -- Sensirion SHT45, M01's PRIMARY temperature and humidity source and the
 * only admissible input to the VPD computation (M01 spec 4, 6.1). The BME688
 * and SCD41 also report T/RH; those are published on their own subjects and
 * must never reach this quantity.
 *
 * Address 0x44, fixed -- the SHT4x address is set by the ordering part, not by
 * a strap, so a bus conflict here is a BOM error and not a jumper.
 *
 * The heater is NOT driven by this driver. Spec 10 leaves it disabled by
 * default, and its 200 mW pulse is 59 K of self-heating at steady state
 * (spec 8.1) against a 0.1 K module budget -- an accidental pulse would not
 * corrupt one reading, it would corrupt the thermal state the next readings are
 * taken in. Condensate recovery is a commanded operation, not a background one,
 * and gets its own entry point when it exists.
 *
 * The approved alternative SHT45-AD1B-R2 (spec 4.2) is identical here: it drops
 * the PTFE membrane, which is a mechanical property, not a protocol one.
 */

#define SHT4X_ADDR 0x44u

/* One high-repeatability conversion: command, ~10 ms, 6 bytes CRC-checked.
 * Blocking. `rh_ratio` is 0.0..1.0, clamped -- the raw transfer function is
 * allowed to overshoot slightly at the rails and a >100 %RH figure would
 * propagate into VPD as a negative deficit. Returns 0 on success. */
int sht4x_read(float *celsius, float *rh_ratio);

/* 32-bit device serial. Read at boot as proof that a part which merely ACKs its
 * address can also answer a command. */
int sht4x_serial(uint32_t *out);

int sht4x_soft_reset(void);

#endif /* IGROW_M01_SHT4X_H */
