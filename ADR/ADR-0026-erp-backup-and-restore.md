<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0026 — ERP backup and restore across two stores

- **Status:** Accepted
- **Date:** 2026-07-26; changed 2026-10-19
- **Parent:** [ADR-0021](ADR-0021-instance-and-integration-erp.md)
- **Related:** [ADR-0004](ADR-0004-gateway-host-hardening.md) (stateless gateway, out of scope), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (identifier as object key, which makes verification a key check), [ADR-0020](ADR-0020-gateway-persistence-model.md) (enumerated gateway state, out of scope), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (blob-first write order that decisions 3–4 invert), [ADR-0023](ADR-0023-machine-readable-type-registry.md) (type registry in git; the mirror's restore path), [ADR-0024](ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) (custody model decision 6 reuses; CA material excluded), [ADR-0025](ADR-0025-cultivation-profile-signing.md) (key-proliferation argument decision 7 applies; signing key excluded)

## Context

The ERP is the only copy of its data for roadmap stages 1–10 (ADR-0021 dec 2). It is also the serial-allocation authority (ADR-0021 dec 4): losing the counter loses correctness, not history, because the next allocation re-issues a serial already stamped on a board and bound to a certificate. Every other record can in principle be reconstructed from an operator's notes; the counter cannot.

The ERP's state spans the document store and the warehouse, with an ordering invariant between them: the blob is written first and its key indexed second (ADR-0022 dec 7), so a recorded key always resolves and the permitted failure is an orphan blob. A backup of two stores taken at two instants either preserves that invariant or inverts it.

The operator CA and profile-signing key have their own ceremony-based custody (ADR-0024 dec 5–7; ADR-0025 dec 5). The gateway is the stateless edge (ADR-0004; ADR-0020 dec 5). The type registry is in git (ADR-0023 dec 1). Operator-private data stays off the public repository (ADR-0021 dec 14).

## Decision

### Scope

1. **The backup covers exactly two things: the ERP's document store, and the warehouse objects not reconstructible from the repository.** The document store holds the serial counter, both kinds of provisioning binding, integration history, the lifecycle index, profile versions, firmware intent and SP stock; the uploaded lifecycle documents exist nowhere else. The repository mirror in the warehouse, the operator CA, the profile-signing key, the gateway and the type registry are excluded by name, because a backup that covers everything is one nobody runs or tests.
2. **The reason the backup exists is the serial counter.** Verification, restore refusals and retention answer first to whether a serial could be issued twice, and only then to convenience.

### Ordering across the two stores

3. **A backup captures the index first and the blobs second — the inverse of the write order.** The warehouse is at every instant a superset of what the index references, so a later warehouse copy still covers an earlier index copy. Anything uploaded in between arrives as an orphan blob, the failure direction ADR-0022 dec 7 already permits.
4. **A restore replays in reverse: blobs first, then the index.** At no point does an index exist that references objects not yet present, and an interrupted restore is recovered by re-running it.

### Form and custody

5. **The document store is backed up as a logical dump, not a volume snapshot.** The ERP runs on whatever host the operator has (ADR-0021 dec 1), and a snapshot restores only onto a near-identical host and database major version.
6. **Backups follow the custody model of ADR-0024 dec 5–7 and must not share the warehouse's failure domain.** One compromised credential, deleted bucket or unpaid account must not take the data and its copy together. Reusing the CA's model gives the operator one discipline for both.
7. **The backup is encrypted under an operator passphrase before it leaves the host, introducing no new long-lived key.** It carries operator-private data to locations chosen for durability, not confidentiality. A passphrase avoids a fourth long-lived secret beside the trust roots of ADR-0025 dec 4; a keypair is permitted, plaintext is not, and a forgotten passphrase is an unrecoverable backup.

### Restore

8. **A restore onto a store whose serial counter is ahead of the backup's stops and requires explicit operator reconciliation.** Restoring would re-arm serials that may already be stamped on hardware. That failure is undetectable afterwards, so refusing is more useful than completing.
9. **A restore is verified before it is called done, and the check is the cross-store invariant: every object key the restored index names resolves in the warehouse.** It is the property a two-store restore can break, otherwise discovered only when a document link fails months later.
10. **An untested backup is not a backup.** The procedure includes restoring into a throwaway database and running decision 9's check against it, at a cadence the runbook sets.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | Back up the whole warehouse bucket, including the repository mirror | Copies what git versions and inflates every backup, which decides whether second copies are kept and restores tested |
| 3 | Snapshot the two stores in either order and reconcile afterwards | A dangling index row cannot be reconciled against an object the backup does not hold; correct ordering costs nothing |
| 5 | Volume snapshots of the database directory as the primary form | Bound to a database major version and host filesystem, and not inspectable without restoring; permitted as an extra pre-upgrade rollback |
| 6 | Rely on the object store's own durability and versioning for the blobs | Shares the account and provider with the live data; does not survive bucket deletion or account loss |
| 7 | Require encryption to a keypair rather than a passphrase | Adds a fourth long-lived secret to custody (ADR-0025 dec 4); permitted as a choice |
| 7 | No application-level encryption; rely on the target's server-side encryption | The operator holds neither those keys nor that access control |
| 8 | Let the restore complete and warn about the serial counter | A scrolled-past warning is not a control for a silent, permanent, physical failure |

## Consequences

**Positive**

- The pre-cloud system of record is recoverable, and the serial counter has a named guard against the way a restore corrupts it.
- The cross-store invariant holds on the backup path in both directions, not by luck of timing.
- Excluding the reconstructible mirror keeps backups small enough that two copies and restore rehearsals are kept.
- Custody reuses the CA's model.
- Retention, cadence and rehearsal frequency are runbook values.

**Negative**

- A forgotten passphrase loses the data while the operator believes it protected.
- Decision 8's refusal requires knowing which serials reached hardware, which the ERP cannot tell.
- A backup places operator-private data in more locations than the live store; encryption bounds the exposure, not the count.
- Irreplaceable data placed somewhere decision 1 does not name — a new collection outside the ERP's sets, a new object kind — is silently not covered.
- The dump and the object copy fail independently, so the procedure must detect a partial backup.

## Deferred

| Open question | Settled by |
|---|---|
| Scheduling and automation: timer on the ERP host, operator workstation, or by hand | Operator; nothing here requires a daemon |
| Point-in-time recovery by continuous archiving | Maintainers, when the store stops being small and low-churn |
| What backup covers once IndustryFlow holds the `[F]` data | ADR-IF-0001, at stage 11 |
| Restoring a subset — one document or one integration record | Maintainers, when a partial loss occurs |

## References

- `erp/app/backup.py` — save, verify and restore; the counter refusal.
- `erp/deploy/backup.sh` — encryption under the operator passphrase.
- `erp/deploy/README.md` — retention, cadence, rehearsal and custody values.
