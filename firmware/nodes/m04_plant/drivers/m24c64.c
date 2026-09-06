/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "m24c64.h"

#include "i2c.h"

#include <string.h>

bool m24c64_present(void)
{
    if (!i2c_probe(M24C64_ADDR)) {
        return false;
    }
    uint8_t b;
    return m24c64_read(M24C64_CLASS_ID_ADDR, &b, 1u) == 0;
}

int m24c64_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0u) || ((uint32_t)addr + len > M24C64_SIZE)) {
        return -1;
    }
    const uint8_t a[2] = {(uint8_t)(addr >> 8), (uint8_t)addr};
    return i2c_write_read(M24C64_ADDR, a, sizeof(a), buf, len);
}

int m24c64_write_page(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0u) || (len > M24C64_PAGE) ||
        ((uint32_t)addr + len > M24C64_SIZE)) {
        return -1;
    }
    /* A write that crosses a page boundary wraps to the start of the same page
     * inside the device and silently corrupts what is already there. */
    if (((addr % M24C64_PAGE) + len) > M24C64_PAGE) {
        return -2;
    }
    uint8_t frame[2u + M24C64_PAGE];
    frame[0] = (uint8_t)(addr >> 8);
    frame[1] = (uint8_t)addr;
    memcpy(&frame[2], buf, len);
    return i2c_write(M24C64_ADDR, frame, (size_t)len + 2u);
}

bool m24c64_ready(void)
{
    return i2c_probe(M24C64_ADDR);
}
