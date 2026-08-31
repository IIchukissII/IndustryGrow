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
 * (node-id, subject-id port assignments) and string (descriptions).
 *
 * One register is not RAM-backed: `uavcan.node.id` is mutable and PERSISTENT,
 * and a write commits to the carrier flash sector behind
 * common/carrier/identity.h (ADR-0027 d5). It takes effect at the next restart,
 * so between the write and that restart the register reports the committed
 * value while the transport still runs on the previous one. Every other
 * register resets to its default on power-up; subject-ID assignments still
 * have no store (ADR-0005 d7). */

#include "uavcan/_register/Value_1_0.h"
#include "uavcan/_register/Name_1_0.h"

/* Seed the table (node.id, node.description, and any port-id defaults). */
/* `description` seeds uavcan.node.description and is the strap-selected
 * personality's, not the image's (ADR-0017 d16). Must outlive the call. */
void registers_init(uint8_t node_id, const char *description);

/* Add a mutable real32-array register backed by the caller's storage, for a
 * DEPLOYMENT CONSTANT a node cannot carry compiled in. M02's PPFD
 * reconstruction coefficients are the first: they belong to one luminaire
 * spectrum and one optical stack, are identified at commissioning against a
 * reference instrument, and are carried in the deployment profile (M02 spec
 * 6.2, 10). A firmware constant would be a different instrument's.
 *
 * `name` and `values` must outlive the call; `values` is read on every Access
 * and written in place by a write, so the personality sees changes without
 * being told. `count` is the exact array length, capped at
 * REGISTERS_REAL32_MAX; a write of a different length is rejected rather than
 * partially applied -- a coefficient set is a set, and half of one describes
 * nothing. Returns false if the table is full.
 *
 * Not persistent. Like every register but uavcan.node.id it resets to its
 * compiled default at power-up, because there is no store behind it
 * (ADR-0005 d7). */
#define REGISTERS_REAL32_MAX 16u
bool registers_add_real32(const char *name, float *values, uint8_t count);

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
