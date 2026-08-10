/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_BME68X_H
#define IGROW_M01_BME68X_H

#include <stdbool.h>
#include <stdint.h>

/*
 * U2 -- Bosch BME688 (M01 spec 4): gas/VOC resistance, barometric pressure, and
 * a secondary T/RH that must not reach the VPD computation. Address 0x76, set
 * by the SDO strap; 0x77 is unused on this board.
 *
 * VDD comes from U4's 1.8 V rail, VDDIO from the header 3.3 V (spec 7.4). The
 * part is characterized at VDD <= 1.8 V and the rail exists for that reason, so
 * the datasheet currents this driver's duty cycle assumes apply as written.
 *
 * Two roles, and the second one is the load-bearing one:
 *
 *  - VOC trend. Uncalibrated resistance against a per-device baseline, never an
 *    absolute threshold (spec 6.4). No IAQ, no CO2-equivalent, no bVOC -- those
 *    are BSEC outputs, and BSEC is a closed-source binary incompatible with the
 *    AGPL firmware (ADR-0002 d5). Humidity compensation and baseline tracking
 *    of the gas signal are BSEC functions too, and are therefore absent here.
 *  - Barometric pressure, which is what compensates the SCD41 (spec 6.3).
 *    Uncompensated pressure error propagates into CO2 at ~1:1, and this part's
 *    +/-0.6 hPa is ~0.25 ppm at 400 ppm against U3's +/-50 ppm floor -- two
 *    orders of margin, which is why the on-board barometer won over M07's.
 *    Pressure is oversampled x16 for that reason and no other.
 *
 * VARIANT DISCRIMINATION (spec 10). chip_id is 0x61 on BOTH the BME688 and the
 * approved BME680 alternative and cannot tell them apart. variant_id at 0xF0
 * can: 0x01 = BME688, 0x00 = BME680. It is not cosmetic -- the run_gas enable
 * bit and the gas-resistance conversion differ between the two, so a BME680
 * read with BME688 constants returns a plausible wrong number rather than an
 * error. Both variants are handled; spec 4.2's substitution stays a BOM line.
 */

#define BME68X_ADDR 0x76u

/* Hotplate setpoint and dwell, published alongside the resistance so a trend is
 * only ever compared against readings taken at the same setpoint. */
#define BME68X_HEATER_CELSIUS 320u
#define BME68X_HEATER_MS      150u

typedef struct {
    float celsius;      /* secondary -- NOT a VPD input (spec 4) */
    float pressure_pa;
    float rh_ratio;     /* secondary -- NOT a VPD input */
    float gas_ohm;
    bool gas_valid;     /* conversion completed AND the heater reached stability */
} bme68x_data_t;

/* Verify chip_id, latch variant_id, read the calibration block, and program the
 * oversampling and filter configuration. Returns 0 on success. */
int bme68x_init(void);

/* True when the fitted part is a BME688 rather than the BME680 alternative. */
bool bme68x_is_bme688(void);

/* Start one forced-mode conversion. `ambient_celsius` sets the heater resistance
 * for the target setpoint -- the hotplate is driven to a temperature ABOVE
 * ambient, so the code that reaches 320 C depends on where it starts. Feed it
 * U1's reading; 25 C is the datasheet reference if nothing better is known. */
int bme68x_trigger(float ambient_celsius);

/* Milliseconds from bme68x_trigger() to a completed conversion, from the
 * datasheet timing of the configured oversampling plus the heater dwell. The
 * caller waits this out in its own loop rather than blocking here: it is ~190 ms
 * against a 1 s publish cycle, and the Cyphal TX queue must keep flushing. */
uint32_t bme68x_meas_duration_ms(void);

/* Read and compensate the completed conversion. Returns 0 on success, <0 on a
 * bus error or if the device has not finished. */
int bme68x_read(bme68x_data_t *out);

#endif /* IGROW_M01_BME68X_H */
