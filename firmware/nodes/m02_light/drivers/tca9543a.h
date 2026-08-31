/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_TCA9543A_H
#define IGROW_DRIVERS_TCA9543A_H

#include <stdbool.h>
#include <stdint.h>

/* TI TCA9543A two-channel I2C switch, the part that makes M02 possible: U3 and
 * U4 are both fixed at 0x39 and are separated onto its two channels (M02 spec
 * 5.2). It also performs the 3.3 V master to 1.8 V channel translation.
 *
 * Address 0x70, A0 and A1 to GND. The whole register model is one byte.
 *
 * The device powers up with BOTH CHANNELS DESELECTED, so 0x39 is unreachable
 * until this driver has run. A probe of 0x39 with no channel selected is a U1
 * fault, never two absent sensors (M02 spec 10, O-65).
 *
 * RESET is tied to V_CC on this board, so there is no recovery line: a channel
 * stuck low is recoverable only by a node power cycle (O-65). Nothing here
 * attempts a reset it has no pin for. */
#define TCA9543A_ADDR 0x70u

#define TCA9543A_CH_U4 0u /* channel 0 -- AS7343 */
#define TCA9543A_CH_U3 1u /* channel 1 -- TSL2585 */

/* Select exactly one channel. Never both: 0x39 answers on each, and two
 * devices at one address on one segment is the fault this topology exists to
 * prevent (M02 spec V11). Returns 0 on success. */
int tca9543a_select(uint8_t channel);

/* Deselect both channels, leaving 0x39 unreachable -- the power-up state. */
int tca9543a_deselect(void);

/* True if the switch ACKs and reads back the channel last selected. An ACK
 * alone is not identification (M02 spec 10), and this device has no ID
 * register; the read-back of a value only this part would hold is the closest
 * a one-register device gets to one. */
bool tca9543a_present(void);

#endif /* IGROW_DRIVERS_TCA9543A_H */
