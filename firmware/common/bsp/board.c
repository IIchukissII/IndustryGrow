/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "board.h"

/* Helper: set two MODER bits for `pin` to `mode` (00 in,01 out,10 AF,11 an). */
static void gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

/* Helper: select alternate function `af` for `pin` (AFR[0] pins 0-7, AFR[1] 8-15). */
static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t idx = pin >> 3u;
    uint32_t pos = (pin & 7u) * 4u;
    port->AFR[idx] = (port->AFR[idx] & ~(0xFu << pos)) | (af << pos);
}

/* Helper: set PUPDR for `pin` (00 none,01 pull-up,10 pull-down). */
static void gpio_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull)
{
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
}

void board_init(void)
{
    /* GPIOA + GPIOB clocks. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR; /* dummy read: ensure clock is up before access */

    /* LEDs as push-pull outputs, start off. */
    board_led_status(false);
    board_led_can(false);
    gpio_mode(BRD_LED_GPIO, BRD_LED_STATUS_PIN, 1u);
    gpio_mode(BRD_LED_GPIO, BRD_LED_CAN_PIN, 1u);

    /* Module-ID straps as inputs with pull-down. */
    gpio_mode(BRD_STRAP_GPIO, BRD_STRAP0_PIN, 0u);
    gpio_mode(BRD_STRAP_GPIO, BRD_STRAP1_PIN, 0u);
    gpio_mode(BRD_STRAP_GPIO, BRD_STRAP2_PIN, 0u);
    gpio_pull(BRD_STRAP_GPIO, BRD_STRAP0_PIN, 2u);
    gpio_pull(BRD_STRAP_GPIO, BRD_STRAP1_PIN, 2u);
    gpio_pull(BRD_STRAP_GPIO, BRD_STRAP2_PIN, 2u);
}

uint8_t board_read_module_id(void)
{
    uint32_t idr = BRD_STRAP_GPIO->IDR;
    uint8_t id = 0u;
    if (idr & (1u << BRD_STRAP0_PIN)) id |= 1u << 0u;
    if (idr & (1u << BRD_STRAP1_PIN)) id |= 1u << 1u; /* always 0 on E0001-000001 */
    if (idr & (1u << BRD_STRAP2_PIN)) id |= 1u << 2u;
    return id;
}

void board_led_status(bool on)
{
    uint32_t bit = 1u << BRD_LED_STATUS_PIN;
#if BRD_LED_ACTIVE_HIGH
    BRD_LED_GPIO->BSRR = on ? bit : (bit << 16u);
#else
    BRD_LED_GPIO->BSRR = on ? (bit << 16u) : bit;
#endif
}

void board_led_can(bool on)
{
    uint32_t bit = 1u << BRD_LED_CAN_PIN;
#if BRD_LED_ACTIVE_HIGH
    BRD_LED_GPIO->BSRR = on ? bit : (bit << 16u);
#else
    BRD_LED_GPIO->BSRR = on ? (bit << 16u) : bit;
#endif
}

void board_led_status_toggle(void)
{
    BRD_LED_GPIO->ODR ^= (1u << BRD_LED_STATUS_PIN);
}
