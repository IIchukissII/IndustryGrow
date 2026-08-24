/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M05_LEAK_H
#define IGROW_M05_LEAK_H

#include <stdbool.h>
#include <stdint.h>

/* Reservoir/pump-zone leak strip: sensed on ADC_1 = PC4 (ADC1_IN14), excited
 * from GPIO_1 = PA9. E0006 puts R6 (4.7k) between the two, with the strip as
 * the lower leg to GND and R8 (1k) + C5 (100n) filtering into the ADC. The
 * electrode is impulse-excited only during a sample to avoid electrolysis
 * (ADR-0018 d11). Report/alert only — no interlock.
 *
 * PA9 is also USART1_TX, so leak_init() ENDS the debug console (e0001.h). Do
 * all uart_puts() before sensors_init(); release builds use SWD instead. */
void leak_init(void);

/* One gated-excitation sample: drive the electrode, settle, read ADC, stop. */
/* One gated, settled ADC sample, and the wet/dry decision taken from it.
 *
 * Returns false when the conversion does not complete, in which case neither
 * output is meaningful: the data register holds whatever the last conversion
 * left there, and comparing that against a threshold would invent a leak state.
 * That failure is what LeakStatus.valid reports. Either pointer may be NULL.
 *
 * One call, one excitation pulse: asking for the state and the raw count
 * separately would double the duty the gating exists to keep down. */
bool leak_sample(bool *wet, uint16_t *raw);

/* Convenience: true if the latest sample crosses the wet threshold. */


#endif /* IGROW_M05_LEAK_H */
