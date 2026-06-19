/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * ATECC608 secure element on the E0001 carrier (I2C2, PB10 = SCL / PB11 = SDA) —
 * the node's hardware-identity ANCHOR per ADR-0007. This seam reads the device's
 * public 9-byte serial number and surfaces it as the Cyphal node unique-id. It
 * does NO crypto and NO provisioning: on-chip key generation, certificate
 * issuance, the serial<->certificate (-PR) binding and the ATECC slot map are
 * Production / Phase-2 concerns, deferred to a manufacturing document
 * (ADR-0007 decisions 6 and 9). Per ADR-0007 decision 5 the node secure element
 * is an identity/provenance anchor, NOT a CAN-bus credential — this code never
 * authenticates bus traffic, preserving the trusted-CAN boundary (ADR-0004 d17).
 *
 * DATASHEET-AUTHORED, NOT BENCH-VERIFIED: no carrier PCB / ATECC608 exists on the
 * current bench (bare WeAct F405), so this is written against the Microchip
 * ATECC608 datasheet and must degrade gracefully when the device is absent —
 * atecc608_present() then returns false and the node falls back to the STM32
 * factory UID, leaving the verified layer-1 bring-up unaffected.
 */
#ifndef IGROW_CARRIER_ATECC608_H
#define IGROW_CARRIER_ATECC608_H

#include <stdbool.h>
#include <stdint.h>

#define ATECC608_SERIAL_LEN 9u

/* Bring up I2C2 and probe the secure element once, caching the serial. Safe when
 * no device is present (records "absent"). Call after clock_init(). */
void atecc608_init(void);

/* True if the secure element answered at init and returned a CRC-valid serial. */
bool atecc608_present(void);

/* The cached 9-byte serial number (ATECC608_SERIAL_LEN); all-zero when !present. */
const uint8_t *atecc608_serial(void);

#endif /* IGROW_CARRIER_ATECC608_H */
