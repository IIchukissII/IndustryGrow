/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "tca9543a.h"
#include "i2c.h"

/* Control register, D1:D0 = channel enables. D5:D4 are the read-only interrupt
 * flags; both INT inputs are tied to V_CC on this board (M02 spec 5.2), so they
 * read back as no interrupt and are masked out of the comparison below. */
#define CTRL_CHANNEL_MASK 0x03u

static uint8_t s_selected = 0x00u; /* the value we last wrote, power-up = none */

static int write_ctrl(uint8_t ctrl)
{
    if (i2c_write(TCA9543A_ADDR, &ctrl, 1u) < 0) {
        return -1;
    }
    s_selected = ctrl;
    return 0;
}

int tca9543a_select(uint8_t channel)
{
    if (channel > TCA9543A_CH_U3) {
        return -1;
    }
    const uint8_t ctrl = (uint8_t)(1u << channel);
    if (ctrl == s_selected) {
        return 0; /* already there; a redundant write costs a transaction */
    }
    return write_ctrl(ctrl);
}

int tca9543a_deselect(void)
{
    return write_ctrl(0x00u);
}

bool tca9543a_present(void)
{
    /* Write then read back, rather than compare against what we last wrote.
     * A device that lost its rail and came back would hold the power-up 0x00
     * while this driver still believed a channel was selected, and a
     * comparison against a cached value would call a perfectly good switch
     * absent from then on. Writing first makes the probe self-synchronising.
     *
     * The value written is 0x00, which is both the power-up state and what the
     * caller wants before it selects a channel of its own. */
    if (write_ctrl(0x00u) < 0) {
        s_selected = 0xFFu; /* unknown: force the next select to write */
        return false;
    }
    uint8_t ctrl = 0xFFu;
    if (i2c_read(TCA9543A_ADDR, &ctrl, 1u) < 0) {
        s_selected = 0xFFu;
        return false;
    }
    /* The switch is the only device at 0x70 and its control register holds
     * exactly what was last written to it. A part that ACKs but reads back
     * something else is not this one. */
    return (uint8_t)(ctrl & CTRL_CHANNEL_MASK) == 0x00u;
}
