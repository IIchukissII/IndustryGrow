/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "crc32.h"
#include "stm32f4xx.h"

void crc32_mpeg2_begin(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR; /* the enable takes effect after one read-back */
    CRC->CR = CRC_CR_RESET;
}

void crc32_mpeg2_feed(const void *addr, uint32_t bytes)
{
    const uint32_t *words = (const uint32_t *)addr;
    const uint32_t count = bytes / 4u;
    for (uint32_t i = 0u; i < count; i++) {
        CRC->DR = words[i];
    }
}

uint32_t crc32_mpeg2_end(void)
{
    return CRC->DR;
}

uint32_t crc32_mpeg2_words(const uint32_t *words, uint32_t count)
{
    crc32_mpeg2_begin();
    crc32_mpeg2_feed(words, count * 4u);
    return crc32_mpeg2_end();
}

uint32_t crc32_mpeg2_region(const void *addr, uint32_t bytes)
{
    return crc32_mpeg2_words((const uint32_t *)addr, bytes / 4u);
}
