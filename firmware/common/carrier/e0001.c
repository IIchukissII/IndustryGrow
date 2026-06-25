/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "e0001.h"

/* Helper: set two MODER bits for `pin` to `mode` (00 in,01 out,10 AF,11 an). */
static void gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

/* Helper: set PUPDR for `pin` (00 none,01 pull-up,10 pull-down). */
static void gpio_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull)
{
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
}

void e0001_init(void)
{
    /* GPIOA + GPIOB clocks. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR; /* dummy read: ensure clock is up before access */

    /* LEDs as push-pull outputs, start off. */
    e0001_led_status(false);
    e0001_led_can(false);
    gpio_mode(E0001_LED_GPIO, E0001_LED_STATUS_PIN, 1u);
    gpio_mode(E0001_LED_GPIO, E0001_LED_CAN_PIN, 1u);

    /* Module-ID straps as inputs with pull-down. */
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP0_PIN, 0u);
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP1_PIN, 0u);
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP2_PIN, 0u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP0_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP1_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP2_PIN, 2u);
}

uint8_t e0001_read_module_id(void)
{
    uint32_t idr = E0001_STRAP_GPIO->IDR;
    uint8_t id = 0u;
    if (idr & (1u << E0001_STRAP0_PIN)) id |= 1u << 0u;
    if (idr & (1u << E0001_STRAP1_PIN)) id |= 1u << 1u; /* always 0 on E0001-000001 */
    if (idr & (1u << E0001_STRAP2_PIN)) id |= 1u << 2u;
    return id;
}

void e0001_led_status(bool on)
{
    uint32_t bit = 1u << E0001_LED_STATUS_PIN;
#if E0001_LED_ACTIVE_HIGH
    E0001_LED_GPIO->BSRR = on ? bit : (bit << 16u);
#else
    E0001_LED_GPIO->BSRR = on ? (bit << 16u) : bit;
#endif
}

void e0001_led_can(bool on)
{
    uint32_t bit = 1u << E0001_LED_CAN_PIN;
#if E0001_LED_ACTIVE_HIGH
    E0001_LED_GPIO->BSRR = on ? bit : (bit << 16u);
#else
    E0001_LED_GPIO->BSRR = on ? (bit << 16u) : bit;
#endif
}

void e0001_led_status_toggle(void)
{
    E0001_LED_GPIO->ODR ^= (1u << E0001_LED_STATUS_PIN);
}
