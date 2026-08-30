<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0029: Node firmware update and bootloader

- **ID:** ADR-0029
- **Status:** Accepted
- **Date:** 2026-08-28
- **Project:** IndustryGrow
- **Parent:** ADR-0004 (rev 1)
- **Companions:** ADR-0002 (rev 3), ADR-0005, ADR-0007 (rev 1), ADR-0027, ADR-0028
- **Realizes:** ADR-0005's deferred *"firmware-update / OTA service types"*; ADR-0028's deferred
  *"interaction with the firmware update path"*

## Revision history

- **Amendments** — decision 12 (2026-08-29): the condition that confirms a trial slot, which decision 8 left to the deferred list; bounds decision 8 and answers its deferred item.
- **Amendments** — decisions 13–16 (2026-08-29): the artifact a node is served, where the gateway gets it, how a running version becomes known, and what starts a transfer; answers the deferred *gateway artifact storage layout and the push-versus-pull trigger*.
- **Correction** — decision 15 (2026-08-29): as first written it had the gateway report the observed version to the ERP for recording, which ADR-0022 decision 9 excludes by category — operational intake stays platform-side. The observation stays at the gateway; nothing else in decisions 13–16 changes.
- **Amendment** — decision 17 (2026-08-30): how the gateway picks which of decision 13's two slot artifacts to serve. Building decision 16's transfer exposed that decision 3 has the node choose the target slot while the gateway chooses the file, with nothing on the bus naming the running slot; decision 17 resolves it from the image CRC decision 15 already reads, and fixes what happens when the running image is unrecognised. Bounds decisions 13 and 16; changes neither.

## Context and problem

Fixed elsewhere:

| Source | Fixed |
|---|---|
| ADR-0004 rev 1 d12–16 | Build-time signing by an offline key; distribution via the Cyphal file transfer service; per-node verification before flashing; bootloader replacement over SWD only; audit events to IndustryFlow |
| ADR-0004 rev 1 d17 | The in-cabinet CAN domain is trusted against injection |
| ADR-0027 d4 | An update must not erase the Node-ID sector |
| ADR-0005 | The update DSDL service binding is deferred to this ADR |
| ADR-0028 | The interaction between an update and commissioning is deferred to this ADR |

Not fixed:

- flash partitioning and bootloader location;
- behaviour of an update that fails part-way;
- which component downloads and writes;
- whether an updated node is re-provisioned or re-calibrated;
- which DSDL types carry the transfer.

Four properties bound the answer.

| # | Property | Source |
|---|---|---|
| P1 | Single 1 MB flash bank, no read-while-write. Code cannot execute from flash while flash is erased or written | RM0090 |
| P2 | Erase granularity is the sector: 16 KB ×4 (S0–S3), 64 KB ×1 (S4), 128 KB ×7 (S5–S11) | RM0090 |
| P3 | Sector 11 (`0x080E0000`, 128 KB) holds the Node-ID store and must survive an update | ADR-0027 d2, d4 |
| P4 | 500 kbit/s classic CAN, 8-byte frames; a node is addressable only while a Cyphal stack runs on it | ADR-0002 d8 |

P1 excludes in-place rewrite of the running image. P2 fixes partition boundaries to sector edges.
P3 excludes sector 11 from every write path. P4 requires the downloading component to be a Cyphal
node.

## Decision drivers

- Release image: ~21 KB. Flash: 1 MB.
- Project cryptography: ECDSA on NIST P-256 with SHA-256 (ADR-0007 d1–2, ADR-0025 d9).
- Firmware distribution is a gateway-paced burst below telemetry priority (ADR-0002 rev 3).
- Watchdog window: ~1.4 s at worst-case LSI (`firmware/common/platform/watchdog.c`).
- SWD recovery requires physical access to the node.

## Decision

1. **Flash is partitioned at sector boundaries.**

    | Region | Sectors | Address | Size | Written by |
    |---|---|---|---|---|
    | Bootloader | 0–3 | `0x08000000` | 64 KB | SWD only |
    | Update state | 4 | `0x08010000` | 64 KB | bootloader, application |
    | Slot A | 5–7 | `0x08020000` | 384 KB | bootloader |
    | Slot B | 8–10 | `0x08080000` | 384 KB | bootloader |
    | Identity | 11 | `0x080E0000` | 128 KB | application (ADR-0027 d5) |

    The application links to a slot base and sets `VTOR`. Sector 11 is outside every update write
    path (P3).

    ![First flash over SWD, then updates over CAN: command, restart, download, verify, trial, confirm or revert](./figures/adr0029-flash-and-update.svg)

2. **Two application slots, with rollback.** The bootloader selects the slot from the update state
   block. A slot is written only while the other holds the running image (P1).

3. **The bootloader downloads and writes; the application writes no slot.** The application records
   an update request in the update state block and restarts.

4. **The bootloader is a Cyphal node** (P4): publishes `uavcan.node.Heartbeat`, answers
   `uavcan.node.GetInfo`, and is a `uavcan.file.Read` client. It joins under the Node-ID in the
   ADR-0027 store, or `127` when unprovisioned (ADR-0027 d6).

5. **DSDL binding: `uavcan.node.ExecuteCommand` `COMMAND_BEGIN_SOFTWARE_UPDATE`, and
   `uavcan.file.Read`**, consumed from the pinned regulated set (ADR-0005 d2, d10). The command
   carries the artifact path.

6. **An image is verified by ECDSA P-256 signature over SHA-256 of header and body before its slot
   is marked bootable.** The verification public key is held in the bootloader (ADR-0004 d14). A
   failed image is discarded, the slot left unmarked, and the previous slot continues to run.

7. **The image header carries:** magic, header version, image length, target hardware class, image
   version, SHA-256 digest, detached signature. Byte layout is an implementation specification
   (ADR-0000 d2). A header whose target class does not match the node is refused.

8. **A newly written slot boots in trial state and must be confirmed.** The application confirms the
   running slot on reaching a defined healthy condition. An unconfirmed slot is reverted at the next
   boot. A bounded attempt counter in the update state block terminates a boot loop. Each boot
   checks a CRC-32 over the selected slot; the signature is checked on download only.

9. **An update is not a commissioning event.** Node-ID survives by d1 and P3; a calibration trim is
   held in the device it corrects, not in MCU flash (ADR-0028 d2). An update therefore does not void
   a `-CC`. ADR-0028 d1 step 1 remains the manufacture-time SWD flash of bootloader and first image.

10. **The bootloader is not an update target over the bus.** Its sectors lie outside both slots;
    replacement requires SWD (ADR-0004 d15). The debug header is reachable with the carrier mounted
    (`store/E0001-000003-D-pinmap.md`).

11. **The node reports the verification result and resulting image version on its diagnostic
    channel** (ADR-0004 d16).

12. **A trial slot is confirmed by the application reaching its main loop** (added 2026-08-29).
    The condition is a provisioned Node-ID, a personality resolved from the strap, and the Cyphal
    node up: the point at which bring-up has completed and the loop begins. The application
    asserts it and writes the update state once. An image that faults, hangs in bring-up or boot
    loops never reaches it and is reverted by d8's attempt counter. This bounds d8 and nothing
    else; the gateway takes no part, so a node updated on a bus with no gateway keeps its image.

13. **A release publishes the bus artifact beside the flashable one** (added 2026-08-29). Each
    application slot ships as `-F-slot-x.hex` and `-F-slot-x.img`. The `.img` is header plus body:
    the unit a signature covers and the only form a node can be given over the bus. The `.hex`
    carries its own load address, so flashing cannot put an image at the wrong one. The bootloader
    ships flashable only — it is never served over the bus (d10).

14. **The gateway obtains artifacts from the ERP and holds them on disk** (added 2026-08-29), as it
    obtains the active cultivation profile (ADR-0021 d8). ADR-0004 d13 fixes that the gateway holds
    and serves artifacts; this fixes where they come from. The ERP is the system of record and
    already indexes store objects by key (ADR-0021 d7), so no third party holds firmware.

15. **What a node runs is observed at the gateway, never assumed** (added 2026-08-29, corrected
    the same day). The gateway reads `uavcan.node.GetInfo` and compares the node's
    `software_image_crc` against the artifact it holds — an exact question, not a version number
    two sides could increment differently. A version is not inferred from the last command sent: a
    reverted trial, an SWD flash and a failed download all leave a node running something other
    than what was last asked of it.

    **The observation is operational and does not enter the ERP** (ADR-0022 d9, which excludes
    telemetry, operational and firmware-flash intake by category; ADR-0022 d8 rev 1, under which a
    machine records nothing at all). Pre-cloud it is answerable at the gateway; it reaches a system
    of record at stage 11 with the rest of the operational stream (ADR-0020 d9). What the ERP holds
    is the operator's *intent* — d16 — exactly as it holds the active profile version and not
    whether the gateway pulled it.

16. **A transfer starts from an operator action recorded in the ERP; the gateway performs it**
    (added 2026-08-29). The ERP holds the intended version per machine, as it holds the active
    profile; the gateway compares it against d15's observation, fetches the artifact, sends
    `COMMAND_BEGIN_SOFTWARE_UPDATE` and serves the file. Pull, by the component that owns the bus,
    on a record the operator changed — not a push from a system that cannot see the node.

17. **The gateway identifies the running image by its CRC, and serves the artifact for the other
    slot; an image it cannot identify stops the update** (added 2026-08-30). Decision 13 publishes
    one artifact per slot because the application is not position-independent, and decision 3 has
    the *node* choose the target — the slot that is not running. The gateway chooses the *file*, so
    the two choices have to agree, and nothing a node exposes names its slot: `GetInfo` reports the
    image, not where it sits.

    The image is enough. Each `.img` header carries `body_crc32` over its own body, the two slot
    builds of a release differ, and d15 already has the gateway read `software_image_crc`. Matching
    that value against the headers of the artifacts it holds names the running release *and* its
    slot in one comparison — the same exact question d15 asks, which the slot falls out of rather
    than a second mechanism. A match against the intended release means there is nothing to do; a
    match against another release gives the slot, and the artifact served is the intended release's
    other one.

    **No match is a refusal, not a guess.** A node running an image the gateway has never held —
    bench-flashed, or from a release since removed — leaves the running slot unknown, and the two
    ways to proceed are both worse than stopping: assuming slot A is wrong for every node that has
    updated an odd number of times, and trying one slot then the other spends a full transfer and a
    deliberate failed trial boot to learn one bit. Both write a wrong-slot image, which verifies
    (d6 covers the artifact, not where it is written), boots to a vector table that is not there,
    and is reverted by d8. The bootloader survives it; the operator gets a node that failed for a
    reason nothing reported. So the gateway declines and says which node and which CRC, and an
    operator resolves it by SWD flash or by making the running release available. Fail-closed, as
    the profile path is when it cannot verify (ADR-0015 d7).

## Alternatives considered

**A. One slot, updated in place.** *Rejected:* P1 — an interrupted write leaves no bootable image.

**B. Application self-update executing from RAM.** *Rejected:* unrecoverable on power loss mid-erase.

**C. STM32 ROM system bootloader over CAN.** *Rejected:* AN2606 binds it to CAN2 (PB5/PB13); the
carrier wires CAN1 (PB8/PB9) and PB13 is `SPI2_SCK`; it cannot verify a signature (ADR-0004 d14).

**D. USB DFU per node.** *Rejected:* physical access per node. Retained as a recovery path.

**E. Unsigned images under ADR-0004 d17.** *Rejected:* d17 covers wire injection, not a corrupted or
substituted artifact.

**F. Update state in the identity sector.** *Rejected:* P3.

## Consequences

### Positive

- A failed update leaves the previous image bootable; no SWD access required.
- Node-ID and trims survive by construction.
- Flash writing and signature verification exist in one signed component.
- A wrong-class image is refused before execution.

### Negative

- The application relocates off `0x08000000` and sets `VTOR`: linker script, release artifact and
  SWD flashing procedure all change.
- A second signed component enters the build and the key ceremony.
- A bootloader defect requires physical access to correct.
- The Cyphal skeleton is linked into both images. It is the same code, not a second implementation, so the cost is 22 KB of the bootloader's 64 KB rather than a stack to keep in step.
- First flash is two artifacts.

## Deferred decisions

- **Image header byte layout and the build-time signing step.** An implementation specification.
- **Bootloader-signing key custody and rotation — discharged 2026-08-30 by ADR-0024 decision 14.**
- **Measured bus occupancy of a full image transfer**, by the bit accounting ADR-0002 rev 3 applies
  to telemetry.
- **Whether the bootloader services or disables the watchdog during a transfer.**
- **Fleet-wide update order and concurrency.**
- **How a node running an unrecognised image is brought back under decision 17** without an SWD
  visit — whether by publishing the running slot on the bus, or by the gateway retaining more
  release history. Decision 17 fixes the refusal, not the recovery.

## References

- ADR-0002 (rev 3): Field bus architecture — d8 fixes 500 kbit/s classic CAN; the CAN1 pin choice
  keeps USB DFU available.
- ADR-0004 (rev 1): Gateway host hardening — d12–16 update policy, d17 trusted-bus assumption.
- ADR-0005: DSDL foundation — d2 and d10 on the pinned regulated set; the deferred OTA service types.
- ADR-0007 (rev 1): PKI and secure element identity — d1–2 fix P-256; d8 separates the
  firmware-signing root from node identity.
- ADR-0027: Node identity model — d2, d4, d5, d6.
- ADR-0028: Commissioning sequence and trim custody — d1, d2, and the deferred item this ADR closes.
- `store/E0001-000003-D-pinmap.md`: CAN1 on PB8/PB9, `SPI2_SCK` on PB13, SWD on the WeAct debug
  header.
- ST RM0090: Table 5 *Flash module organization (STM32F40x and STM32F41x)*; section *Erase and
  program operations* — a flash read stalls while flash is written or erased.
- ST AN2606: Table 50 *STM32F40xxx/41xxx configuration in system memory boot mode* — system
  bootloader resources, CAN2 on PB5/PB13.
