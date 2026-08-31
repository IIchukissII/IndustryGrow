/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_TSL2585_H
#define IGROW_DRIVERS_TSL2585_H

#include <stdbool.h>
#include <stdint.h>

/* ams OSRAM TSL2585, M02's UV-A sensor: 315-400 nm through a band-pass filter,
 * covering the profile's 365-385 nm trace (M02 spec 6.4).
 *
 * Address 0x39 -- the same fixed address as the AS7343, which is why both sit
 * behind the bus switch and why nothing here touches it. Reach this device
 * only with the switch on TCA9543A_CH_U3 (M02 spec 5.2).
 *
 * The part also carries photopic and IR photodiodes. Neither is published
 * (M02 spec 10.1): they exist to support the vendor's UV-index algorithm, and
 * the AS7343 already measures the visible spectrum with eleven bands. They are
 * still MEASURED -- the device has three modulators whether or not two of them
 * are read, and disabling them would buy nothing but a different set of
 * defaults to be wrong about. */
#define TSL2585_ADDR 0x39u

typedef struct {
    uint16_t raw;                 /* modulator-2 counts, as read */
    float watt_per_square_metre;  /* nominal; see the responsivity note below */
    float gain;                   /* the gain the device reports for this data */
    bool valid;                   /* converted this cycle and did not saturate */
} tsl2585_uv_t;

/* Identify the part: ID (0x92) must read 0x5C (M02 spec 10, V11). An address
 * ACK is not identification, and at 0x39 an ACK could equally be the AS7343 on
 * the wrong switch channel. */
bool tsl2585_present(void);

/* Configure the UV path and start the ALS engine. Routes both UV-A photodiodes
 * to modulator 2 and reads the device's own UV calibration factor. Returns 0
 * on success. */
int tsl2585_init(void);

/* The most recent UV-A conversion. Returns 0 on success, <0 on a bus failure.
 * `out->valid` is false where the engine had not completed a cycle or the
 * modulator saturated -- a bus success with an invalid sample is a normal
 * outcome, not an error. */
int tsl2585_read_uv(tsl2585_uv_t *out);

#endif /* IGROW_DRIVERS_TSL2585_H */
