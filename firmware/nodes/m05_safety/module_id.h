/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * M05-SAFETY module identity. The carrier (E0001) is the parent and reads the
 * strap pins; the node (this child) owns the pattern it is expected to assert.
 * Keep per-node identity here, never in the shared carrier unit (common/carrier).
 */
#ifndef IGROW_M05_MODULE_ID_H
#define IGROW_M05_MODULE_ID_H

/* Module-ID strap pattern for M05-SAFETY (ADR-0014 d6: 0b101). */
#define M05_MODULE_ID 0x5u

/* On carrier rev E0001-000001, STRAP_1 (PA6 / bit 1) is unrouted and reads 0
 * (E0001_STRAP1_UNROUTED). M05's bit 1 is 0, so a direct compare passes — but
 * that is luck, not a true read of bit 1. See firmware/common/carrier/e0001.h. */

#endif /* IGROW_M05_MODULE_ID_H */
