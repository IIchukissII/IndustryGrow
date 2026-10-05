/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ds18b20.h"

#include "clock.h"
#include "onewire.h"

#define CMD_CONVERT_T        0x44u
#define CMD_READ_SCRATCHPAD  0xBEu

#define CONVERSION_US 750000u /* 12-bit, datasheet t_CONV max */
#define POWER_ON_RAW  0x0550  /* 85.0 C, the scratchpad's reset value */

/* One cycle as a flat sequence of bus operations:
 *
 *   CONVERT: reset, Skip ROM, Convert T
 *   WAIT:    750 ms, no bus traffic
 *   READ:    per assigned role -- reset, Match ROM + 8 code bytes,
 *            Read Scratchpad, 9 data bytes
 */
typedef enum {
    PH_CONVERT,
    PH_WAIT,
    PH_READ,
} phase_t;

#define READ_OPS (1u + 9u + 1u + 9u)

static const uint64_t *s_roms;
static phase_t s_phase;
static uint8_t s_op;      /* operation index within the phase or the read */
static uint8_t s_role;    /* role being read */
static uint64_t s_wait_start;
static uint8_t s_pad[9];
static bool s_present;    /* the last reset of this read saw a presence pulse */

static float s_kelvin[DS18B20_ROLES];
static uint64_t s_at[DS18B20_ROLES];
static bool s_valid[DS18B20_ROLES];

void ds18b20_init(const uint64_t *roms)
{
    s_roms = roms;
    s_phase = PH_CONVERT;
    s_op = 0u;
    ow_init();
}

static void finish_read(void)
{
    if (!s_present || (ow_crc8(s_pad, 8u) != s_pad[8])) {
        return;
    }
    const int16_t raw = (int16_t)((uint16_t)s_pad[0] | ((uint16_t)s_pad[1] << 8));
    if (raw == POWER_ON_RAW) {
        return;
    }
    s_kelvin[s_role] = (float)raw / 16.0f + 273.15f;
    s_at[s_role] = micros64();
    s_valid[s_role] = true;
}

/* Next role at or after `from` with a code assigned, or DS18B20_ROLES. */
static uint8_t next_role(uint8_t from)
{
    while ((from < DS18B20_ROLES) && (s_roms[from] == 0u)) {
        from++;
    }
    return from;
}

void ds18b20_poll(void)
{
    switch (s_phase) {
    case PH_CONVERT:
        if (s_op == 0u) {
            (void)ow_reset();
        } else if (s_op == 1u) {
            ow_write_byte(OW_CMD_SKIP_ROM);
        } else {
            ow_write_byte(CMD_CONVERT_T);
            s_wait_start = micros64();
            s_phase = PH_WAIT;
        }
        s_op++;
        break;

    case PH_WAIT:
        if ((micros64() - s_wait_start) >= CONVERSION_US) {
            s_role = next_role(0u);
            s_op = 0u;
            s_phase = (s_role < DS18B20_ROLES) ? PH_READ : PH_CONVERT;
        }
        break;

    case PH_READ: {
        const uint8_t op = s_op;
        if (op == 0u) {
            s_present = ow_reset();
        } else if (op == 1u) {
            ow_write_byte(OW_CMD_MATCH_ROM);
        } else if (op < 10u) {
            ow_write_byte((uint8_t)(s_roms[s_role] >> (8u * (op - 2u))));
        } else if (op == 10u) {
            ow_write_byte(CMD_READ_SCRATCHPAD);
        } else {
            s_pad[op - 11u] = ow_read_byte();
        }
        s_op++;
        if (s_op >= READ_OPS) {
            finish_read();
            s_role = next_role((uint8_t)(s_role + 1u));
            s_op = 0u;
            if (s_role >= DS18B20_ROLES) {
                s_phase = PH_CONVERT;
            }
        }
        break;
    }
    }
}

bool ds18b20_latest(size_t role, float *kelvin, uint64_t *at_us)
{
    if ((role >= DS18B20_ROLES) || !s_valid[role]) {
        return false;
    }
    *kelvin = s_kelvin[role];
    *at_us = s_at[role];
    return true;
}
