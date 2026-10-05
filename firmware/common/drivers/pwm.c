/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "pwm.h"

#include "e0001.h" /* CMSIS device header */

/* TIM3 sits on APB1 at 42 MHz; with the APB1 prescaler at 4 the timer clock is
 * doubled to 84 MHz (RM0090 6.2, clock.h). */
#define TIM3_CLK_HZ 84000000u

static uint32_t s_period; /* ARR + 1 */

static void pin_af2(GPIO_TypeDef *port, uint32_t pin)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
    port->OSPEEDR = (port->OSPEEDR & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
    volatile uint32_t *afr = &port->AFR[pin >> 3];
    const uint32_t shift = (pin & 7u) * 4u;
    *afr = (*afr & ~(0xFu << shift)) | (2u << shift);
}

void pwm_init(uint32_t hz)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;

    uint32_t psc = 0u;
    s_period = TIM3_CLK_HZ / hz;
    while (s_period > 65536u) {
        psc++;
        s_period = TIM3_CLK_HZ / ((psc + 1u) * hz);
    }
    TIM3->CR1 = 0u;
    TIM3->PSC = psc;
    TIM3->ARR = s_period - 1u;
    TIM3->CCR1 = 0u;
    TIM3->CCR2 = 0u;
    TIM3->CCR3 = 0u;
    TIM3->CCR4 = 0u;
    /* PWM mode 1 with preload on every channel: high while CNT < CCR. */
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE |
                  (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM3->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    /* Pins last, so they leave the floating state already holding duty 0. */
    pin_af2(GPIOC, 6u);
    pin_af2(GPIOC, 7u);
    pin_af2(GPIOB, 0u);
    pin_af2(GPIOB, 1u);
}

void pwm_set(uint8_t line, float duty)
{
    if (!(duty > 0.0f)) {
        duty = 0.0f; /* also catches NaN */
    }
    const uint32_t ccr = (duty >= 1.0f) ? s_period : (uint32_t)(duty * (float)s_period + 0.5f);
    switch (line) {
    case 1u: TIM3->CCR1 = ccr; break;
    case 2u: TIM3->CCR2 = ccr; break;
    case 3u: TIM3->CCR3 = ccr; break;
    case 4u: TIM3->CCR4 = ccr; break;
    default: break;
    }
}
