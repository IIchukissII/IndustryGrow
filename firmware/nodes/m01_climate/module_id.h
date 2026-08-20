/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * M01-CLIMATE module identity. The carrier (E0001) is the parent and reads the
 * strap pins; the node (this child) owns the pattern it is expected to assert.
 * Keep per-node identity here, never in the shared carrier unit (common/carrier).
 */
#ifndef IGROW_M01_MODULE_ID_H
#define IGROW_M01_MODULE_ID_H

/* Module class ID for M01-CLIMATE (ADR-0014 rev 4 d6: 0x01).
 *
 * The identifier is 8-bit; its TRANSPORT depends on the carrier revision. On
 * E0001-000002 and earlier that transport is the 3-bit strap field, which
 * carries 0x01..0x07 and no more -- enough for every sensor class defined
 * today, and the reason M01 can keep identifying by strap while actuator
 * classes (0x80 and up) cannot. E0001-000100 moves identity to a module EEPROM;
 * a firmware image is built for one carrier revision (ADR-0017 d16), so this
 * image reads straps and makes no attempt to fall back between transports.
 *
 * The strap bit index equals the strap signal index: STRAP_0 is bit 0, so 0x01
 * is STRAP_0 high with the other two low (M01 spec 5). A transposed pattern is
 * electrically valid and yields a node that identifies as another class;
 * neither ERC nor a 1:1 footprint printout detects it, which is what makes the
 * boot self-check worth having.
 */
#define M01_MODULE_ID 0x01u

/* M01's bit 1 is 0, so the pattern also compares correctly on a carrier older
 * than E0001-000003 whose STRAP_1 (PA6) never reaches the MCU. That match is
 * not evidence that bit 1 was read. See firmware/common/carrier/e0001.h. */

#endif /* IGROW_M01_MODULE_ID_H */
