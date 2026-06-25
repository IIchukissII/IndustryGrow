/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "i2c.h"
#include "e0001.h"

#define I2C_TMO 100000u

/* PB6 = SCL, PB7 = SDA (pin map: I2C1, AF4). */
#define I2C_SCL_PIN 6u
#define I2C_SDA_PIN 7u

static int wait_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t g = I2C_TMO;
    while (!(*reg & mask) && --g) {
    }
    return g ? 0 : -1;
}

void i2c_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;

    /* PB6/PB7: AF4, open-drain, pull-up, high speed. */
    GPIOB->MODER &= ~((3u << (I2C_SCL_PIN * 2u)) | (3u << (I2C_SDA_PIN * 2u)));
    GPIOB->MODER |= (2u << (I2C_SCL_PIN * 2u)) | (2u << (I2C_SDA_PIN * 2u));
    GPIOB->OTYPER |= (1u << I2C_SCL_PIN) | (1u << I2C_SDA_PIN);
    GPIOB->OSPEEDR |= (3u << (I2C_SCL_PIN * 2u)) | (3u << (I2C_SDA_PIN * 2u));
    GPIOB->PUPDR &= ~((3u << (I2C_SCL_PIN * 2u)) | (3u << (I2C_SDA_PIN * 2u)));
    GPIOB->PUPDR |= (1u << (I2C_SCL_PIN * 2u)) | (1u << (I2C_SDA_PIN * 2u));
    GPIOB->AFR[0] &= ~((0xFu << (I2C_SCL_PIN * 4u)) | (0xFu << (I2C_SDA_PIN * 4u)));
    GPIOB->AFR[0] |= (4u << (I2C_SCL_PIN * 4u)) | (4u << (I2C_SDA_PIN * 4u));

    /* Reset then configure for 100 kHz on PCLK1 = 42 MHz. */
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0u;
    I2C1->CR2 = 42u;                 /* FREQ = APB1 MHz */
    I2C1->CCR = 210u;                /* Sm: 42e6 / (2 * 100e3) */
    I2C1->TRISE = 43u;               /* FREQ + 1 */
    I2C1->CR1 = I2C_CR1_PE;
}

static int start(uint8_t addr7, bool read)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (wait_set(&I2C1->SR1, I2C_SR1_SB) < 0) {
        return -1;
    }
    I2C1->DR = (uint8_t)((addr7 << 1) | (read ? 1u : 0u));
    uint32_t g = I2C_TMO;
    while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)) && --g) {
    }
    if ((g == 0u) || (I2C1->SR1 & I2C_SR1_AF)) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return -2; /* no ACK */
    }
    return 0;
}

static void stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}

bool i2c_probe(uint8_t addr7)
{
    if (start(addr7, false) < 0) {
        return false;
    }
    (void)I2C1->SR2; /* clear ADDR */
    stop();
    return true;
}

int i2c_write_reg16(uint8_t addr7, uint8_t reg, uint16_t value)
{
    if (start(addr7, false) < 0) {
        return -1;
    }
    (void)I2C1->SR2; /* clear ADDR */
    const uint8_t bytes[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    for (int i = 0; i < 3; i++) {
        if (wait_set(&I2C1->SR1, I2C_SR1_TXE) < 0) {
            stop();
            return -2;
        }
        I2C1->DR = bytes[i];
    }
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        stop();
        return -3;
    }
    stop();
    return 0;
}

int i2c_read_reg16(uint8_t addr7, uint8_t reg, uint16_t *out)
{
    /* Phase 1: write the register pointer. */
    if (start(addr7, false) < 0) {
        return -1;
    }
    (void)I2C1->SR2;
    if (wait_set(&I2C1->SR1, I2C_SR1_TXE) < 0) {
        stop();
        return -2;
    }
    I2C1->DR = reg;
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        stop();
        return -3;
    }

    /* Phase 2: repeated start, read 2 bytes (RM0090 N=2 POS method). */
    I2C1->CR1 |= I2C_CR1_ACK | I2C_CR1_POS;
    if (start(addr7, true) < 0) {
        return -4;
    }
    __disable_irq();
    (void)I2C1->SR2; /* clear ADDR */
    I2C1->CR1 &= ~I2C_CR1_ACK;
    __enable_irq();
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        stop();
        return -5;
    }
    __disable_irq();
    stop();
    uint8_t hi = (uint8_t)I2C1->DR;
    uint8_t lo = (uint8_t)I2C1->DR;
    __enable_irq();
    I2C1->CR1 &= ~I2C_CR1_POS;
    *out = (uint16_t)((hi << 8) | lo);
    return 0;
}
