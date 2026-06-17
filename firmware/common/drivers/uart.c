/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "uart.h"
#include "board.h"

void uart_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* PA9/PA10 to alternate function 7 (USART1). */
    const uint32_t tx = BRD_DBG_TX_PIN, rx = BRD_DBG_RX_PIN;
    BRD_DBG_GPIO->MODER &= ~((3u << (tx * 2u)) | (3u << (rx * 2u)));
    BRD_DBG_GPIO->MODER |= (2u << (tx * 2u)) | (2u << (rx * 2u)); /* AF mode */
    BRD_DBG_GPIO->AFR[1] &= ~((0xFu << ((tx - 8u) * 4u)) | (0xFu << ((rx - 8u) * 4u)));
    BRD_DBG_GPIO->AFR[1] |= (BRD_DBG_AF << ((tx - 8u) * 4u)) |
                            (BRD_DBG_AF << ((rx - 8u) * 4u));

    /* APB2 = 84 MHz; BRR = fck / baud (16x oversampling). */
    BRD_DBG_UART->BRR = (84000000u + (BRD_DBG_BAUD / 2u)) / BRD_DBG_BAUD;
    BRD_DBG_UART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while (!(BRD_DBG_UART->SR & USART_SR_TXE)) {
    }
    BRD_DBG_UART->DR = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_put_u32(uint32_t v)
{
    char buf[10];
    int i = 0;
    if (v == 0u) {
        uart_putc('0');
        return;
    }
    while (v > 0u && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void uart_put_bin3(uint8_t v)
{
    uart_putc((v & 0x4u) ? '1' : '0');
    uart_putc((v & 0x2u) ? '1' : '0');
    uart_putc((v & 0x1u) ? '1' : '0');
}
