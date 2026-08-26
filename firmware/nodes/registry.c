/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * The module-ID -> personality table. One entry per built node type; the strap
 * read at boot selects among them (ADR-0017 d16).
 *
 * This is the only file that knows every personality, which is what keeps the
 * common boot path (common/node/main.c) free of node-specific code. Adding a
 * module class is a directory under nodes/ and one line here.
 *
 * A personality carries no Node-ID. One ID per module class collides as soon as
 * a class has two instances, which ADR-0006 already schedules; ADR-0027 d1 makes
 * the Node-ID an instance value provisioned into carrier flash
 * (common/carrier/identity.h) and leaves this table with what it is for --
 * which class means which sensors.
 */

#include "node.h"

#include <stddef.h>

#include "m05_safety/sensors.h"
#include "m05_safety/module_id.h"
#include "m01_climate/sensors.h"
#include "m01_climate/module_id.h"

static const node_personality_t s_personalities[] = {
    {
        .module_id = M05_MODULE_ID,
        .name = "M05-SAFETY (E0006)",
        .cyphal_name = "org.industrygrow.node.m05",
        .init = m05_sensors_init,
        .spin = m05_sensors_spin,
    },
    {
        .module_id = M01_MODULE_ID,
        .name = "M01-CLIMATE (E0002)",
        .cyphal_name = "org.industrygrow.node.m01",
        .init = m01_sensors_init,
        .spin = m01_sensors_spin,
    },
};

#define PERSONALITY_COUNT (sizeof(s_personalities) / sizeof(s_personalities[0]))

const node_personality_t *node_for_module_id(uint8_t module_id)
{
    /* 0x00 is reserved (unknown / unprogrammed) and 0xFF is not a valid class
     * (ADR-0014 rev 4 d6). Neither may match, whatever the table says. */
    if ((module_id == 0x00u) || (module_id == 0xFFu)) {
        return NULL;
    }
    for (size_t i = 0; i < PERSONALITY_COUNT; i++) {
        if (s_personalities[i].module_id == module_id) {
            return &s_personalities[i];
        }
    }
    return NULL;
}
