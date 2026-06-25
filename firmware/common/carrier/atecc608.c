/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "atecc608.h"
#include "e0001.h"  /* CMSIS device header + E0001_SECURE_* pin map */
#include "clock.h"  /* delay_ms, micros64 */

#include <string.h>

/* I2C2 master on the carrier's secure-element bus (PB10/PB11, AF4), 100 kHz —
 * a self-contained blocking master kept separate from the I2C1 sensor driver
 * because the ATECC608 speaks a command/response protocol, not 16-bit registers,
 * and lives on a different peripheral. */
#define ATECC_ADDR7   0x60u  /* default ATECC608 I2C address (7-bit) */
#define I2C_TMO       100000u

/* ATECC608 I2C word-address bytes (datasheet "I/O blocks"). */
#define ATECC_WA_SLEEP   0x01u
#define ATECC_WA_COMMAND 0x03u

/* Read opcode + Config-zone, 32-byte read of block 0 (holds the serial number).
 * Config bytes 0..3 and 8..12 are the 9-byte serial (datasheet config-zone map). */
#define ATECC_OP_READ    0x02u
#define ATECC_READ_32    0x80u  /* param1 bit7: 32-byte read; zone bits = 0 (Config) */

static bool    s_present;
static uint8_t s_serial[ATECC608_SERIAL_LEN];

/* ---- microsecond busy-wait (SysTick-derived; clock.h has only delay_ms) ---- */
static void delay_us(uint32_t us)
{
    uint64_t t0 = micros64();
    while ((micros64() - t0) < (uint64_t)us) {
    }
}

/* ---- ATECC CRC-16 (poly 0x8005, LSB-first per byte; Microchip atCRC) -------- */
static uint16_t atca_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0u;
    for (size_t i = 0; i < len; i++) {
        for (uint8_t mask = 0x01u; mask != 0x00u; mask <<= 1) {
            uint8_t data_bit = (data[i] & mask) ? 1u : 0u;
            uint8_t crc_bit = (uint8_t)((crc >> 15) & 1u);
            crc = (uint16_t)(crc << 1);
            if (data_bit != crc_bit) {
                crc ^= 0x8005u;
            }
        }
    }
    return crc; /* transmitted little-endian: low byte first */
}

/* ---- low-level I2C2 ---------------------------------------------------------- */
static int wait_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t g = I2C_TMO;
    while (!(*reg & mask) && --g) {
    }
    return g ? 0 : -1;
}

static void i2c2_pins_af(void)
{
    const uint32_t scl = E0001_SECURE_SCL_PIN, sda = E0001_SECURE_SDA_PIN; /* 10, 11 */
    /* AF4, open-drain, pull-up, high speed. */
    E0001_SECURE_GPIO->MODER &= ~((3u << (scl * 2u)) | (3u << (sda * 2u)));
    E0001_SECURE_GPIO->MODER |= (2u << (scl * 2u)) | (2u << (sda * 2u));
    E0001_SECURE_GPIO->OTYPER |= (1u << scl) | (1u << sda);
    E0001_SECURE_GPIO->OSPEEDR |= (3u << (scl * 2u)) | (3u << (sda * 2u));
    E0001_SECURE_GPIO->PUPDR &= ~((3u << (scl * 2u)) | (3u << (sda * 2u)));
    E0001_SECURE_GPIO->PUPDR |= (1u << (scl * 2u)) | (1u << (sda * 2u));
    /* PB10/PB11 are AFR[1] (pins 8-15), each nibble at (pin-8)*4. */
    E0001_SECURE_GPIO->AFR[1] &= ~((0xFu << ((scl - 8u) * 4u)) | (0xFu << ((sda - 8u) * 4u)));
    E0001_SECURE_GPIO->AFR[1] |= (4u << ((scl - 8u) * 4u)) | (4u << ((sda - 8u) * 4u));
}

static void i2c2_periph_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
    (void)RCC->APB1ENR;

    i2c2_pins_af();

    /* Reset then configure for 100 kHz on PCLK1 = 42 MHz (mirrors the I2C1 driver). */
    I2C2->CR1 = I2C_CR1_SWRST;
    I2C2->CR1 = 0u;
    I2C2->CR2 = 42u;     /* FREQ = APB1 MHz */
    I2C2->CCR = 210u;    /* Sm: 42e6 / (2 * 100e3) */
    I2C2->TRISE = 43u;   /* FREQ + 1 */
    I2C2->CR1 = I2C_CR1_PE;
}

static int i2c2_start(bool read)
{
    I2C2->CR1 |= I2C_CR1_START;
    if (wait_set(&I2C2->SR1, I2C_SR1_SB) < 0) {
        return -1;
    }
    I2C2->DR = (uint8_t)((ATECC_ADDR7 << 1) | (read ? 1u : 0u));
    uint32_t g = I2C_TMO;
    while (!(I2C2->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)) && --g) {
    }
    if ((g == 0u) || (I2C2->SR1 & I2C_SR1_AF)) {
        I2C2->SR1 &= ~I2C_SR1_AF;
        I2C2->CR1 |= I2C_CR1_STOP;
        return -2; /* no ACK -> device absent/asleep */
    }
    return 0;
}

static void i2c2_stop(void)
{
    I2C2->CR1 |= I2C_CR1_STOP;
}

/* Write n bytes (buf includes the leading word-address byte). */
static int i2c2_write(const uint8_t *buf, size_t n)
{
    if (i2c2_start(false) < 0) {
        return -1;
    }
    (void)I2C2->SR2; /* clear ADDR */
    for (size_t i = 0; i < n; i++) {
        if (wait_set(&I2C2->SR1, I2C_SR1_TXE) < 0) {
            i2c2_stop();
            return -2;
        }
        I2C2->DR = buf[i];
    }
    if (wait_set(&I2C2->SR1, I2C_SR1_BTF) < 0) {
        i2c2_stop();
        return -3;
    }
    i2c2_stop();
    return 0;
}

/* Read n bytes (n >= 3 for ATECC responses) — RM0090 multi-byte master receive. */
static int i2c2_read(uint8_t *buf, size_t n)
{
    if (n < 3u) {
        return -1; /* this driver only issues >=3-byte reads */
    }
    I2C2->CR1 |= I2C_CR1_ACK;
    if (i2c2_start(true) < 0) {
        return -2;
    }
    (void)I2C2->SR2; /* clear ADDR */

    size_t i = 0;
    while ((n - i) > 3u) {
        if (wait_set(&I2C2->SR1, I2C_SR1_RXNE) < 0) {
            i2c2_stop();
            return -3;
        }
        buf[i++] = (uint8_t)I2C2->DR; /* ACK stays enabled */
    }
    /* 3 bytes remain: wait BTF (DR=N-3, shift=N-2), then NACK + STOP sequence. */
    if (wait_set(&I2C2->SR1, I2C_SR1_BTF) < 0) {
        i2c2_stop();
        return -4;
    }
    I2C2->CR1 &= ~I2C_CR1_ACK;
    __disable_irq();
    buf[i++] = (uint8_t)I2C2->DR; /* read N-3 */
    I2C2->CR1 |= I2C_CR1_STOP;
    buf[i++] = (uint8_t)I2C2->DR; /* read N-2 */
    __enable_irq();
    if (wait_set(&I2C2->SR1, I2C_SR1_RXNE) < 0) {
        return -5;
    }
    buf[i++] = (uint8_t)I2C2->DR; /* read N-1 */
    return 0;
}

/* ---- ATECC608 protocol ------------------------------------------------------ */

/* Wake: hold SDA low >= tWLO (60 us) by driving PB11 low as GPIO, release, then
 * wait tWHI (~1.5 ms) before talking. Toggles SDA out of AF for the pulse. */
static void atecc_wake(void)
{
    const uint32_t sda = E0001_SECURE_SDA_PIN; /* 11 */
    I2C2->CR1 &= ~I2C_CR1_PE;
    /* PB11 -> push-pull output, drive 0. */
    E0001_SECURE_GPIO->MODER &= ~(3u << (sda * 2u));
    E0001_SECURE_GPIO->MODER |= (1u << (sda * 2u));
    E0001_SECURE_GPIO->OTYPER &= ~(1u << sda);
    E0001_SECURE_GPIO->BSRR = (1u << (sda + 16u)); /* SDA low */
    delay_us(80u);
    E0001_SECURE_GPIO->BSRR = (1u << sda);         /* release high */
    /* Restore AF/open-drain and re-enable the peripheral. */
    i2c2_pins_af();
    I2C2->CR1 = I2C_CR1_PE;
    delay_ms(2u); /* tWHI */
}

static void atecc_sleep(void)
{
    const uint8_t b = ATECC_WA_SLEEP;
    (void)i2c2_write(&b, 1u);
}

/* Read the 9-byte serial via Read(Config, block 0, 32 bytes). Returns 0 on a
 * CRC-valid read, <0 on absence / NACK / timeout / CRC failure. */
static int atecc_read_serial(uint8_t out[ATECC608_SERIAL_LEN])
{
    atecc_wake();

    /* After wake, the device returns a 4-byte status block {0x04,0x11,crc,crc}
     * (0x11 = "after wake, before first command"). Use it as a presence check. */
    uint8_t wake_resp[4];
    if (i2c2_read(wake_resp, 4u) < 0) {
        atecc_sleep();
        return -1;
    }
    if ((wake_resp[0] != 0x04u) || (wake_resp[1] != 0x11u)) {
        atecc_sleep();
        return -2;
    }

    /* Command block: [WA=Command][count=7][Read][param1][param2_lo][param2_hi][crc16] */
    uint8_t cmd[8];
    cmd[0] = ATECC_WA_COMMAND;
    cmd[1] = 7u; /* count: bytes [count .. crc_hi] */
    cmd[2] = ATECC_OP_READ;
    cmd[3] = ATECC_READ_32; /* 32-byte read, Config zone */
    cmd[4] = 0x00u;         /* param2 LSB: block 0, offset 0 */
    cmd[5] = 0x00u;         /* param2 MSB */
    uint16_t ccrc = atca_crc16(&cmd[1], 5u); /* CRC over count..param2 */
    cmd[6] = (uint8_t)(ccrc & 0xFFu);
    cmd[7] = (uint8_t)(ccrc >> 8);
    if (i2c2_write(cmd, sizeof(cmd)) < 0) {
        atecc_sleep();
        return -3;
    }

    delay_ms(5u); /* Read execution time (datasheet max ~ a few ms) */

    /* Response: [count=35][32 data][crc_lo][crc_hi]. */
    uint8_t resp[35];
    if (i2c2_read(resp, sizeof(resp)) < 0) {
        atecc_sleep();
        return -4;
    }
    if (resp[0] != 35u) {
        atecc_sleep();
        return -5;
    }
    uint16_t rcrc = atca_crc16(resp, 33u); /* CRC over count + 32 data */
    if (((uint8_t)(rcrc & 0xFFu) != resp[33]) || ((uint8_t)(rcrc >> 8) != resp[34])) {
        atecc_sleep();
        return -6;
    }

    /* Serial = config-zone bytes 0..3 and 8..12; data byte k is resp[1 + k]. */
    out[0] = resp[1];
    out[1] = resp[2];
    out[2] = resp[3];
    out[3] = resp[4];
    out[4] = resp[9];
    out[5] = resp[10];
    out[6] = resp[11];
    out[7] = resp[12];
    out[8] = resp[13];

    atecc_sleep();
    return 0;
}

void atecc608_init(void)
{
    s_present = false;
    memset(s_serial, 0, sizeof(s_serial));

    i2c2_periph_init();

    uint8_t sn[ATECC608_SERIAL_LEN];
    if (atecc_read_serial(sn) == 0) {
        memcpy(s_serial, sn, sizeof(s_serial));
        s_present = true;
    }
}

bool atecc608_present(void)
{
    return s_present;
}

const uint8_t *atecc608_serial(void)
{
    return s_serial;
}
