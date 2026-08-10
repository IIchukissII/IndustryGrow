/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensirion.h"

uint8_t sensirion_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

int sensirion_unpack(const uint8_t *buf, uint16_t *words, size_t count)
{
    /* Check every word before writing any of them: a caller that gets -1 must
     * not find half its output updated. */
    for (size_t i = 0; i < count; i++) {
        const uint8_t *w = &buf[i * 3u];
        if (sensirion_crc8(w, 2u) != w[2]) {
            return -1;
        }
    }
    for (size_t i = 0; i < count; i++) {
        const uint8_t *w = &buf[i * 3u];
        words[i] = (uint16_t)(((uint16_t)w[0] << 8) | w[1]);
    }
    return 0;
}
