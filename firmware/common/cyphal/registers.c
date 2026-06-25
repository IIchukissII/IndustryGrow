/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "registers.h"
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
 * added alongside the sensor publications (next slice). */
static reg_entry_t s_regs[] = {
    {"uavcan.node.id", REG_NATURAL16, true, false, 0u, {0}},
    {"uavcan.node.description", REG_STRING, true, false, 0u, "IndustryGrow M05-SAFETY"},
};

#define REG_N (sizeof(s_regs) / sizeof(s_regs[0]))

void registers_init(uint8_t node_id)
{
    s_regs[0].n16 = node_id;
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
                r->n16 = in->natural16.value.elements[0];
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
