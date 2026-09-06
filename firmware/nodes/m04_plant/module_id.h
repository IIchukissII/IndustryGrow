/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * M04-PLANT module identity. The carrier (E0001) is the parent and reads the
 * strap pins; the node (this child) owns the pattern it is expected to assert.
 * Keep per-node identity here, never in the shared carrier unit (common/carrier).
 */
#ifndef IGROW_M04_MODULE_ID_H
#define IGROW_M04_MODULE_ID_H

/* Module class ID for M04-PLANT (ADR-0014 rev 6 d6: 0x04; M04 spec 2).
 *
 * The identifier is 8-bit; its TRANSPORT on this carrier revision is the 3-bit
 * strap field, which carries 0x01..0x07. The strap bit index equals the strap
 * signal index, so 0x04 is STRAP_2 high with the other two low.
 *
 * Bit 2 is STRAP_2, which reaches the MCU on every carrier revision: only
 * STRAP_1 (PA6) was unrouted before E0001-000003, so an M04 board reads back
 * correctly on a carrier carrying that defect (M04 spec 5).
 */
#define M04_MODULE_ID 0x04u

#endif /* IGROW_M04_MODULE_ID_H */
