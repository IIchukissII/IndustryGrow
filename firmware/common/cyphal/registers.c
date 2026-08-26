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
} reg_type_t;

typedef struct {
    const char *name;
    reg_type_t type;
    bool mutable_;
    bool persistent;
    uint16_t n16;       /* REG_NATURAL16 */
    char str[64];       /* REG_STRING (NUL-terminated) */
} reg_entry_t;

/* The table. Port-id registers for the sensor subjects (uavcan.pub.*.id) are
 * added alongside the sensor publications (next slice). The description is
 * filled at init from the strap-selected personality, not baked here -- one
 * image runs every module class (ADR-0017 d16). */
static reg_entry_t s_regs[] = {
    {"uavcan.node.id", REG_NATURAL16, true, true, 0u, {0}},
    {"uavcan.node.description", REG_STRING, true, false, 0u, "IndustryGrow node"},
};

/* uavcan.node.id is the provisioning interface of ADR-0027 d5: mutable and
 * persistent, backed by the carrier flash sector, effective at the next restart
 * rather than immediately. It is the only register with a store behind it, so
 * the write path is special-cased here by index rather than generalised into a
 * per-entry hook that nothing else would use. */
#define REG_IDX_NODE_ID 0u

#define REG_N (sizeof(s_regs) / sizeof(s_regs[0]))

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
