/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "clock.h"
#include "stm32f4xx.h"

static volatile uint32_t s_ms = 0u;

void SysTick_Handler(void)
{
    s_ms++;
}

uint32_t millis(void)
{
    return s_ms;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;
    while ((s_ms - start) < ms) {
        __asm volatile("wfi");
    }
}

uint64_t micros64(void)
{
    /* SysTick counts down from LOAD (= fck/1000 - 1) to 0 once per ms.
     * Read ms and VAL coherently against a tick landing in between. */
    uint32_t ms, val;
    do {
        ms = s_ms;
        val = SysTick->VAL;
    } while (ms != s_ms);
    uint32_t ticks_into_ms = SysTick->LOAD - val;
    return (uint64_t)ms * 1000u + (ticks_into_ms / (SystemCoreClock / 1000000u));
}

void clock_init(void)
{
    /* 1. Start HSE (8 MHz crystal on the WeAct board). */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
    }

    /* 2. Voltage scale 1 for 168 MHz operation. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS;

    /* 3. Flash: prefetch + I/D cache + 5 wait states (168 MHz @ 3.3 V). */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;

    /* 4. PLL: HSE(8) / M(8) = 1 MHz; * N(336) = 336; / P(2) = 168 MHz SYSCLK;
     *    / Q(7) = 48 MHz for USB. */
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) |
                   (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | /* PLLP = /2 */
                   (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }

    /* 5. Bus prescalers: AHB /1, APB1 /4 (42 MHz), APB2 /2 (84 MHz). */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* 6. Switch SYSCLK to the PLL. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }

    SystemCoreClock = 168000000u;

    /* 7. 1 kHz SysTick for millis()/delay_ms(). */
    SysTick_Config(SystemCoreClock / 1000u);
}
