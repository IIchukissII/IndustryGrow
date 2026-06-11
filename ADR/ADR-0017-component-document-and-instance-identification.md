<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0017: Component, document, and instance identification scheme

- **ID:** ADR-0017
- **Status:** Proposed
- **Date:** 2026-05-31
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0004 (rev 1), ADR-0007 (planned), ADR-0014, ADR-0015, ADR-0016

## Context and problem

IndustryGrow produces physical artifacts and documentation across a lifecycle (design → production/QC → integration → operation) and across deployment scales (apartment cabinet to commercial greenhouse). The architecture already commits to a strong shape: a small set of reusable PCB designs (carrier, M01–M05 per ADR-0014), instantiated many times, with instances that **move between deployments** as inventory (ADR-0016). What it has not committed to is how those artifacts and their documents are **identified**.

The existing ADRs supply a taxonomy — module classes M01–M05, module-ID straps, the carrier as a distinct host board — but not material numbers, serial numbers, document identifiers, or a way to address per-instance lifecycle documents (test data, calibration, provisioning). Without a scheme, traceability is ad-hoc, procurement and QC records have no canonical key, and the inventory/mobility model of ADR-0016 has nowhere to hang an instance's history.

This ADR specifies a hierarchical identification scheme for IndustryGrow. Its organizing principle is the separation of three things the project needs kept apart: the **type** (a module designation plus a version), the **instance** (a serial number), and the **position in the machine** (a hierarchical depth code). The scheme uses phase-specific identifier formats so that an artifact's identifier reflects where it is in its lifecycle, and it reserves a suffix slot for per-instance lifecycle documents.

The separation is load-bearing for two reasons specific to IndustryGrow. ADR-0014 requires that one design be instantiated many times (designs few, instances many); ADR-0016 requires that an individual instance physically move between positions and deployments while keeping its identity and history. An identifier that fused type with position, or instance with position, would break both. This ADR also fixes the bindings that connect the scheme to the rest of the architecture: the instance serial to the ATECC608 hardware identity (ADR-0007), the document interface layer to Cyphal/DSDL (ADR-0002/0005), the integration-phase depth code to the gateway's runtime position tagging (ADR-0014 decision 7), and the suffix set to IndustryGrow's actual per-instance documents — together with the boundary between those documents and IndustryFlow's operational audit log (ADR-0004 rev 1).

## Decision drivers

- **Reuse the existing taxonomy; do not reinvent it.** M01–M05 and the module-ID straps (ADR-0014) are already a stable type vocabulary. The identifier scheme must encode that vocabulary, not run parallel to it.
- **Separate type, instance, and position.** Load-bearing because of ADR-0014 (one design, many instances at many positions) and ADR-0016 (an instance physically moves between positions and deployments over its life).
- **Reuse hardware identity already present.** Every carrier carries an ATECC608 (ADR-0002 rev 3 decision 3). Instance identity should bind to it rather than to a second, invented serial authority.
- **One scheme across disciplines.** Electrical, mechanical, and pneumatic/fluidic parts of IndustryGrow should share one identification scheme, not fork per discipline.
- **Do not duplicate the operational audit trail.** IndustryFlow already holds firmware events, telemetry, and forensic history (ADR-0004 rev 1). The document store must not become a second, divergent record of the same things.
- **Lifecycle documents belong to the instance, not the position.** A board's test, calibration, and provisioning history must follow its serial across re-installations.

## Decision

### Identifier formats

1. **Three phase-specific identifier formats.**
   - Documentation (design artifacts, type-level): `Exxxx-VVVVVV-L`
   - Production & QC (a manufactured instance): `Exxxx-VVVVVV-NNNNNN[-suffix]`
   - Integration (an instance installed in a machine): `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN`

   Decoded:

   ```
   Documentation  (type-level)
   Exxxx-VVVVVV-L
   │     │      │
   │     │      └─ document layer (S, D, L, P, M, I)
   │     └─ version, 6 digits (major.minor.patch)
   └─ module, E + 4 digits

   Production & QC  (one manufactured instance)
   Exxxx-VVVVVV-NNNNNN-XX
   │     │      │      │
   │     │      │      └─ lifecycle suffix (QP, QR, CP, CC, PR)
   │     │      └─ serial, 6 digits (per module + version)
   │     └─ version, 6 digits
   └─ module, E + 4 digits

   Integration  (instance installed in a machine)
   GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN
   │         │      │     │      │
   │         │      │     │      └─ serial (from production)
   │         │      │     └─ version, 6 digits
   │         │      └─ module, E + 4 digits
   │         └─ depth, 6 digits (position in machine)
   └─ machine, GBOX + 4 digits

   Two encoded fields carry sub-structure:
   VVVVVV  =  major.minor.patch   (2 digits each)        e.g. v2.1.3 = 020103
   DDDDDD  =  main.sub-L1.sub-L2  (2 digits each, main 01–99)
              e.g. climate node at position 1.1 = 010100
   ```

   Field definitions:
   - **Module** `Exxxx` — `E` plus four digits; identifies a buildable/documentable assembly (decision 3). An opaque key; meaning is held in the registry.
   - **Version** `VVVVVV` — six digits encoding semantic version `major.minor.patch`, two digits each (`1.0.0` → `010000`, `2.1.3` → `020103`). This is the version of the *design*, not of a populated configuration (see alternative D).
   - **Serial** `NNNNNN` — six digits, unique per module+version (`000001`–`999999`). Assigned in Production. The width is sized to the largest declared deployment's lifetime install base of a single design+version — most acutely the universal carrier, which has one near-static version and therefore accumulates the most serials — so the range is not exhausted in practice. There is deliberately **no** overflow-into-version mechanism: bumping the patch when a range fills would leak instance *count* into the design *version*, the same corruption alternative D rejects, and would not even buy more headroom than simply widening the field (see decision 8 and alternative D).
   - **Depth** `DDDDDD` — six digits in three two-digit levels (main module / sub-module level 1 / sub-module level 2), encoding the position within the machine hierarchy. Position only; assigned at integration, never present in the production identifier (decision 7).
   - **Layer** `L` — a single letter naming the document type (decision 9).
   - **Suffix** — a per-instance lifecycle-document tag appended in the production identifier (decisions 10–14).

   Documents are stored flat, with the hierarchy carried entirely in the identifier, so the store can be filtered by identifier pattern (all documents of one module, all reports, all instances at a given position, and so on). The store is realized as an object store, where this filtering is a key-prefix list (decision 15).

2. **Two distinct instance histories, one of which this ADR governs.** Static, per-instance *documents* (test data, calibration, provisioning records) live in the document store and are addressable by suffix. *Operational/runtime events* (firmware-flash events, telemetry, control-decision audit, hash-chain) live in IndustryFlow's audit log per ADR-0004 rev 1 decisions 10 and 16. The two are not merged and not duplicated. Suffixes (decisions 10–14) address only the former.

### Module designation (E-numbers)

3. **An E-number identifies a buildable/documentable assembly.** The existing taxonomy maps directly: the carrier and each of M01–M05 are buildable assemblies and each receives an E-number. A **functional subsystem** (climate, lighting, irrigation, plant monitoring, pollination, power/safety per ADR-0001 decision 7) is *not* an E-number — it is a **position** in the machine, expressed as a depth code (decision 7). The WeAct STM32F4 core board is a COTS sub-component; it is either given its own E-number for traceability or tracked by vendor part number (deferred).

4. **Bare-PCB design and populated assembly are distinguished.** One PCB *layout* is one bare-board design artifact; each standard *populated configuration* of that layout is an assembly E-number that references the shared bare design. This resolves the partial-BOM mechanism of ADR-0014 decision 2 without adding a variant field to the scheme:
   - **Carrier:** one bare design and effectively one assembly. Termination is jumper-selected and the power-input set is the only populate option, so the carrier is treated as a single assembly E-number — one type, one version, many serials, appearing at many depth positions. (This was the original motivating case: the carrier is universal precisely because it has no real variant.)
   - **M01–M05:** one bare design each; in Phase 1 one fully-populated assembly E-number each. Zone-specific partial populations in Phase 2 (ADR-0014 decision 2) receive **additional assembly E-numbers referencing the same bare design** — no new layout, no version churn.

5. **Discipline (electrical / mechanical / pneumatic / fluidic) is a property of the E-module, not a field in the identifier.** It is expressed through decomposition — each discipline-specific buildable unit is its own E-number at its own depth sub-position — and recorded as registry metadata. The identifier stays opaque, with meaning held in the registry. Encoding discipline by partitioning the E-number range (e.g. reserving leading digits per discipline) is **reserved as an option** for the case where pattern-filtering by discipline across the document store becomes necessary; it is not the default.

### Machine designation and position

6. **The cabinet machine (ADR-0001 decision 7) is designated `GBOX_NNNN`** (Grow Box). The machine designation has the general form `<prefix>_NNNN` — four digits, enumerating grow-cabinets across the product (a deployment-scaling field, sized like the serial against the largest declared deployment rather than a single apartment cabinet). The `GBOX` prefix is specific to IndustryGrow grow-cabinet machines, leaving room for other machine families under their own prefixes should the scheme be reused elsewhere.

7. **The integration-phase depth code is the static twin of gateway-side position tagging.** The depth code encodes the same information the gateway resolves at runtime as `(module_class, node_role, zone)` and `production_unit` (ADR-0014 decision 7; ADR-IF-0001, planned). Position is assigned at integration and **never** appears in the production identifier. This preserves instance interchangeability (ADR-0014) and instance mobility between deployments (ADR-0016): the same `Exxxx-VVVVVV-NNNNNN` can be installed at many different depth positions over its life. The depth code's two-digit-per-level width (main `01`–`99`) is intentionally *not* widened with the serial and machine fields: depth is intra-machine, bounded by one cabinet's physical decomposition (~6 functional subsystems per ADR-0001 decision 7), not by deployment size. Scaling a commercial facility means more serials and more `GBOX` machines, each carrying its own depth space — never a deeper hierarchy inside one box. The sizing principle across the scheme is "size to the largest declared deployment," and for depth the largest single machine is already covered with wide margin.

### Instance identity

8. **The serial number is the logistics instance key; the ATECC608 is the cryptographic instance identity.** The serial (six digits, unique per module+version, `000001`–`999999`, with no overflow-into-version — the field is widened instead, see decision 1 and alternative D) is the human- and store-facing instance key. The durable cryptographic identity is the ATECC608 plus its provisioned certificate (ADR-0007). The two are bound to each other in the provisioning record (decision 12); no separate serial authority is invented. Serials are assigned in Production (Phase 2).

### Document layers

9. **Layer set `S` / `D` / `L` / `P` / `M` / `I`** — Schema, Drawing, List/BOM, Protocol, Manual, Interface. Two IndustryGrow-specific bindings: the **`I` (Interface)** layer carries Cyphal subject-ID assignments and DSDL `industryflow.greenhouse.*` type definitions (ADR-0002 rev 3 decision 1; ADR-0005, planned); the **`L` (List)** layer carries the per-module BOMs already drafted in the procurement and sensor-module documents.

### Suffixes (per-instance lifecycle documents)

**Suffix legend.** Quick key to the suffix codes; definitions and rules live in the decisions cited — not repeated here. The set is extensible: a new suffix code (two uppercase letters, not colliding with the decision-9 layer letters) is added by appending a row.

| Suffix | Document | Defined in |
|--------|----------|------------|
| `-QP`  | Quality Protocol | decision 10 |
| `-QR`  | Quality Report | decision 10 |
| `-CP`  | Calibration Protocol | decision 11 |
| `-CC`  | Calibration Certificate | decision 11 |
| `-PR`  | Provisioning Record | decision 12 |

10. **`-QP` / `-QR`.** `-QP` (Quality Protocol) is the raw bring-up and functional-test data for an instance (DFU flash, bxCAN init, Cyphal heartbeat, sensor-presence I²C probing, functional run against bench stimuli). `-QR` (Quality Report) is the evaluated result and acceptance. Discipline-specific test content (dimensional inspection for mechanical, leak/pressure test for pneumatic) lives *inside* the QP document; the suffix code stays generic. No `QP`-electrical / `QP`-pneumatic proliferation.

11. **`-CP` / `-CC`, mirroring the protocol/report pattern.** `-CP` (Calibration Protocol) holds the raw calibration points; `-CC` (Calibration Certificate) holds the resulting coefficients and validity period. Primary driver is M03-ANALYTICS (pH against buffers 4/7/10, EC against 1413 µS/cm) plus any sensor offset trims. Unlike QC, **calibration recurs** (probe drift, scheduled recalibration), so calibration suffixes are dated or sequenced — `-CC-YYYYMMDD` or `-CCnnn` — so a later calibration does not overwrite an earlier record. Deployment-level model calibration (ADR-0016 state-space identification) is explicitly *not* a calibration suffix; it is profile-versioned per ADR-0015 (see decision 13 and the alternatives).

12. **`-PR` (Provisioning Record).** Binds the ATECC608 to its issued certificate and holds the certificate metadata (ADR-0007) — the instance's "birth certificate." It contains the public material only; the private key never leaves the ATECC608. Restricted to engineering/quality access.

13. **Suffixes attach to the instance identifier, never to the integration identifier.** Lifecycle documents key off `Exxxx-VVVVVV-NNNNNN`, which is stable; the integration identifier merely records where the instance currently sits and changes when the instance is moved. This is mandatory under ADR-0016's inventory model: when a board is redeployed, its QP/QR/CP/CC/PR history follows its serial, and a recalibration at the new deployment adds a new dated `-CC` to the same serial — not a record on the new position.

14. **M03 probes are their own instances.** The pH electrode and EC cell are replaceable consumables with independent drift and replacement life, so each is its own E-number with its own serial and its own calibration history. The M03 board's calibration record references the paired probe serial, so the board+probe pairing and the probe's standalone history are both reconstructible.

### Storage backend and object-store mapping

15. **The document store is an object store; identifiers are object keys.** The flat layout (decision 1) is chosen for object storage (S3-compatible buckets), not a hierarchical filesystem. Each artifact is a single object whose key *is* its identifier (`E0001-000001-L.csv`, `E0001-000001-D-Top_Layer.gtl`, a `-CC-YYYYMMDD` calibration certificate, and so on); there are no container objects and no real directories.
    - **Pattern queries are the store's native list operation.** "All documents of one module+version" is `ListObjectsV2(Prefix="E0001-000001-")`; "everything for module `E0001`" is `Prefix="E0001-"`; "every document of one instance" is `Prefix="E0001-000001-000007"`. The identifier-pattern filtering of decision 1 *is* the bucket's prefix listing, executed server-side, with no separate index to build or keep consistent.
    - **A hierarchy is synthesized on demand, never stored.** A `Delimiter` argument makes the store return grouped common-prefixes — a browsable virtual tree — without committing to one shape, so the same flat keyspace supports many groupings at once (by module, by version, by serial, by layer).
    - **Object-store frictions are avoided.** No empty "folder" placeholder objects; re-versioning or relocating an instance never rewrites a subtree (there is none); and the per-instance immutability of QC and provisioning records maps cleanly onto object versioning / write-once policies.
    - **Policy is expressed by prefix.** Lifecycle, retention, replication, and access rules attach to key prefixes, so per-module or per-instance policy (retain all `…-PR` provisioning records; restrict their read scope per decision 12) is stated directly, without a parallel directory-ACL scheme.

    A git working copy or local checkout renders the store as a flat directory; that filesystem view is incidental. The canonical target is a prefix-queried object store, for which the flat, identifier-keyed layout is the idiomatic and performant shape — retrieval functionality, not visual layout, is the design concern.

## Alternatives considered

**A. A flat identifier — a single serial namespace that does not separate type, instance, and position.** *Rejected:* cannot express that one design has many instances (ADR-0014) or that an instance moves between positions and deployments (ADR-0016) without overloading a single number, and loses the ability to address type-level documents and per-instance documents distinctly. The type/instance/position split is the whole point.

**B. Encode discipline in the identifier by default** (range partition or a dedicated field). *Rejected as default:* the identifier is opaque with meaning in a registry; discipline-via-decomposition-plus-metadata is consistent with that and avoids rigid range allocation. Range-partitioning is retained as an option (decision 5) for the day discipline pattern-filtering is actually needed.

**C. Bake position (zone/role/slot) into the part or serial number.** *Rejected:* breaks instance interchangeability (ADR-0014) and instance mobility (ADR-0016). Position belongs in the integration identifier / gateway tagging and is assigned at integration.

**D. Treat each populated-BOM variant as a new Version.** *Rejected:* Version is semantic versioning of the *design*; folding a populate-variant into it corrupts version semantics and the rollback story. A distinct assembly E-number referencing the shared bare design (decision 4) is the correct home. The same reasoning forbids an overflow-into-version mechanism on the serial: incrementing the patch when a serial range fills would fold instance *count* into the design version — the identical corruption reached by a production route rather than a populate route, and it bites hardest on the universal carrier (one type, one near-static version, the most instances), where the patch-bump would be routine rather than an edge case. Widening dominates it on capacity too: a two-digit patch over 999 serials yields only ~10⁵ instances before the overflow must climb into the minor and major version as well, whereas a six-digit serial addresses ~10⁶ honestly and never touches the version at all. The serial is therefore widened (decision 1) rather than overflowed, so instance count never touches the version.

**E. Record firmware/telemetry history as document-store suffixes.** *Rejected:* ADR-0004 rev 1 decisions 10 and 16 already route these to IndustryFlow's audit log. A parallel document record creates two sources of truth for the same events.

**F. Issue a fresh serial when a board moves to a new deployment.** *Rejected:* the serial is the durable instance key bound to the ATECC608; mobility (ADR-0016) must preserve instance history. A move produces a new integration record and (typically) a new dated calibration record on the *same* serial.

## Consequences

### Positive

- One identification scheme spanning IndustryGrow's disciplines — uniform tooling, training, and document store.
- Type, instance, and position are cleanly separated, so ADR-0014's multi-instance scaling and ADR-0016's inventory mobility are supported natively rather than bolted on.
- Instance identity reuses hardware already populated on every carrier (ATECC608); no parallel serial-number authority to operate.
- Per-instance lifecycle documents follow the instance — calibration and provisioning history survive redeployment, which is precisely what the inventory model needs.
- Clean boundary with IndustryFlow: the document store holds static documents, the platform holds operational events, with no duplicated forensic trail.
- Cyphal/DSDL definitions fall naturally into the `I` layer; module BOMs into the `L` layer — no new artifact categories invented.
- Flat storage with identifier-pattern filtering works directly on IndustryGrow artifacts, and maps onto object storage natively — identifiers are object keys and pattern filters are prefix lists (decision 15), so the store needs neither a real hierarchy nor a separate index.

### Negative

- **The registry becomes a critical asset.** Because identifiers are opaque, the `Exxxx → meaning / discipline / bare-design` mapping must be maintained and backed up; losing it makes identifiers hard to interpret.
- **This ADR and the registry are the canonical definition of the scheme** — there is no separate specification document. Validation and parsing tooling (identifier regexes, encoders) must be implemented directly from the field formats given here.
- **The assembly E-catalog grows with zone-specific populations** at medium and large scale (one assembly E per standard population). Manageable, but a real catalog-management surface — most acute exactly where ADR-0014's partial-BOM mechanism is most used.
- **Calibration requires dating/sequencing discipline in the suffix** (decision 11) — a process requirement, not just a format choice, with a validity-period policy still to set.
- **Treating M03 probes as their own instances** adds traceability bookkeeping for consumables; justified by their independent calibration and replacement life, but it is extra records.
- **The integration identifier has no suffix slot by design.** Operators must understand that the durable document key is the instance (`Exxxx-VVVVVV-NNNNNN`), not the current position — counterintuitive for anyone used to position-centric records.

## Relationship to other ADRs

- **ADR-0001** (machine/module data model) — the cabinet `machine` is the machine designation; functional subsystems are depth positions, not E-numbers.
- **ADR-0002 (rev 3)** — carrier and M01–M05 are E-modules; the `I` layer carries the Cyphal/DSDL definitions named there.
- **ADR-0004 (rev 1)** — fixes the document-store / audit-log boundary; firmware and telemetry events stay platform-side and are not suffixes.
- **ADR-0007 (planned)** — ATECC608 binding and certificate issuance are the `-PR` provisioning record and the cryptographic instance identity behind the serial.
- **ADR-0014** — taxonomy reused as the E-vocabulary; partial-BOM realized as distinct assembly E-numbers over a shared bare design; gateway `(module_class, node_role, zone)` tagging is the integration-phase depth code.
- **ADR-0015** — the deployment profile is not a stored document under this scheme; profile content (including model parameters) is profile-versioned, not suffix-addressed.
- **ADR-0016** — inventory mobility is the reason suffixes are instance-keyed not position-keyed; deployment-level model calibration is profile-versioned, distinct from the `-CP`/`-CC` per-instance sensor calibration.
- **ADR-IF-0001 (planned)** — the platform-side `production_unit` / `zone` representation is the data-model counterpart of the integration-phase depth code.

## Deferred decisions

- **Calibration recurrence encoding.** Dated (`-CC-YYYYMMDD`) vs. sequenced (`-CCnnn`), and the calibration validity-period / re-calibration-interval policy.
- **WeAct core board identification.** Own E-number for traceability vs. vendor-part-number tracking.
- **Registry location and tooling.** Where the `Exxxx → meaning / discipline / bare-design` map lives (hardware reference repo, IndustryFlow, or a dedicated registry), and how it links to the document store.
- **Bare-PCB design artifact identification.** Whether the shared bare layout gets its own E-number or is tracked as a hardware-repo artifact under CERN-OHL-S.
- **Identifier validation and parsing tooling.** Regexes and encoders implemented from the field formats in this ADR, including the full suffix set.
- **Optional compact binary encoding** of identifiers for embedded/indexing use — out of scope now.
- **Object-store deployment specifics (decision 15).** Bucket topology (single vs. per-deployment / per-tenant), region and replication, how object versioning interacts with the dated calibration suffix and the write-once QC/provisioning records, and prefix-scoped access-control granularity.
- **Mechanical / pneumatic / fluidic module catalog.** E-numbers for enclosures, trays, plumbing, and actuator hardware are assigned when those modules are designed (ADR-0006 deferred; actuator taxonomy deferred per ADR-0014).

## References

- ADR-0001: IndustryGrow framing — machine/module data model.
- ADR-0002 (rev 3): Field bus architecture — carrier, M01–M05, ATECC608, Cyphal/DSDL.
- ADR-0004 (rev 1): Gateway host hardening and stateless-edge operation — IndustryFlow audit log, firmware events.
- ADR-0007 (planned): PKI architecture — ATECC608 binding, certificate provisioning.
- ADR-0014: Sensor node taxonomy — module classes, module-ID straps, partial-BOM, gateway role/zone tagging.
- ADR-0015: Gateway profile caching and local control loops — profile as single mutation channel.
- ADR-0016: Empirical survey and state-space modeling — sensor-instance inventory and mobility.
- ADR-IF-0001 (planned): `production_unit` entity on the IndustryFlow side.
- `procurement-phase1-data-collection.md`, `sensor-modules-phase1-bom.md`, `carrier-pcb-pin-map.md` (planned operational artifacts; not yet in repo).
- Microchip ATECC608 datasheet.
