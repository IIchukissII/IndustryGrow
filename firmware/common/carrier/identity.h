/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_CARRIER_IDENTITY_H
#define IGROW_CARRIER_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The node's instance identity: its Cyphal Node-ID (ADR-0027).
 *
 * A Node-ID says WHICH INSTANCE is on this bus. It is not derived from the
 * module class -- that answers what kind of module is fitted, is fixed at
 * module manufacture, and cannot distinguish two instances of one class
 * (ADR-0027 d1). The class ID still selects the personality; it no longer
 * selects the identity.
 *
 * The store is a dedicated sector of the MCU's internal flash on the CARRIER
 * (d2, d3). Two consequences follow from where it sits: every carrier revision
 * in service has an STM32F405, so E0001-000002 boards are fixable in firmware
 * alone; and identity follows the carrier, so swapping a sensor module leaves
 * the node's number and the gateway's mapping intact.
 *
 * On E0001-000100 the same record lives in the carrier EEPROM U4 instead (d11,
 * E0001 spec F8), on the header I2C at 0x57. The flash sector stays reserved
 * there but is not read: an image built for 000100 has one store, never two.
 *
 * Provisioning is a write to the `uavcan.node.id` register, effective at the
 * next restart (d5). An unprovisioned node runs as IGROW_NODE_ID_UNPROVISIONED
 * and publishes no telemetry subjects (d6).
 */

/* The reserved Node-ID of a node with no provisioned identity. It means that
 * and only that (ADR-0027 d10): a node whose module class resolves to no
 * personality but which HAS been provisioned keeps its own Node-ID and reports
 * the unresolved personality through GetInfo and its health instead. */
#define IGROW_NODE_ID_UNPROVISIONED 127u

/* Provisionable range. 127 is not an identity, so writing it clears the store
 * rather than being stored -- a record holding 127 would contradict d10. */
#define IGROW_NODE_ID_MAX_PROVISIONABLE 126u

/* --- Where the record lives -------------------------------------------------
 *
 * Downstream values, left to the firmware by ADR-0027 d2. The last flash sector
 * is chosen so that the application region needs no ceiling: a programmer that
 * writes only the sectors the image covers never reaches it, which is what d4
 * requires of the update path. A MASS ERASE does reach it and de-provisions the
 * node -- see firmware/README.md.
 *
 * The linker script reserves the same range; identity_init() checks the two
 * against each other rather than trusting them to stay in step. */
#define IGROW_IDENTITY_FLASH_ADDR   0x080E0000u /* STM32F405RG sector 11 */
#define IGROW_IDENTITY_FLASH_SECTOR 11u
#define IGROW_IDENTITY_FLASH_SIZE   (128u * 1024u)

/* E0001-000100: U4, an M24C64 with E0-E2 to 3V3 (E0001 spec I9). The record
 * sits at word address 0 and fits one 32-byte page, so a commit is one page
 * write. */
#define IGROW_IDENTITY_EEPROM_ADDR  0x57u
#define IGROW_IDENTITY_EEPROM_WORD  0u

/* Read and validate the record. Call once at boot, after clock_init(), and on
 * E0001-000100 after i2c_init(). */
void identity_init(void);

/* The Node-ID this boot is running as: the provisioned value, else
 * IGROW_NODE_ID_UNPROVISIONED. Fixed for the lifetime of the boot. */
uint8_t identity_node_id(void);

/* Whether a valid record was found, i.e. whether identity_node_id() is an
 * identity or the unprovisioned placeholder. */
bool identity_provisioned(void);

/* What the store holds NOW, which differs from identity_node_id() between a
 * commit and the restart that adopts it. IGROW_NODE_ID_UNPROVISIONED if empty. */
uint8_t identity_committed_node_id(void);

/* True while a committed value differs from the running one (ADR-0027 d5). */
bool identity_commit_pending(void);

/* Write `node_id` to the store; IGROW_NODE_ID_UNPROVISIONED clears it. Returns
 * 0 on success, -1 on an out-of-range value or a flash failure. Takes effect at
 * the next restart, never on the live transport: changing a Node-ID under a
 * running transport invalidates in-flight transfer state.
 *
 * Blocks for the sector erase -- of the order of a second, with interrupts
 * masked (common/platform/flash.h); on E0001-000100 for one EEPROM page
 * write, at most 10 ms. Only ever called from provisioning. */
int identity_commit(uint8_t node_id);

/* Human-readable state for the bring-up console. */
const char *identity_state_str(void);

#endif /* IGROW_CARRIER_IDENTITY_H */
