/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "e0001.h"

#if IGROW_CARRIER_000100
#include "clock.h"
#include "i2c.h"
#endif

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
    e0001_weact_led(false);
    gpio_mode(E0001_WEACT_LED_GPIO, E0001_WEACT_LED_PIN, 1u);

    /* Module-ID straps as inputs. Pull-downs on the strap carrier; none on
     * E0001-000100 until the fallback asks for them. */
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP0_PIN, 0u);
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP1_PIN, 0u);
    gpio_mode(E0001_STRAP_GPIO, E0001_STRAP2_PIN, 0u);
#if !IGROW_CARRIER_000100
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP0_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP1_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP2_PIN, 2u);
#endif
}

uint8_t e0001_read_module_id(void)
{
#if IGROW_CARRIER_000100
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP0_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP1_PIN, 2u);
    gpio_pull(E0001_STRAP_GPIO, E0001_STRAP2_PIN, 2u);
    delay_ms(1u); /* let the pins settle against the pulls */
#endif
    uint32_t idr = E0001_STRAP_GPIO->IDR;
    uint8_t id = 0u;
    if (idr & (1u << E0001_STRAP0_PIN)) id |= 1u << 0u;
    if (idr & (1u << E0001_STRAP1_PIN)) id |= 1u << 1u;
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

void e0001_weact_led(bool on)
{
    uint32_t bit = 1u << E0001_WEACT_LED_PIN;
#if E0001_WEACT_LED_ACTIVE_HIGH
    E0001_WEACT_LED_GPIO->BSRR = on ? bit : (bit << 16u);
#else
    E0001_WEACT_LED_GPIO->BSRR = on ? (bit << 16u) : bit;
#endif
}

void e0001_weact_led_toggle(void)
{
    E0001_WEACT_LED_GPIO->ODR ^= (1u << E0001_WEACT_LED_PIN);
}

void e0001_led_status_toggle(void)
{
    E0001_LED_GPIO->ODR ^= (1u << E0001_LED_STATUS_PIN);
}

#if IGROW_CARRIER_000100
int e0001_read_class_id(uint8_t *class_id, bool *answered)
{
    *answered = i2c_probe(E0001_MODULE_EEPROM_ADDR);
    if (!*answered) {
        return -1;
    }
    const uint8_t ptr[2] = {(uint8_t)(E0001_CLASS_ID_OFFSET >> 8), (uint8_t)E0001_CLASS_ID_OFFSET};
    return i2c_write_read(E0001_MODULE_EEPROM_ADDR, ptr, sizeof(ptr), class_id, 1u);
}
#endif
