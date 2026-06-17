/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M05_S0_H
#define IGROW_M05_S0_H

#include <stdint.h>

/* DIN energy-meter S0 pulse input on GPIO_4 = PB12 (pin map; E0006: S0 -> GPIO_4).
 * Opto-isolated open-collector output, counted as falling edges via EXTI.
 * Energy [Wh] = pulses * 1000 / (meter pulse constant imp/kWh). The constant is
 * a per-deployment value (ADR-0018) — a register default for now (ADR-0005 d7). */
void s0_init(void);
uint32_t s0_pulses(void);
float s0_energy_wh(void);

#endif /* IGROW_M05_S0_H */
