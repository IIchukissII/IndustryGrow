/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "leak.h"
#include "e0001.h"
#include "clock.h"

#define LEAK_ADC_CH 14u /* PC4 = ADC1_IN14 */

/* Conductive liquid lowers the electrode impedance and pulls the divider; a
 * dry strip reads near full-scale. Threshold is provisional pending E0006
 * conditioning values (ADR-0000: lives in the schematic/BOM). */
#define LEAK_WET_THRESHOLD 2048u

void leak_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    GPIOC->MODER |= (3u << (4u * 2u)); /* PC4 analog */

    /* Long sample time for the high-impedance electrode: SMPR1 ch14 = 480 cyc. */
    ADC1->SMPR1 |= (7u << ((LEAK_ADC_CH - 10u) * 3u));
    ADC1->SQR1 = 0u;          /* one conversion */
    ADC1->SQR3 = LEAK_ADC_CH; /* first (only) in sequence */
    ADC1->CR2 |= ADC_CR2_ADON;
}

/* TODO(E0006): drive the gated excitation output here before sampling and
 * release it after (ADR-0018 d11). Pin is an E0006 net not yet in the pin map. */
uint16_t leak_sample_raw(void)
{
    /* leak_excite(true); delay; */
    ADC1->SR &= ~ADC_SR_EOC;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t g = 100000u;
    while (!(ADC1->SR & ADC_SR_EOC) && --g) {
    }
    uint16_t raw = (uint16_t)ADC1->DR;
    /* leak_excite(false); */
    return raw;
}

bool leak_is_wet(void)
{
    return leak_sample_raw() < LEAK_WET_THRESHOLD;
}
