/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * A01-CLIMATE module identity. The carrier (E0001) is the parent and reads the
 * module's class ID; the node (this child) owns the value it is expected to
 * carry. Keep per-node identity here, never in the shared carrier unit.
 */
#ifndef IGROW_A01_MODULE_ID_H
#define IGROW_A01_MODULE_ID_H

/* Module class ID for A01-CLIMATE (ADR-0014 rev 4 d6, ADR-0031 rev 1 d2: 0x80),
 * the first actuator class. Byte 0 of the module EEPROM U10 at 0x50; no strap
 * reaches it, so only an E0001-000100 image can select this personality
 * (A01 spec 2, F1). */
#define A01_MODULE_ID 0x80u

#endif /* IGROW_A01_MODULE_ID_H */
