/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * M02-LIGHT module identity. The carrier (E0001) is the parent and reads the
 * strap pins; the node (this child) owns the pattern it is expected to assert.
 * Keep per-node identity here, never in the shared carrier unit (common/carrier).
 */
#ifndef IGROW_M02_MODULE_ID_H
#define IGROW_M02_MODULE_ID_H

/* Module class ID for M02-LIGHT (ADR-0014 rev 6 d6: 0x02; M02 spec 2).
 *
 * The identifier is 8-bit; its TRANSPORT on this carrier revision is the 3-bit
 * strap field, which carries 0x01..0x07. The strap bit index equals the strap
 * signal index, so 0x02 is STRAP_1 high with the other two low.
 *
 * M02 IS THE FIRST CLASS WHOSE BIT 1 IS SET, and bit 1 (STRAP_1, PA6) reaches
 * the MCU only from carrier revision E0001-000003. On E0001-000001 and -000002
 * it arrives only with the J6 pad 4 -> J3 pad 15 link added by hand; without it
 * an M02 board reads back as 0b000 -- the reserved value, which resolves to no
 * personality at all rather than to another class (M02 spec 5,
 * firmware/common/carrier/e0001.h).
 *
 * That failure is silent in the sense that nothing on the module is wrong: the
 * node comes up, publishes nothing, and reports itself unidentified. The
 * carrier is what to look at.
 */
#define M02_MODULE_ID 0x02u

#endif /* IGROW_M02_MODULE_ID_H */
