/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "flatfield.h"

#include "crc32.h"
#include "m24c64.h"

#include <string.h>

/* Record layout (M04 spec 6.7). Little-endian throughout, as the served frame
 * header is, so one reader decodes both.
 *
 *   0  magic "IGFF"          16  determination scene temperature, float32 K
 *   4  version               20  determination Ta, float32 K
 *   5  kind                  24  determination time, uint32 s
 *   6  pixel count           28  CRC-32 over bytes 0..27 and 32..1567
 *   8  U1 device ID, 3 x u16 32  768 x int16, 0.01 K per LSB
 *  14  reserved
 */
#define OFF_MAGIC     0u
#define OFF_VERSION   4u
#define OFF_KIND      5u
#define OFF_PIXELS    6u
#define OFF_DEVICE_ID 8u
#define OFF_CRC       28u
#define OFF_TABLE     FLATFIELD_HEADER_BYTES

/* Word-aligned, because the CRC unit is fed words and the table is read as
 * int16 in place. */
static union {
    uint32_t words[FLATFIELD_RECORD_BYTES / 4u];
    uint8_t bytes[FLATFIELD_RECORD_BYTES];
} s_rec;

static bool s_applied;
static const char *s_state = "absent";

static bool s_commit_active;
static bool s_commit_wait;
static uint16_t s_commit_off;

static uint32_t rd_u32(uint16_t off)
{
    uint32_t v;
    memcpy(&v, &s_rec.bytes[off], sizeof(v));
    return v;
}

static uint16_t rd_u16(uint16_t off)
{
    uint16_t v;
    memcpy(&v, &s_rec.bytes[off], sizeof(v));
    return v;
}

/* 0 accepted; -1 malformed; -2 CRC; -3 bound to another imager. */
static int validate(void)
{
    if ((rd_u32(OFF_MAGIC) != FLATFIELD_MAGIC) ||
        (s_rec.bytes[OFF_VERSION] != FLATFIELD_VERSION) ||
        (s_rec.bytes[OFF_KIND] != FLATFIELD_KIND_ONE_POINT) ||
        (rd_u16(OFF_PIXELS) != MLX90640_PIXELS)) {
        return -1;
    }

    crc32_mpeg2_begin();
    crc32_mpeg2_feed(&s_rec.bytes[0], OFF_CRC);
    crc32_mpeg2_feed(&s_rec.bytes[OFF_TABLE], FLATFIELD_TABLE_BYTES);
    if (crc32_mpeg2_end() != rd_u32(OFF_CRC)) {
        return -2;
    }

    const mlx90640_id_t *id = mlx90640_id();
    for (unsigned i = 0; i < 3u; i++) {
        if (rd_u16((uint16_t)(OFF_DEVICE_ID + 2u * i)) != id->word[i]) {
            return -3;
        }
    }
    return 0;
}

int flatfield_load(void)
{
    s_applied = false;

    if (!m24c64_present()) {
        s_state = "absent: no store";
        return -1;
    }
    /* One blocking read, at boot or after a refused offer. Neither is on the
     * measurement path. */
    for (uint16_t off = 0; off < FLATFIELD_RECORD_BYTES; off += M24C64_PAGE) {
        uint16_t n = (uint16_t)(FLATFIELD_RECORD_BYTES - off);
        if (n > M24C64_PAGE) {
            n = M24C64_PAGE;
        }
        if (m24c64_read((uint16_t)(FLATFIELD_U2_OFFSET + off), &s_rec.bytes[off], n) != 0) {
            s_state = "absent: store unreadable";
            return -2;
        }
    }

    switch (validate()) {
    case 0:
        s_applied = true;
        s_state = "applied";
        return 0;
    case -1:
        s_state = "absent: no record";
        return -3;
    case -2:
        s_state = "corrupt: CRC";
        return -4;
    default:
        s_state = "refused: another imager";
        return -5;
    }
}

bool flatfield_applied(void)
{
    return s_applied;
}

const char *flatfield_state_str(void)
{
    return s_state;
}

float flatfield_correction(uint16_t index)
{
    if (!s_applied || (index >= MLX90640_PIXELS)) {
        return 0.0f;
    }
    const int16_t *table = (const int16_t *)(const void *)&s_rec.bytes[OFF_TABLE];
    return (float)table[index] * 0.01f;
}

int flatfield_stage(uint32_t offset, const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (offset + len) > FLATFIELD_RECORD_BYTES) {
        return -1;
    }
    if (len == 0u) {
        return 0; /* the empty write that ends a transfer */
    }
    /* From the first block the applied field is gone: the buffer it lived in is
     * the buffer being filled. Reported, never silent. */
    s_applied = false;
    s_state = "offered: transfer in progress";
    memcpy(&s_rec.bytes[offset], data, len);
    return 0;
}

int flatfield_commit_begin(void)
{
    const int rc = validate();
    if (rc != 0) {
        s_state = (rc == -1) ? "refused: not a flat-field record"
                             : ((rc == -2) ? "refused: CRC" : "refused: another imager");
        /* Nothing has been written, so U2 still holds whatever it held; the
         * previous field comes back rather than being lost to a bad offer. */
        const char *why = s_state;
        (void)flatfield_load();
        s_state = why;
        return rc;
    }
    s_commit_active = true;
    s_commit_wait = false;
    s_commit_off = 0u;
    s_state = "offered: writing";
    return 0;
}

bool flatfield_commit_active(void)
{
    return s_commit_active;
}

int flatfield_commit_step(void)
{
    if (!s_commit_active) {
        return 1;
    }
    if (s_commit_wait) {
        if (!m24c64_ready()) {
            return 0; /* the device is still in its 5 ms write cycle */
        }
        s_commit_wait = false;
    }
    if (s_commit_off >= FLATFIELD_RECORD_BYTES) {
        s_commit_active = false;
        s_applied = true;
        s_state = "applied";
        return 1;
    }

    const uint16_t addr = (uint16_t)(FLATFIELD_U2_OFFSET + s_commit_off);
    uint16_t n = (uint16_t)(M24C64_PAGE - (addr % M24C64_PAGE));
    const uint16_t left = (uint16_t)(FLATFIELD_RECORD_BYTES - s_commit_off);
    if (n > left) {
        n = left;
    }
    if (m24c64_write_page(addr, &s_rec.bytes[s_commit_off], n) != 0) {
        s_commit_active = false;
        s_state = "refused: store write failed";
        const char *why = s_state;
        (void)flatfield_load();
        s_state = why;
        return -1;
    }
    s_commit_off = (uint16_t)(s_commit_off + n);
    s_commit_wait = true;
    return 0;
}
