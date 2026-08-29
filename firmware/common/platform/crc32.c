/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "crc32.h"
#include "stm32f4xx.h"

uint32_t crc32_mpeg2_words(const uint32_t *words, uint32_t count)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR; /* the enable takes effect after one read-back */
    CRC->CR = CRC_CR_RESET;
    for (uint32_t i = 0u; i < count; i++) {
        CRC->DR = words[i];
    }
    return CRC->DR;
}

uint32_t crc32_mpeg2_region(const void *addr, uint32_t bytes)
{
    return crc32_mpeg2_words((const uint32_t *)addr, bytes / 4u);
}
