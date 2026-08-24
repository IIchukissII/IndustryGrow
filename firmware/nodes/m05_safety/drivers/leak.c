/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "leak.h"

#include <stddef.h>
#include "e0001.h"
#include "clock.h"

#define LEAK_ADC_CH 14u /* PC4 = ADC1_IN14 = ADC_1 on the header */
#define LEAK_EXC_PIN 9u /* PA9 = GPIO_1 on the header (also USART1_TX) */

/* Settle before converting: E0006 charges C5 (100n) through R6 (4.7k) + R8
 * (1k), so tau ~= 570 us. 5 ms is >8 tau, and at the 1 Hz publish rate the
 * excitation duty cycle stays at 0.5% -- which is the point of gating it. */
#define LEAK_SETTLE_MS 5u

/* Conductive liquid lowers the electrode impedance and pulls the divider; a
 * dry strip reads near full-scale. Threshold is provisional pending E0006
 * conditioning values (ADR-0000: lives in the schematic/BOM). */
#define LEAK_WET_THRESHOLD 2048u

/* Excitation off = driven low, not high-Z: with the strip's lower leg on GND,
 * both ends sit at 0 V, so no DC flows through the electrodes between samples.
 * High-Z would leave the node floating on C5's charge instead. */
static void leak_excite(bool on)
{
    if (on) {
        GPIOA->BSRR = (1u << LEAK_EXC_PIN);
    } else {
        GPIOA->BSRR = (1u << (LEAK_EXC_PIN + 16u));
    }
}

void leak_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    /* Claim PA9 from USART1_TX (AF7) as a push-pull output, low. This is what
     * ends the debug console -- see leak.h. Drive low BEFORE switching MODER so
     * the pin never glitches high into the electrode on the way through. */
    GPIOA->BSRR = (1u << (LEAK_EXC_PIN + 16u));
    GPIOA->MODER &= ~(3u << (LEAK_EXC_PIN * 2u));
    GPIOA->MODER |= (1u << (LEAK_EXC_PIN * 2u)); /* output */
    GPIOA->OTYPER &= ~(1u << LEAK_EXC_PIN);      /* push-pull */
    GPIOA->PUPDR &= ~(3u << (LEAK_EXC_PIN * 2u));

    GPIOC->MODER |= (3u << (4u * 2u)); /* PC4 analog */

    /* Long sample time for the high-impedance electrode: SMPR1 ch14 = 480 cyc. */
    ADC1->SMPR1 |= (7u << ((LEAK_ADC_CH - 10u) * 3u));
    ADC1->SQR1 = 0u;          /* one conversion */
    ADC1->SQR3 = LEAK_ADC_CH; /* first (only) in sequence */
    ADC1->CR2 |= ADC_CR2_ADON;
}

bool leak_sample(bool *wet, uint16_t *raw)
{
    leak_excite(true);
    delay_ms(LEAK_SETTLE_MS);

    ADC1->SR &= ~ADC_SR_EOC;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t g = 100000u;
    while (!(ADC1->SR & ADC_SR_EOC) && --g) {
    }
    const bool ok = (g != 0u);
    const uint16_t v = (uint16_t)ADC1->DR;

    leak_excite(false);
    if (raw != NULL) {
        *raw = v;
    }
    if (wet != NULL) {
        *wet = ok && (v < LEAK_WET_THRESHOLD);
    }
    return ok;
}


