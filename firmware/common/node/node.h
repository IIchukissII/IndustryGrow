/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_NODE_NODE_H
#define IGROW_NODE_NODE_H

#include <stdint.h>

/*
 * The node-personality seam.
 *
 * One codebase, one image, one carrier. The sensor-module personality is
 * SELECTED AT RUNTIME by the module-ID strap (ADR-0017 d16; ADR-0014 d6) --
 * it is a hardware attribute of the board that is plugged in, not a separate
 * build. That is why the firmware document layer `F` roots on the carrier
 * E0001 and not on the module: there is one versioned codebase, and filing it
 * per-module would bump every module's F version on any shared-code change.
 *
 * A personality owns exactly two things beyond its identity: what to bring up,
 * and what to do each pass of the main loop. Everything else -- clock, carrier
 * BSP, CAN, the Cyphal node skeleton, the watchdog -- is common and runs
 * identically whichever module is fitted.
 */
typedef struct {
    uint8_t module_id;  /* 8-bit class ID, ADR-0014 rev 4 d6 */
    uint8_t node_id;    /* static bring-up default; ADR-0027 makes it flash-provisioned
                         * per instance and removes it from this record */
    const char *name;        /* human, for the debug console */
    const char *cyphal_name; /* reverse-DNS uavcan.node.GetInfo name, <= 50 bytes */
    void (*init)(void); /* called once, before the watchdog starts */
    void (*spin)(void); /* called every pass of the main loop */
} node_personality_t;

/* The personality claiming `module_id`, or NULL if none does.
 *
 * NULL is a real and expected answer, not an error path: a strap value with no
 * personality means the carrier has no module fitted, an unbuilt module class,
 * or a mis-strapped board. ADR-0014 rev 4 d6 is explicit that an unreadable or
 * unknown ID is *unidentified* and never a guessed class, so the caller brings
 * up the standard node skeleton and publishes nothing. */
const node_personality_t *node_for_module_id(uint8_t module_id);

/* Node-ID used when no personality claims the strap. The node still enumerates
 * on the gateway -- an unidentified node that is visible can be diagnosed; one
 * that stays off the bus cannot. */
#define IGROW_NODE_ID_UNIDENTIFIED 127u

/* GetInfo name when no personality claims the strap. Deliberately its own name
 * rather than a default class: ADR-0014 rev 4 d6 forbids guessing a class from
 * an ID that does not resolve, and the gateway must be able to see the
 * difference between a module it does not know and a module that is missing. */
#define IGROW_NODE_NAME_UNIDENTIFIED "org.industrygrow.node.unidentified"

#endif /* IGROW_NODE_NODE_H */
