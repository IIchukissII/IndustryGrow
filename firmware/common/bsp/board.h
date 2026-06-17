/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Board support for the IndustryGrow universal carrier (E0001) hosting the
 * WeAct STM32F4 64-Pin Core Board (STM32F405RGT6), populated as the M05-SAFETY
 * node (E0006).
 *
 * Pin assignments below are NOT decided here — the authoritative source is
 * store/E0001-000001-D-pinmap.md. This header only restates them in code form.
 */
#ifndef IGROW_BSP_BOARD_H
#define IGROW_BSP_BOARD_H

#include "stm32f4xx.h" /* CMSIS device header (third_party/cmsis_device_f4) */
#include <stdbool.h>
#include <stdint.h>

/* --- CAN1: PB8 = RX, PB9 = TX, AF9 (pin-map: bxCAN1, 500 kbit/s) --- */
#define BRD_CAN_GPIO        GPIOB
#define BRD_CAN_RX_PIN      8u
#define BRD_CAN_TX_PIN      9u
#define BRD_CAN_AF          9u

/* --- Status / CAN-activity LEDs (pin-map: PA1 = status, PA2 = CAN) --- *
 * Carrier LED polarity is not stated in the pin map; assumed active-high.
 * Flip BRD_LED_ACTIVE_HIGH to 0 if the carrier sinks the LED through the pin. */
#define BRD_LED_ACTIVE_HIGH 1
#define BRD_LED_GPIO        GPIOA
#define BRD_LED_STATUS_PIN  1u
#define BRD_LED_CAN_PIN     2u

/* --- Module-ID straps (pin-map: PA5/PA6/PA7 = STRAP_0/1/2; ADR-0014 d6) --- *
 * Read as inputs with pull-down: a module ties its ID bits to 3V3 (=1) and
 * leaves the 0 bits to the pull-down. KNOWN HARDWARE GAP: on E0001-000001,
 * STRAP_1 (PA6) is unrouted to the MCU (pin-map note / tracked carrier fix),
 * so bit 1 always reads the pull-down 0. That happens to match M05 (0b101),
 * but it is a real gap for M02/M03/M06 — see board_read_module_id(). */
#define BRD_STRAP_GPIO      GPIOA
#define BRD_STRAP0_PIN      5u
#define BRD_STRAP1_PIN      6u  /* unrouted on E0001-000001 */
#define BRD_STRAP2_PIN      7u

/* --- Debug UART: USART1 PA9 = TX, PA10 = RX, AF7 (bench bring-up only) --- *
 * These pins are GPIO_1/GPIO_2 on the sensor-module header; M05 uses GPIO_3/4
 * (reed/S0), so PA9/PA10 are free for a debug console on the bench. */
#define BRD_DBG_UART        USART1
#define BRD_DBG_GPIO        GPIOA
#define BRD_DBG_TX_PIN      9u
#define BRD_DBG_RX_PIN      10u
#define BRD_DBG_AF          7u
#define BRD_DBG_BAUD        115200u

/* Module-ID strap pattern expected for M05-SAFETY (ADR-0014 d6: 0b101). */
#define BRD_MODULE_ID_M05   0x5u

/* Bring up GPIO clocks and configure LEDs + straps. Call after clock_init(). */
void board_init(void);

/* Read the 3-bit module-ID strap pattern (bit0=STRAP_0 .. bit2=STRAP_2).
 * Bit 1 is forced to 0 on E0001-000001 (STRAP_1/PA6 unrouted, see above). */
uint8_t board_read_module_id(void);

void board_led_status(bool on);
void board_led_can(bool on);
void board_led_status_toggle(void);

#endif /* IGROW_BSP_BOARD_H */
