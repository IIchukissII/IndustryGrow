/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_PWM_H
#define IGROW_DRIVERS_PWM_H

#include <stdint.h>

/* The four header PWM lines, PWM_1-PWM_4 on TIM3 CH1-CH4: PC6, PC7, PB0, PB1,
 * AF2 (E0001 spec 5.2). One timer, so one frequency and independent duty.
 *
 * Until pwm_init() runs the pins are the reset state, floating inputs; a module
 * that drives anything from them holds it safe with its own pulls. */
void pwm_init(uint32_t hz);

/* Duty 0..1 on header line 1..4. 0 holds the line low and 1 holds it high --
 * the full-scale compare sits past the period, so a line used as a static
 * enable never glitches. Out-of-range channels are ignored. */
void pwm_set(uint8_t line, float duty);

#endif /* IGROW_DRIVERS_PWM_H */
