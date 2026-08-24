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
 * The heater never runs on its own. Spec 10 leaves it disabled by default, and
 * its 200 mW level is 59 K of self-heating at steady state (spec 8.1) against a
 * 0.1 K module budget -- an accidental pulse would not corrupt one reading, it
 * would corrupt the thermal state the next readings are taken in. Condensate
 * recovery is therefore a COMMANDED operation: sht4x_heater_pulse() below is
 * the only entry point, it uses the lowest level and shortest duration, and
 * nothing in this driver calls it.
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

/* One condensate-recovery pulse: 20 mW for 0.1 s, the lowest level and shortest
 * duration the device offers, which M01 spec 10 prefers and which keeps the
 * 10 % duty ceiling far away. Blocks ~110 ms. The measurement the device
 * returns at the end of the pulse is taken on a heated die and is discarded --
 * the pulse is for driving condensate off the sensor, not for reading. */
int sht4x_heater_pulse(void);

#endif /* IGROW_M01_SHT4X_H */
