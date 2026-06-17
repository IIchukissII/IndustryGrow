/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_CYPHAL_REGISTERS_H
#define IGROW_CYPHAL_REGISTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* uavcan.register data model behind the Access/List services (ADR-0005 d5/d7).
 *
 * NOTE on names: the DSDL namespace `uavcan.register` collides with the C
 * keyword `register`, so Nunavut stropes it — generated headers live under
 * uavcan/_register/ and types are uavcan_register_*. Verify these identifiers
 * against your generated output (a likely first compile fixup).
 *
 * This is a small RAM-backed store supporting two value flavours: natural16
 * (node-id, subject-id port assignments) and string (descriptions). Flash
 * persistence is deferred — registers reset to defaults on power-up for now. */

#include "uavcan/_register/Value_1_0.h"
#include "uavcan/_register/Name_1_0.h"

/* Seed the table (node.id, node.description, and any port-id defaults). */
void registers_init(uint8_t node_id);

/* Number of registers, for List index bounds. */
size_t registers_count(void);

/* Name of the register at `index`; out_name.name.count = 0 if out of range. */
void registers_name_at(size_t index, uavcan_register_Name_1_0 *out_name);

/* Access: if `in` is a non-empty value and the named register is mutable and
 * type-compatible, write it; then return the current value and flags. An
 * unknown name yields an empty value with mutable=false, persistent=false. */
void registers_access(const uavcan_register_Name_1_0 *name,
                      const uavcan_register_Value_1_0 *in,
                      uavcan_register_Value_1_0 *out_value,
                      bool *out_mutable,
                      bool *out_persistent);

#endif /* IGROW_CYPHAL_REGISTERS_H */
