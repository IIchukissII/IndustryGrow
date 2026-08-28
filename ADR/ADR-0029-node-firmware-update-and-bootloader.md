<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0029: Node firmware update and bootloader

- **ID:** ADR-0029
- **Status:** Proposed
- **Date:** 2026-08-28
- **Project:** IndustryGrow
- **Parent:** ADR-0004 (rev 1)
- **Companions:** ADR-0002 (rev 3), ADR-0005, ADR-0007 (rev 1), ADR-0027, ADR-0028
- **Realizes:** ADR-0005's deferred *"firmware-update / OTA service types"*; ADR-0028's deferred
  *"interaction with the firmware update path"*; the mechanism ADR-0027 decision 4 constrains

## Context and problem

ADR-0004 rev 1 decisions 12–16 fix the *policy* of CAN-node firmware update: build-time signing by
an offline key, distribution via the Cyphal file transfer service, per-node signature verification
before flashing, bootloader replacement only over SWD, and audit events to IndustryFlow. ADR-0005
defers the DSDL service binding to this work. ADR-0028 defers the interaction between an update and
commissioning to this work. ADR-0027 decision 4 constrains any update path to leave the Node-ID
sector intact.

No document fixes the mechanism:

- how flash is partitioned, and where the bootloader lives;
- what happens to a node whose update fails part-way;
- which component performs the download and the write;
- whether an updated node must be re-provisioned or re-calibrated;
- which DSDL types carry the transfer.

Four device and bus properties bound the answer.

| # | Property | Source |
|---|---|---|
| P1 | The STM32F405 has a single 1 MB flash bank with no read-while-write. Code cannot execute from flash while flash is being erased or written | RM0090 |
| P2 | Erase granularity is the sector: 16 KB ×4 (S0–S3), 64 KB ×1 (S4), 128 KB ×7 (S5–S11) | RM0090 |
| P3 | Sector 11 (`0x080E0000`, 128 KB) holds the Node-ID store and must survive an update | ADR-0027 decisions 2, 4 |
| P4 | The bus is 500 kbit/s classic CAN with 8-byte frames; a node is addressable only while a Cyphal stack is running on it | ADR-0002 decision 8 |

P1 excludes any design in which the running application rewrites its own image in place. P2 fixes
the partition boundaries to sector edges. P3 excludes the identity sector from every write path. P4
means the component that performs the download must itself be a Cyphal node.

## Decision drivers

- ADR-0004 rev 1 decisions 12–16 already settle policy. This ADR binds mechanism and must not
  re-decide either the signing model or the distribution service.
- A failed update must not require a probe visit. Physical access to a node is the cost the update
  path exists to remove.
- The release image is ~21 KB against 1 MB of flash. Space is not scarce; robustness is affordable.
- Project cryptography is ECDSA on NIST P-256 with SHA-256 (ADR-0007 decisions 1–2, ADR-0025
  decision 9). A second scheme would be a second thing to audit.
- ADR-0002 rev 3 already classes firmware distribution as a gateway-paced burst below telemetry
  priority.
- The watchdog window is ~1.4 s at worst-case LSI (`firmware/common/cyphal/cyphal.h`).

## Decision

1. **Flash is partitioned at sector boundaries, fixed by this ADR.**

    | Region | Sectors | Address | Size | Written by |
    |---|---|---|---|---|
    | Bootloader | 0–3 | `0x08000000` | 64 KB | SWD only |
    | Update state | 4 | `0x08010000` | 64 KB | bootloader, application |
    | Slot A | 5–7 | `0x08020000` | 384 KB | bootloader |
    | Slot B | 8–10 | `0x08080000` | 384 KB | bootloader |
    | Identity | 11 | `0x080E0000` | 128 KB | application (ADR-0027 decision 5) |

    The application is linked to a slot base rather than to `0x08000000` and sets `VTOR`
    accordingly. The identity sector stays outside every update write path, which is how
    ADR-0027 decision 4 becomes a property of the partition rather than a rule an operator keeps.

2. **Two application slots, with rollback.** The bootloader selects the slot to start from the
   update state block. A slot is written only while the other one is the running image's source, so
   a failed or interrupted update always leaves a bootable image behind. P1 forbids updating the
   running slot in place; P2 makes the 384 KB slots free, since the image is ~21 KB.

3. **The bootloader performs the download and the write; the application never writes a slot.**
   The application receives the update command, records the request in the update state block, and
   restarts. This keeps the flash-write and verification path in one signed component rather than
   in every image.

4. **The bootloader is a Cyphal node.** Per P4 it publishes `uavcan.node.Heartbeat`, answers
   `uavcan.node.GetInfo`, and acts as a `uavcan.file.Read` client against the gateway. It joins the
   bus under the Node-ID held in the ADR-0027 store, or `127` when unprovisioned (ADR-0027
   decision 6), so an update is addressed to the same node before and after the restart.

5. **The DSDL binding is `uavcan.node.ExecuteCommand` with `COMMAND_BEGIN_SOFTWARE_UPDATE`, and
   `uavcan.file.Read`.** Both are consumed from the pinned regulated set, not minted (ADR-0005
   decisions 2 and 10). The command carries the artifact path; the gateway serves it. This realizes
   the binding ADR-0005 deferred.

6. **A downloaded image is verified by ECDSA P-256 signature over SHA-256 of its header and body,
   before the slot is marked bootable.** The verification public key is held in the bootloader
   (ADR-0004 decision 14). An image that fails verification is discarded and the slot is left
   unmarked; the previous slot continues to run. Signature verification is not made redundant by
   ADR-0004 decision 17's trusted-bus assumption: that assumption covers injection on the wire, not
   a corrupted or substituted artifact upstream of it.

7. **The image header carries: magic, header version, image length, target hardware class, image
   version, SHA-256 digest, and the detached signature.** The byte layout is an implementation
   specification, not this ADR (ADR-0000 decision 2). A header whose target class does not match
   the node's is refused, so an M05 image cannot be started on an M01 carrier.

8. **A freshly written slot boots once in trial state and must be confirmed.** The application
   confirms the running slot after it reaches a defined healthy condition; an unconfirmed slot is
   reverted by the bootloader at the next boot, and a bounded attempt counter in the update state
   block stops a boot loop. At every boot the bootloader checks a CRC-32 over the selected slot;
   the signature is checked on download, not on each boot.

9. **An update preserves identity and trims, and therefore does not re-enter commissioning.**
   The Node-ID survives by decision 1 (ADR-0027 decision 4). A calibration trim is held in the
   device it corrects, not in MCU flash (ADR-0028 decision 2), so an update cannot disturb one. An
   OTA update is consequently not a commissioning event and does not void a `-CC`. ADR-0028
   decision 1 step 1 remains the manufacture-time SWD flash of bootloader and first image.

10. **The bootloader is never an update target over the bus.** Its sectors are outside both slots,
    and replacing it requires SWD (ADR-0004 decision 15). The debug header stays reachable with the
    carrier mounted (`store/E0001-000003-D-pinmap.md`), so this costs access, not disassembly.

11. **The node reports the verification result and the resulting version on its diagnostic
    channel**, which is the node-side half of the audit record ADR-0004 decision 16 requires.

## Alternatives considered

**A. One slot, updated in place.** *Rejected:* P1 and P2. An interrupted write leaves no bootable
image, and recovery is a probe visit to every affected node — the cost this path exists to remove.

**B. Application self-update, executing the flash routine from RAM.** *Rejected:* P1 makes it
possible but fragile; a power cut mid-erase is unrecoverable, and the flash and verification path
would be duplicated into every image rather than held in one signed component.

**C. The STM32 ROM system bootloader over CAN.** *Rejected:* per AN2606 it binds to CAN2 on
PB5/PB13, while the carrier wires CAN1 on PB8/PB9 and PB13 is `SPI2_SCK`
(`store/E0001-000003-D-pinmap.md`). It is unreachable without a carrier change, needs BOOT0
asserted, and cannot verify a signature as ADR-0004 decision 14 requires.

**D. USB DFU per node.** *Rejected:* requires physical access to each node. Retained as a recovery
path, not an update path; ADR-0002's CAN1 pin choice already keeps the WeAct USB free for it.

**E. Unsigned images, relying on ADR-0004 decision 17.** *Rejected:* decision 17 documents that the
in-cabinet bus is trusted against injection. It says nothing about a corrupted artifact or a
compromised gateway, which is what decision 14 defends against.

**F. Hold update state in the identity sector.** *Rejected:* P3. Any write path reaching sector 11
reintroduces the erase risk decision 1 removes by construction.

## Consequences

### Positive

- A failed update is self-recovering; no probe visit, no disassembly.
- Node-ID and calibration trims survive an update by construction, closing ADR-0028's deferred item.
- One signed component owns flash writing and signature verification.
- A wrong-class image is refused before it runs.
- The identity sector's protection stops depending on operator discipline.

### Negative

- The application must be relocated off `0x08000000` and set `VTOR`; the linker script, the release
  artifact and the SWD flashing procedure all change.
- A second signed component enters the build and the key ceremony, with its own release discipline.
- Bootloader replacement remains a physical operation, so a bootloader defect is expensive.
- The bootloader duplicates a minimal CAN and Cyphal stack, which is code that must stay in step
  with the application's.
- First flash becomes two artifacts rather than one.

## Deferred decisions

- **Image header byte layout and the build-time signing step.** An implementation specification.
- **The bootloader-signing key's custody and rotation.** Belongs with the key ceremony (ADR-0024).
- **Artifact storage layout on the gateway and the push-versus-pull trigger.** ADR-0004 decision 13
  fixes that the gateway serves them; when a transfer starts is operational.
- **Measured bus occupancy of a full image transfer**, by the exact bit accounting ADR-0002 rev 3
  applies to telemetry, rather than an estimate.
- **Whether the bootloader services or disables the watchdog during a transfer**, against the
  ~1.4 s window.
- **Fleet-wide orchestration** — update order and concurrency across a cabinet.
- **The healthy condition that confirms a trial slot**, and which component asserts it.

## References

- ADR-0002 (rev 3): Field bus architecture — decision 8 fixes 500 kbit/s classic CAN; the CAN1
  pin choice keeps USB DFU available.
- ADR-0004 (rev 1): Gateway host hardening — decisions 12–16 own the update policy this ADR
  implements, and decision 17 the trusted-bus assumption.
- ADR-0005: DSDL foundation — decisions 2 and 10 on reusing the pinned regulated set; the deferred
  OTA service types this ADR binds.
- ADR-0007 (rev 1): PKI and secure element identity — decisions 1–2 fix P-256; decision 8 keeps the
  firmware-signing root distinct from node identity.
- ADR-0027: Node identity model — decisions 2, 4, 5 and 6 on the store, its survival and the
  unprovisioned Node-ID.
- ADR-0028: Commissioning sequence and trim custody — decision 1's sequence, decision 2's trim
  custody, and the deferred item this ADR closes.
- `store/E0001-000003-D-pinmap.md`: CAN1 on PB8/PB9, `SPI2_SCK` on PB13, SWD on the WeAct debug
  header.
- ST RM0090 (flash sectors, single bank) and AN2606 (system bootloader interfaces).
