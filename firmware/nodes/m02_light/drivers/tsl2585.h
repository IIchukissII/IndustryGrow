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

/* --- Bench instrument (M02 spec 6.4 V4) ---------------------------------
 *
 * A raw readback of the registers the published sample is derived from, so
 * that a disagreement between the sample and the device can be attributed to
 * one of them. It exists because `valid` is a conclusion drawn from three
 * registers at once: when it reads false there is nothing in the published
 * record that says which of them said so.
 *
 * Ordered as the read path uses them. STATUS2 is taken first and alone, for
 * the reason tsl2585_read_uv() gives; the others follow in one transaction.
 * A dump is therefore a second acquisition, not a copy of the last one. */
typedef struct {
    uint8_t id;            /* 0x92: 0x5C, or the dump came from the wrong device */
    uint8_t status2;       /* 0x9D: ALS_DATA_VALID, digital + analog saturation */
    uint8_t als_status;    /* 0x94: per-data-register saturation and scaling */
    uint8_t als_status2;   /* 0x9B: gain in force for ALS_DATA0 / ALS_DATA1 */
    uint8_t als_status3;   /* 0x9C: gain in force for ALS_DATA2, the UV path */
    uint8_t enable;        /* 0x80: PON and AEN as the device holds them */
    uint8_t meas_mode0;    /* 0x81: ALS_SCALE actually in force */
    uint8_t mod_ctrl;      /* 0x40: which modulators are enabled */
    uint8_t gain_step0_l;  /* 0xD4: modulator 0 and 1 gain codes as written */
    uint8_t gain_step0_h;  /* 0xD5: modulator 2 gain code as written */
    uint8_t smux_step0_l;  /* 0xDC: photodiodes 0-3 to modulators */
    uint8_t smux_step0_h;  /* 0xDD: photodiodes 4-5 to modulators */
    uint8_t nr_samples0;   /* 0x85: integration length, low byte */
    uint8_t nr_samples1;   /* 0x86: integration length, high bits */
    uint8_t agc_asat;      /* 0xDF: [7:4] must be 0 or the AGC owns the gain */
    uint8_t agc_predict;   /* 0xE1: [7:4] the same, for predict AGC */
} tsl2585_bench_t;

/* Read the block above. Returns 0 on success, <0 on a bus failure. Reads only;
 * the ALS_STATUS read it performs clears ALS_DATA_VALID exactly as the normal
 * sample path does, so calling it does not leave the device in a new state. */
int tsl2585_bench_dump(tsl2585_bench_t *out);

#endif /* IGROW_DRIVERS_TSL2585_H */
