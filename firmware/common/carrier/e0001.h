/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Board support for the IndustryGrow universal carrier E0001 — the PARENT board
 * that hosts the WeAct STM32F4 64-Pin Core Board (STM32F405RGT6, SP0005) and is
 * shared by every node type. A sensor-module personality (E0002..E0006) plugs
 * into the carrier as a CHILD: the carrier owns the bus, the LEDs, the MCU
 * socket and — crucially — the node IDENTITY (module-ID straps + the ATECC608
 * secure element); the child only asserts a strap pattern and adds sensors.
 * For that reason the per-node expected strap value does NOT live here — it
 * belongs to the node (e.g. nodes/m05_safety/module_id.h).
 *
 * Pin assignments below are NOT decided here — the authoritative source is
 * store/E0001-000001-D-pinmap.md. This header only restates them in code form.
 */
#ifndef IGROW_CARRIER_E0001_H
#define IGROW_CARRIER_E0001_H

#include "stm32f4xx.h" /* CMSIS device header (third_party/cmsis_device_f4) */
#include <stdbool.h>
#include <stdint.h>

/* --- CAN1: PB8 = RX, PB9 = TX, AF9 (pin-map: bxCAN1, 500 kbit/s) --- */
#define E0001_CAN_GPIO        GPIOB
#define E0001_CAN_RX_PIN      8u
#define E0001_CAN_TX_PIN      9u
#define E0001_CAN_AF          9u

/* --- Status / CAN-activity LEDs (pin-map: PA1 = status, PA2 = CAN) --- *
 * Carrier LED polarity is not stated in the pin map; assumed active-high.
 * Flip E0001_LED_ACTIVE_HIGH to 0 if the carrier sinks the LED through the pin. */
#define E0001_LED_ACTIVE_HIGH 1
#define E0001_LED_GPIO        GPIOA
#define E0001_LED_STATUS_PIN  1u
#define E0001_LED_CAN_PIN     2u

/* --- Module-ID straps (pin-map: PA5/PA6/PA7 = STRAP_0/1/2; ADR-0014 d6) --- *
 * Read as inputs with pull-down: a child module ties its ID bits to 3V3 (=1) and
 * leaves the 0 bits to the pull-down. The 3-bit pattern selects which node type
 * is mounted on the carrier; the expected value per node lives in that node. */
#define E0001_STRAP_GPIO      GPIOA
#define E0001_STRAP0_PIN      5u
#define E0001_STRAP1_PIN      6u  /* unrouted on E0001-000001 — see gap below */
#define E0001_STRAP2_PIN      7u

/* KNOWN HARDWARE GAP (carrier rev E0001-000001): STRAP_1 (PA6) is unrouted to
 * the MCU (PCB diverged from the pin map), so module-ID bit 1 always reads the
 * pull-down 0. This is a CARRIER-LEVEL fact — every node inherits it until a
 * carrier respin routes PA6. Bits 0 and 2 are the only reliably-readable bits,
 * so a node compares the readable bits (see E0001_MODULE_ID_READABLE_MASK).
 * Benign for M05 (0b101, bit1=0); breaks M02/M03/M06 (bit1=1) self-ID. */
#define E0001_STRAP1_UNROUTED         1u
#define E0001_MODULE_ID_READABLE_MASK 0x5u /* bits 0 and 2 (bit 1 unreadable) */

/* --- Secure element / node identity IC: ATECC608 on I2C2 (PB10 = SCL,
 * PB11 = SDA), per the pin map and ADR-0007 (PKI). This is the carrier's
 * per-instance cryptographic identity anchor — a PARENT responsibility, not the
 * child module's. NOT YET WIRED UP IN FIRMWARE: I2C2 and an ATECC driver are a
 * tracked seam to add at the carrier level (no I2C2 init exists today). When
 * implemented it lives here / in a sibling common/carrier unit, never per-node. */
#define E0001_SECURE_I2C        I2C2
#define E0001_SECURE_GPIO       GPIOB
#define E0001_SECURE_SCL_PIN    10u
#define E0001_SECURE_SDA_PIN    11u

/* --- Debug UART: USART1 PA9 = TX, PA10 = RX, AF7 (bench bring-up only) --- *
 * These pins are GPIO_1/GPIO_2 on the sensor-module header; M05 uses GPIO_3/4
 * (reed/S0), so PA9/PA10 are free for a debug console on the bench. */
#define E0001_DBG_UART        USART1
#define E0001_DBG_GPIO        GPIOA
#define E0001_DBG_TX_PIN      9u
#define E0001_DBG_RX_PIN      10u
#define E0001_DBG_AF          7u
#define E0001_DBG_BAUD        115200u

/* Bring up GPIO clocks and configure LEDs + straps. Call after clock_init(). */
void e0001_init(void);

/* Read the 3-bit module-ID strap pattern (bit0=STRAP_0 .. bit2=STRAP_2).
 * Bit 1 is forced to 0 on E0001-000001 (STRAP_1/PA6 unrouted, see above). */
uint8_t e0001_read_module_id(void);

void e0001_led_status(bool on);
void e0001_led_can(bool on);
void e0001_led_status_toggle(void);

#endif /* IGROW_CARRIER_E0001_H */
