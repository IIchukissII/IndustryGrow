/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "registers.h"
#include "identity.h"
#include <string.h>

/* Supported value flavours in this minimal store. */
typedef enum {
    REG_NATURAL16,
    REG_STRING,
    REG_REAL32,
} reg_type_t;

typedef struct {
    const char *name;
    reg_type_t type;
    bool mutable_;
    bool persistent;
    uint16_t n16;       /* REG_NATURAL16 */
    char str[64];       /* REG_STRING (NUL-terminated) */
    float *f32;         /* REG_REAL32 -- the personality's storage, borrowed */
    uint8_t f32_count;
} reg_entry_t;

/* The table. Port-id registers for the sensor subjects (uavcan.pub.*.id) are
 * added alongside the sensor publications (next slice). The description is
 * filled at init from the strap-selected personality, not baked here -- one
 * image runs every module class (ADR-0017 d16).
 *
 * Sized for the two fixed entries plus what a personality adds at bring-up.
 * The seam is deliberately one-way: a personality may ADD a register, and
 * nothing removes one, so the index a List walk returns stays stable for the
 * life of the boot. */
#define REG_CAP 8u
static reg_entry_t s_regs[REG_CAP] = {
    {"uavcan.node.id", REG_NATURAL16, true, true, 0u, {0}, NULL, 0u},
    {"uavcan.node.description", REG_STRING, true, false, 0u, "IndustryGrow node", NULL, 0u},
};
static size_t s_reg_n = 2u;

/* uavcan.node.id is the provisioning interface of ADR-0027 d5: mutable and
 * persistent, backed by the carrier flash sector, effective at the next restart
 * rather than immediately. It is the only register with a store behind it, so
 * the write path is special-cased here by index rather than generalised into a
 * per-entry hook that nothing else would use. */
#define REG_IDX_NODE_ID 0u

#define REG_N (s_reg_n)

bool registers_add_real32(const char *name, float *values, uint8_t count)
{
    if ((s_reg_n >= REG_CAP) || (name == NULL) || (values == NULL) ||
        (count == 0u) || (count > REGISTERS_REAL32_MAX)) {
        return false;
    }
    reg_entry_t *r = &s_regs[s_reg_n];
    r->name = name;
    r->type = REG_REAL32;
    r->mutable_ = true;
    r->persistent = false;
    r->f32 = values;
    r->f32_count = count;
    s_reg_n++;
    return true;
}

void registers_init(uint8_t node_id, const char *description)
{
    /* The register reports the CONFIGURED value, which at boot is also the
     * running one -- the node is brought up with what the store held. */
    s_regs[REG_IDX_NODE_ID].n16 = node_id;
    if (description != NULL) {
        size_t len = strlen(description);
        if (len >= sizeof(s_regs[1].str)) {
            len = sizeof(s_regs[1].str) - 1u;
        }
        memcpy(s_regs[1].str, description, len);
        s_regs[1].str[len] = '\0';
    }
}

size_t registers_count(void)
{
    return REG_N;
}

static bool name_matches(const uavcan_register_Name_1_0 *n, const char *s)
{
    size_t len = strlen(s);
    return (n->name.count == len) && (memcmp(n->name.elements, s, len) == 0);
}

void registers_name_at(size_t index, uavcan_register_Name_1_0 *out_name)
{
    if (index >= REG_N) {
        out_name->name.count = 0u;
        return;
    }
    const char *s = s_regs[index].name;
    size_t len = strlen(s);
    memcpy(out_name->name.elements, s, len);
    out_name->name.count = len;
}

static void load_value(const reg_entry_t *r, uavcan_register_Value_1_0 *out)
{
    if (r->type == REG_NATURAL16) {
        uavcan_register_Value_1_0_select_natural16_(out);
        out->natural16.value.elements[0] = r->n16;
        out->natural16.value.count = 1u;
    } else if (r->type == REG_REAL32) {
        uavcan_register_Value_1_0_select_real32_(out);
        for (uint8_t i = 0; i < r->f32_count; i++) {
            out->real32.value.elements[i] = r->f32[i];
        }
        out->real32.value.count = r->f32_count;
    } else { /* REG_STRING */
        uavcan_register_Value_1_0_select_string_(out);
        size_t len = strlen(r->str);
        memcpy(out->_string.value.elements, r->str, len);
        out->_string.value.count = len;
    }
}

void registers_access(const uavcan_register_Name_1_0 *name,
                      const uavcan_register_Value_1_0 *in,
                      uavcan_register_Value_1_0 *out_value,
                      bool *out_mutable,
                      bool *out_persistent)
{
    for (size_t i = 0; i < REG_N; i++) {
        reg_entry_t *r = &s_regs[i];
        if (!name_matches(name, r->name)) {
            continue;
        }
        /* Write, if a compatible non-empty value was supplied and we're mutable. */
        if (r->mutable_ && in != NULL && !uavcan_register_Value_1_0_is_empty_(in)) {
            if (r->type == REG_NATURAL16 && uavcan_register_Value_1_0_is_natural16_(in) &&
                in->natural16.value.count >= 1u) {
                const uint16_t v = in->natural16.value.elements[0];
                if (i == REG_IDX_NODE_ID) {
                    /* Out of range, or a flash failure, leaves the register
                     * reading what the store actually holds -- which is how an
                     * operator sees the write rejected. 127 is not an identity
                     * (d10), so writing it clears the store rather than being
                     * stored as one. */
                    if (v <= IGROW_NODE_ID_UNPROVISIONED) {
                        (void)identity_commit((uint8_t)v);
                    }
                    r->n16 = identity_committed_node_id();
                } else {
                    r->n16 = v;
                }
            } else if (r->type == REG_REAL32 && uavcan_register_Value_1_0_is_real32_(in) &&
                       in->real32.value.count == r->f32_count) {
                /* All or nothing. A short write would leave a coefficient set
                 * half from the profile and half from the compiled default,
                 * which describes no instrument; the register simply reads
                 * back unchanged and the operator sees the write refused. */
                for (uint8_t i = 0; i < r->f32_count; i++) {
                    r->f32[i] = in->real32.value.elements[i];
                }
            } else if (r->type == REG_STRING && uavcan_register_Value_1_0_is_string_(in)) {
                size_t len = in->_string.value.count;
                if (len >= sizeof(r->str)) {
                    len = sizeof(r->str) - 1u;
                }
                memcpy(r->str, in->_string.value.elements, len);
                r->str[len] = '\0';
            }
        }
        load_value(r, out_value);
        *out_mutable = r->mutable_;
        *out_persistent = r->persistent;
        return;
    }
    /* Unknown register: empty value, immutable. */
    uavcan_register_Value_1_0_select_empty_(out_value);
    *out_mutable = false;
    *out_persistent = false;
}
