/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_A01_PCA9685_H
#define IGROW_A01_PCA9685_H

#include <stdint.h>

/* NXP PCA9685 16-channel 12-bit PWM generator, A01's module-local drive
 * expansion (U9, ADR-0031 rev 1 d11). I2C1, address 0x40, internal 25 MHz
 * oscillator, totem-pole outputs.
 *
 * Every output is LOW after power-on reset (all LEDn_OFF_H full-off bits set)
 * and stays LOW while the device sleeps, which is the state E0011 wires each
 * element's safe output to (A01 spec F11). */
#define PCA9685_ADDR 0x40u

/* Highest output frequency the prescaler reaches, PRE_SCALE = 3. Both RC
 * filters on U9 (VREF2) and the fan inputs are sized against it. */
#define PCA9685_PWM_HZ 1526u

/* Configure and wake the device. 0 on success, <0 if it does not answer. */
int pca9685_init(void);

/* Duty 0..1 on channel 0..15, using the full-off and full-on bits at the two
 * ends so a static level is exactly static. */
int pca9685_set(uint8_t channel, float duty);

/* True if MODE1 still holds what init wrote. A device that has lost power
 * comes back asleep with its outputs LOW; this is how the node notices it has
 * to re-initialize rather than writing duties into a sleeping part. */
int pca9685_configured(void);

#endif /* IGROW_A01_PCA9685_H */
