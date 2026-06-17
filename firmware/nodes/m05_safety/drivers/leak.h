/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M05_LEAK_H
#define IGROW_M05_LEAK_H

#include <stdbool.h>
#include <stdint.h>

/* Reservoir/pump-zone leak strip on ADC_1 = PC4 (ADC1_IN14). The electrode is
 * impulse-excited only during a sample to avoid electrolysis (ADR-0018 d11);
 * the excitation drive pin is an E0006 net to be wired into leak_sample_raw().
 * Report/alert only — no interlock. */
void leak_init(void);

/* One gated-excitation sample: drive the electrode, settle, read ADC, stop. */
uint16_t leak_sample_raw(void);

/* Convenience: true if the latest sample crosses the wet threshold. */
bool leak_is_wet(void);

#endif /* IGROW_M05_LEAK_H */
