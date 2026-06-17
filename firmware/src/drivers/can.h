/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_CAN_H
#define IGROW_DRIVERS_CAN_H

#include <stdbool.h>
#include <stdint.h>

/* bxCAN1 on PB8/PB9 at 500 kbit/s (ADR-0002 d8). Bit timing for APB1 = 42 MHz:
 * BRP 6, BS1 11, BS2 2, SJW 1 -> 42e6 / (6 * (1+11+2)) = 500000, sample 85.7%.
 *
 * `loopback` selects internal loopback + silent mode for a self-test with no
 * bus or transceiver attached; false is normal mode for the live bus. Filter 0
 * is set to accept-all into FIFO0. Returns 0 on success, non-zero on timeout. */
int can_init(bool loopback);

/* Convenience for normal (live-bus) operation. */
static inline int can_init_normal(void) { return can_init(false); }

/* Send one classic-CAN frame (standard 11-bit id). Returns 0 if queued. */
int can_send(uint16_t id, const uint8_t *data, uint8_t len);

/* Non-blocking receive from FIFO0. Returns 1 if a frame was read, else 0. */
int can_recv(uint16_t *id, uint8_t *data, uint8_t *len);

/* Internal-loopback round trip of one frame; 0 = peripheral + timing OK. */
int can_selftest_loopback(void);

#endif /* IGROW_DRIVERS_CAN_H */
