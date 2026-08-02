/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "s0.h"
#include "e0001.h"

#define S0_PIN 12u /* PB12 = GPIO_4 */

/* Meter pulse constant (imp/kWh). Per-deployment: the meter is SP0001, a COTS
 * DIN part, and REGISTRY.md puts its phase count and pulse constant in the
 * deployment BOM (ADR-0018). No E0007 BOM exists yet, so the fitted meter's
 * value lives here until it does -- and until the subject-ID-style register
 * treatment of ADR-0005 d7 reaches it.
 *
 * Fitted meter: 1600 imp/kWh -> 2250 J per pulse. A meter swap that changes
 * this constant silently rescales every energy reading, so it is the first
 * thing to check when published energy looks wrong by a clean ratio. */
#define S0_IMP_PER_KWH 1600.0f
#define S0_JOULE_PER_KWH 3600000.0f

static volatile uint32_t s_pulses;

void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR & (1u << S0_PIN)) {
        EXTI->PR = (1u << S0_PIN); /* write-1-to-clear */
        s_pulses++;
    }
}

void s0_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    /* PB12 input, pull-up (S0 is open-collector, idle high). */
    GPIOB->MODER &= ~(3u << (S0_PIN * 2u));
    GPIOB->PUPDR &= ~(3u << (S0_PIN * 2u));
    GPIOB->PUPDR |= (1u << (S0_PIN * 2u));

    /* Route EXTI12 to port B (EXTICR[3], field for EXTI12 = bits [3:0]). */
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~0xFu) | 0x1u;

    EXTI->IMR |= (1u << S0_PIN);  /* unmask */
    EXTI->FTSR |= (1u << S0_PIN); /* falling edge = pulse leading edge */
    EXTI->RTSR &= ~(1u << S0_PIN);

    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

uint32_t s0_pulses(void)
{
    return s_pulses;
}

float s0_energy_joule(void)
{
    return (float)s_pulses * (S0_JOULE_PER_KWH / S0_IMP_PER_KWH);
}
