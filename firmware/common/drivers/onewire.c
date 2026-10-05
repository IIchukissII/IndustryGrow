/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "onewire.h"

#include "e0001.h" /* CMSIS device header */

#define OW_GPIO GPIOA
#define OW_PIN  0u

/* Cycle counter at the 168 MHz core clock: the only time base here fine enough
 * for a 6 us slot. SysTick is 1 kHz. */
#define CYCLES_PER_US (SystemCoreClock / 1000000u)

static void delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t ticks = us * CYCLES_PER_US;
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

static inline void bus_low(void) { OW_GPIO->BSRR = (1u << OW_PIN) << 16u; }
static inline void bus_release(void) { OW_GPIO->BSRR = (1u << OW_PIN); }
static inline bool bus_read(void) { return (OW_GPIO->IDR & (1u << OW_PIN)) != 0u; }

void ow_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    bus_release();
    OW_GPIO->OTYPER |= (1u << OW_PIN); /* open drain */
    OW_GPIO->PUPDR &= ~(3u << (OW_PIN * 2u)); /* the module holds the pull-up */
    OW_GPIO->MODER = (OW_GPIO->MODER & ~(3u << (OW_PIN * 2u))) | (1u << (OW_PIN * 2u));
}

bool ow_reset(void)
{
    __disable_irq();
    bus_low();
    delay_us(480u);
    bus_release();
    delay_us(70u);
    const bool present = !bus_read();
    __enable_irq();
    delay_us(410u);
    return present;
}

static void write_bit(bool one)
{
    __disable_irq();
    bus_low();
    if (one) {
        delay_us(6u);
        bus_release();
        __enable_irq();
        delay_us(64u);
    } else {
        delay_us(60u);
        bus_release();
        __enable_irq();
        delay_us(10u);
    }
}

static bool read_bit(void)
{
    __disable_irq();
    bus_low();
    delay_us(6u);
    bus_release();
    delay_us(9u);
    const bool bit = bus_read();
    __enable_irq();
    delay_us(55u);
    return bit;
}

void ow_write_byte(uint8_t b)
{
    for (unsigned i = 0; i < 8u; i++) {
        write_bit((b >> i) & 1u);
    }
}

uint8_t ow_read_byte(void)
{
    uint8_t b = 0u;
    for (unsigned i = 0; i < 8u; i++) {
        if (read_bit()) {
            b |= (uint8_t)(1u << i);
        }
    }
    return b;
}

uint8_t ow_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0u;
    for (size_t i = 0; i < len; i++) {
        uint8_t in = data[i];
        for (unsigned b = 0; b < 8u; b++) {
            const uint8_t mix = (uint8_t)((crc ^ in) & 1u);
            crc >>= 1;
            if (mix) {
                crc ^= 0x8Cu;
            }
            in >>= 1;
        }
    }
    return crc;
}

void ow_match_rom(uint64_t rom)
{
    ow_write_byte(OW_CMD_MATCH_ROM);
    for (unsigned i = 0; i < 8u; i++) {
        ow_write_byte((uint8_t)(rom >> (8u * i)));
    }
}

size_t ow_search(uint64_t *roms, size_t max)
{
    size_t found = 0u;
    int last_discrepancy = -1; /* bit index of the last branch taken as 0 */
    uint64_t rom = 0u;
    bool done = false;

    while (!done && (found < max)) {
        if (!ow_reset()) {
            break;
        }
        ow_write_byte(OW_CMD_SEARCH_ROM);
        int discrepancy = -1;
        for (int bit = 0; bit < 64; bit++) {
            const bool id = read_bit();
            const bool cmp = read_bit();
            bool dir;
            if (id && cmp) {
                return found; /* no device answered this slot: bus disturbed */
            } else if (id != cmp) {
                dir = id; /* every remaining device agrees */
            } else if (bit < last_discrepancy) {
                dir = ((rom >> bit) & 1u) != 0u; /* repeat the earlier choice */
            } else {
                dir = (bit == last_discrepancy); /* take 1 at the old fork, 0 at a new one */
            }
            if (!dir && (id == cmp)) {
                discrepancy = bit;
            }
            if (dir) {
                rom |= (1ull << bit);
            } else {
                rom &= ~(1ull << bit);
            }
            write_bit(dir);
        }
        uint8_t bytes[8];
        for (unsigned i = 0; i < 8u; i++) {
            bytes[i] = (uint8_t)(rom >> (8u * i));
        }
        if (ow_crc8(bytes, 7u) == bytes[7]) {
            roms[found++] = rom;
        }
        last_discrepancy = discrepancy;
        done = (discrepancy < 0);
    }
    return found;
}
