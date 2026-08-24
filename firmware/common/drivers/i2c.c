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

/* Flags a failed transaction leaves behind, and why none of them may survive
 * into the next one. AF outlives a NACK: send() gives up waiting for a BTF that
 * will never arrive and its caller only issues a STOP, so the flag is still set
 * when start() runs again -- where it reads as a NACK of an address that has
 * not been sent yet. The abort that follows requests a STOP while the real
 * address phase is still on the wire, and the ACK arriving a moment later
 * latches ADDR with nobody left to clear it. From there every start() sees ADDR
 * already set and reports an address phase that never happened, i2c_probe()
 * included -- so a bus in that state answers "present" for every device on it
 * while not one transfer can complete.
 *
 * Bench 2026-08-24, M01 on E0002: one SCD41 command NACKed at boot left the
 * node publishing heartbeat only, all three sensors still reported present, for
 * as long as it was left running. Clearing ADDR by hand from a debugger started
 * all ten subjects inside five seconds. */
static void clear_stale_flags(void)
{
    if (I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
    }
    if (I2C1->SR1 & I2C_SR1_ADDR) {
        (void)I2C1->SR2; /* SR1 was just read: RM0090's ADDR-clear sequence */
    }
}

/* Close a failed transfer so the next one starts on a quiet bus. The wait is
 * the part that matters: an aborted address phase sets ADDR AFTER its STOP is
 * requested, so clearing before the bus falls idle clears a flag that has not
 * arrived yet. A bus that never falls idle is not a flag problem -- SWRST is
 * the documented escape (RM0090 27.6.1) and i2c_init() re-applies the pin and
 * timing configuration the reset drops. */
static void abort_transfer(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
    uint32_t g = I2C_TMO;
    while ((I2C1->SR2 & I2C_SR2_BUSY) && --g) {
    }
    clear_stale_flags();
    if (g == 0u) {
        i2c_init();
    }
}

static int start(uint8_t addr7, bool read)
{
    clear_stale_flags();

    /* BUSY with MSL clear is a segment this peripheral does not hold: a lost
     * arbitration, or a device still clocking out a byte from a transfer that
     * was abandoned under it. Neither is reachable through the status
     * registers. A repeated START arrives here with MSL set and is left alone. */
    const uint32_t sr2 = I2C1->SR2;
    if ((sr2 & I2C_SR2_BUSY) && !(sr2 & I2C_SR2_MSL)) {
        i2c_init();
    }

    I2C1->CR1 |= I2C_CR1_START;
    if (wait_set(&I2C1->SR1, I2C_SR1_SB) < 0) {
        abort_transfer();
        return -1;
    }
    I2C1->DR = (uint8_t)((addr7 << 1) | (read ? 1u : 0u));
    uint32_t g = I2C_TMO;
    while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)) && --g) {
    }
    if ((g == 0u) || (I2C1->SR1 & I2C_SR1_AF)) {
        abort_transfer(); /* clears AF, and the ADDR the abort itself can latch */
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

/* Like wait_set() on SR1, but gives up the moment the device NACKs. A byte that
 * was not acknowledged is never followed by the flag being waited for, so the
 * plain form spends a full timeout -- and then another on the BTF that closes
 * the transfer -- to reach an answer the AF already gave. That is the wait a
 * rejected command runs into, and it is charged to the watchdog window. */
static int wait_sr1_or_nack(uint32_t mask)
{
    uint32_t g = I2C_TMO;
    while (!(I2C1->SR1 & (mask | I2C_SR1_AF)) && --g) {
    }
    return ((g == 0u) || (I2C1->SR1 & I2C_SR1_AF)) ? -1 : 0;
}

/* Address + payload, leaving the bus WITHOUT a STOP so the caller can either
 * close it or turn it round with a repeated START. A failure leaves the bus
 * held too -- the caller owns the abort, because only the caller knows whether
 * a repeated START was going to follow. */
static int send(uint8_t addr7, const uint8_t *buf, size_t len)
{
    if (start(addr7, false) < 0) {
        return -1;
    }
    (void)I2C1->SR2; /* clear ADDR */
    for (size_t i = 0; i < len; i++) {
        if (wait_sr1_or_nack(I2C_SR1_TXE) < 0) {
            return -2;
        }
        I2C1->DR = buf[i];
    }
    if (wait_sr1_or_nack(I2C_SR1_BTF) < 0) {
        return -3;
    }
    return 0;
}

/* Master reception, RM0090 27.3.3. The three length cases are genuinely
 * different sequences on this peripheral, not an optimization: the NACK for
 * the final byte has to be programmed before that byte is clocked in, and how
 * far ahead of it that is depends on how many bytes are still in flight. */
static int recv(uint8_t addr7, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return -1;
    }

    if (len == 1u) {
        /* ACK off BEFORE the address is cleared — the single byte is also the
         * last one, and clearing ADDR starts clocking it in. */
        I2C1->CR1 &= ~(I2C_CR1_ACK | I2C_CR1_POS);
        if (start(addr7, true) < 0) {
            return -4;
        }
        __disable_irq();
        (void)I2C1->SR2; /* clear ADDR */
        I2C1->CR1 |= I2C_CR1_STOP;
        __enable_irq();
        if (wait_set(&I2C1->SR1, I2C_SR1_RXNE) < 0) {
            abort_transfer();
            return -5;
        }
        buf[0] = (uint8_t)I2C1->DR;
        return 0;
    }

    if (len == 2u) {
        /* POS defers the NACK to the byte after next, which is the only way to
         * NACK byte 2 while byte 1 is still being received. */
        I2C1->CR1 |= I2C_CR1_ACK | I2C_CR1_POS;
        if (start(addr7, true) < 0) {
            I2C1->CR1 &= ~I2C_CR1_POS;
            return -4;
        }
        __disable_irq();
        (void)I2C1->SR2; /* clear ADDR */
        I2C1->CR1 &= ~I2C_CR1_ACK;
        __enable_irq();
        if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
            abort_transfer();
            I2C1->CR1 &= ~I2C_CR1_POS;
            return -5;
        }
        __disable_irq();
        I2C1->CR1 |= I2C_CR1_STOP;
        buf[0] = (uint8_t)I2C1->DR;
        buf[1] = (uint8_t)I2C1->DR;
        __enable_irq();
        I2C1->CR1 &= ~I2C_CR1_POS;
        return 0;
    }

    /* len >= 3: stream on ACK until three remain, then close out on BTF. */
    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;
    if (start(addr7, true) < 0) {
        return -4;
    }
    (void)I2C1->SR2; /* clear ADDR */

    size_t i = 0;
    while ((len - i) > 3u) {
        if (wait_set(&I2C1->SR1, I2C_SR1_RXNE) < 0) {
            abort_transfer();
            return -5;
        }
        buf[i++] = (uint8_t)I2C1->DR;
    }
    /* BTF here means N-2 is in DR and N-1 in the shift register, with SCL held
     * low — so the NACK for N lands in time. */
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        abort_transfer();
        return -6;
    }
    I2C1->CR1 &= ~I2C_CR1_ACK;
    buf[i++] = (uint8_t)I2C1->DR; /* data N-2 */
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        abort_transfer();
        return -7;
    }
    __disable_irq();
    I2C1->CR1 |= I2C_CR1_STOP;
    buf[i++] = (uint8_t)I2C1->DR; /* data N-1 */
    buf[i] = (uint8_t)I2C1->DR;   /* data N */
    __enable_irq();
    return 0;
}

int i2c_write(uint8_t addr7, const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return -1;
    }
    int rc = send(addr7, buf, len);
    if (rc < 0) {
        abort_transfer();
        return rc;
    }
    stop();
    return 0;
}

int i2c_read(uint8_t addr7, uint8_t *buf, size_t len)
{
    return recv(addr7, buf, len);
}

int i2c_write_read(uint8_t addr7, const uint8_t *wbuf, size_t wlen,
                   uint8_t *rbuf, size_t rlen)
{
    if (wlen == 0u) {
        return -1;
    }
    int rc = send(addr7, wbuf, wlen);
    if (rc < 0) {
        abort_transfer();
        return rc;
    }
    return recv(addr7, rbuf, rlen); /* repeated START, no STOP in between */
}

int i2c_write_reg16(uint8_t addr7, uint8_t reg, uint16_t value)
{
    if (start(addr7, false) < 0) {
        return -1;
    }
    (void)I2C1->SR2; /* clear ADDR */
    const uint8_t bytes[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    for (int i = 0; i < 3; i++) {
        if (wait_sr1_or_nack(I2C_SR1_TXE) < 0) {
            abort_transfer();
            return -2;
        }
        I2C1->DR = bytes[i];
    }
    if (wait_sr1_or_nack(I2C_SR1_BTF) < 0) {
        abort_transfer();
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
    if (wait_sr1_or_nack(I2C_SR1_TXE) < 0) {
        abort_transfer();
        return -2;
    }
    I2C1->DR = reg;
    if (wait_sr1_or_nack(I2C_SR1_BTF) < 0) {
        abort_transfer();
        return -3;
    }

    /* Phase 2: repeated start, read 2 bytes (RM0090 N=2 POS method). */
    I2C1->CR1 |= I2C_CR1_ACK | I2C_CR1_POS;
    if (start(addr7, true) < 0) {
        I2C1->CR1 &= ~I2C_CR1_POS; /* POS left set NACKs the wrong byte of the next read */
        return -4;
    }
    __disable_irq();
    (void)I2C1->SR2; /* clear ADDR */
    I2C1->CR1 &= ~I2C_CR1_ACK;
    __enable_irq();
    if (wait_set(&I2C1->SR1, I2C_SR1_BTF) < 0) {
        abort_transfer();
        I2C1->CR1 &= ~I2C_CR1_POS;
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
