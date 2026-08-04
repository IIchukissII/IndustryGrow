<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0026: ERP backup and restore across two stores

- **ID:** ADR-0026
- **Status:** Accepted
- **Date:** 2026-07-26
- **Project:** IndustryGrow
- **Parent:** ADR-0021 (rev 3)
- **Companions:** ADR-0000, ADR-0004 (rev 1), ADR-0007 (rev 1), ADR-0017 (rev 2), ADR-0020, ADR-0022 (rev 1), ADR-0024, ADR-0025
- **Realizes:** ADR-0021's deferred *"backup/restore and operator-private data handling"*

## Context and problem

The ERP is the pre-cloud system of record (ADR-0021 decision 2). Nothing else holds
what it holds, and IndustryFlow — the durable home its data eventually migrates to
— arrives at stage 11. So for stages 1–10 the ERP is the only copy, and it has no
backup procedure. The deployment runbook says so and offers a `mongodump` one-liner
described honestly as a stopgap.

Two properties make this more than "run a dump nightly".

**The ERP is the serial-allocation authority (ADR-0021 decision 4).** Serials are
issued gap-free from a counter, bound to physical parts and to ATECC certificates
(ADR-0007; ADR-0017 decision 8). Losing the counter is not a loss of history — it
is a loss of *correctness*: the next allocation re-issues a serial that is already
stamped on a board somewhere, and two physical parts then claim one identity. Every
other record in the store can in principle be reconstructed by an operator with
good notes. That one cannot.

**Its state spans two stores with an ordering invariant between them.** ADR-0022
decision 7 writes the blob to the warehouse *first* and records its key in the ERP
index *second*, which buys referential integrity in one direction: a recorded key
always resolves, and the permitted failure is an orphan blob. A backup of two stores
taken at two instants either preserves that invariant or inverts it, depending on
which is captured first — and inverting it produces a restored database whose index
points at objects that are not there.

Inherited and not re-opened here:

- **The operator CA and the profile-signing key have their own custody**, and it is
  a ceremony rather than a backup job (ADR-0024 decisions 5–7; ADR-0025 decision 5).
- **The gateway is the stateless replaceable edge** whose permitted persistent state
  is enumerated and is all either re-derivable or on a chip (ADR-0004; ADR-0020
  decision 5).
- **The type registry is `REGISTRY.md` in git** (ADR-0023 decision 1).
- **Operator-private production data stays off the public repo** (ADR-0021
  decision 14).

## Decisions

### What is backed up, and what deliberately is not

1. **The backup covers exactly two things: the ERP's document store, and the
   warehouse objects that are not reconstructible from the repository.** Everything
   else is excluded, by name, because a backup that tries to cover everything is one
   nobody runs or tests:
   - **The Mongo store — in scope.** The serial counter, provisioning bindings
     (E-instance and machine), integration history, the lifecycle-document index,
     profile versions, and SP stock and placement. Irreplaceable.
   - **Uploaded lifecycle documents (`-QP/-QR/-CP/-CC/-PR`) — in scope.** A
     calibration certificate exists in the warehouse and nowhere else.
   - **The `store/` mirror in the warehouse — out of scope.** It is a copy of
     repository content, restorable with `store_sync` (ADR-0023; ADR-0017
     decision 15). Backing it up would multiply the backup's size by the part of it
     that git already versions.
   - **The operator CA, the profile-signing key, the gateway, the type registry —
     out of scope**, each for the reason given in Context.

2. **The reason the backup exists is the serial counter; the rest is why it is worth
   restoring.** Stated as a decision because it orders the priorities of everything
   below: verification, restore refusals, and retention all answer to *"could this
   cause a serial to be issued twice"* before they answer to convenience.

### Ordering across the two stores

3. **A backup captures the index *first* and the blobs *second* — the inverse of the
   write order.** The live path is blob-then-index (ADR-0022 decision 7), so at every
   instant the warehouse is a superset of what the index references. Capturing the
   index at T₁ and the warehouse at T₂ > T₁ therefore yields a warehouse copy that is
   still a superset of the index copy: every key the restored index names was written
   before T₁ and is present. Anything uploaded between T₁ and T₂ arrives as an orphan
   blob — the failure direction decision 7 already permits. Capturing them the other
   way round produces the failure it forbids: an index that names an object the
   backup does not contain.

4. **A restore replays that in reverse: blobs first, then the index.** Same invariant
   from the other side — at no point during a restore does an index exist that
   references objects not yet present. A restore interrupted halfway leaves orphan
   blobs and an untouched index, which is recoverable by re-running it.

### Form and custody

5. **The Mongo side is a logical dump, not a volume snapshot.** ADR-0021 decision 1's
   audience runs this on a host of their own choosing and may rebuild on whatever
   hardware they have; a snapshot restores cleanly only onto a near-identical host and
   the same database major version. A portable archive is slower to take and is the
   one that still works on the day it is needed.

6. **Backups follow the custody model of ADR-0024 decisions 5–7, and must
   additionally not share the warehouse's failure domain.** Reusing that model
   rather than inventing one means the operator learns a single discipline for the
   CA root and for this. The addition is the load-bearing part: the warehouse and
   its backup sitting in one provider account means one compromised credential, one
   deleted bucket or one unpaid invoice takes the data and its copy together, which
   is the event a backup exists to survive.

7. **The backup is encrypted under an operator passphrase before it leaves the host,
   and this introduces no new long-lived key.** It carries operator-private
   production data (ADR-0021 decision 14) to locations chosen for durability rather
   than for confidentiality, so it cannot travel as plaintext. Symmetric encryption
   under a passphrase is chosen over a keypair specifically to avoid a *fourth*
   long-lived secret in custody, after the three trust roots ADR-0025 decision 4
   names. The marginal key is the one that gets held badly — that is ADR-0025's own
   negative consequence, applied here to decline the increment. The cost is accepted and
   must be stated wherever the procedure is written: **a forgotten passphrase is an
   unrecoverable backup.** An operator who prefers a keypair loses nothing by using
   one; what is forbidden is plaintext.

### Restoring is not symmetric with backing up

8. **A restore rolls the serial counter backwards, so the tooling refuses to do it
   silently.** If serials were allocated after the backup point — and parts stamped
   with them — restoring re-arms those numbers for re-issue. So a restore onto a
   store whose counter is *ahead* of the backup's must stop and require the operator
   to reconcile explicitly, rather than completing and reporting success. This is the
   one place where refusing to run is more useful than restoring: the failure it
   prevents is undetectable afterwards and is stamped into hardware.

9. **A restore is verified before it is called done, and the check is the
   cross-store invariant.** Every object key the restored index names must resolve in
   the warehouse (ADR-0021 decision 7's "a recorded key always resolves"). This is
   exactly the property a two-store restore can break and the only one whose failure
   is otherwise discovered by a person clicking a document link months later.

10. **An untested backup is not a backup.** The procedure includes restoring into a
    throwaway database and running the verification of decision 9 against it; the
    runbook says how often. This is a decision because the alternative is the default:
    backups that have never been restored, discovered to be unrestorable at the only
    moment it matters.

## Alternatives considered

**A. Volume snapshots of the Mongo data directory instead of a logical dump.** Fast,
byte-exact, no schema assumptions. *Rejected as the primary form:* it binds the
backup to a database major version and a host filesystem, so it restores onto a
near-identical box or not at all, and ADR-0021 decision 1 explicitly places the ERP
on whatever host the operator has. It also cannot be inspected — an operator cannot
tell whether a snapshot is good without restoring it. Nothing forbids an operator
taking snapshots *as well*, before an upgrade; they are a rollback convenience, not
the copy that survives the box.

**B. Back up the whole warehouse bucket, including the `store/` mirror.** One rule,
no exclusions to get wrong. *Rejected:* the mirror is repository content (ADR-0023),
so this copies what git already versions and inflates every backup with the part of
it that is least at risk. Size is not a cosmetic concern here — it decides whether
the operator keeps the second copy and whether restores get tested (decision 10).

**C. Rely on the object store's own durability and versioning for the blobs, and back
up only Mongo.** Providers replicate; object versioning exists. *Rejected:* it makes
the backup depend on the same account and the same provider as the live data, which
decision 6 rejects for exactly this reason, and it does not survive the operator
deleting the bucket or losing the account. Provider durability protects against disk
failure, which is not the failure mode that loses a deployment.

**D. Encrypt to an age/gpg keypair rather than a passphrase.** Stronger, and the
backup host needs only the public half. *Rejected as the requirement,* permitted as a
choice: it adds a fourth long-lived secret to operator custody, and the project has
already argued (ADR-0025 decision 4) that the marginal key is the one held badly.
Decision 7 forbids plaintext, not keypairs.

**E. No application-level encryption; rely on the target's server-side encryption and
access control.** Least work. *Rejected:* the operator does not hold those keys, and
decision 6 deliberately sends a copy somewhere chosen for durability rather than for
trust. Operator-private production data (ADR-0021 decision 14) should not be
plaintext at a location whose access control the operator does not own.

**F. Snapshot the two stores in either order and reconcile afterwards.** Simpler
tooling; an orphan-and-dangling reconciliation pass at restore time. *Rejected:* a
dangling index row cannot be reconciled — the object is not in the backup, so there is
nothing to reconcile it against, and the only repair is deleting the record that
proves a calibration happened. Ordering the capture correctly (decision 3) costs
nothing and makes the unrepairable case impossible.

**G. Let the restore complete and warn about the serial counter.** Less obstructive.
*Rejected:* a warning in a terminal that scrolls is not a control. The failure it
guards is silent, permanent and physical — a serial stamped on two boards — so
decision 8 makes it a refusal that the operator must clear deliberately.

## Consequences

### Positive

- The pre-cloud system of record becomes recoverable, which it was not, and the one
  irreplaceable value in it (the serial counter) gets a named guard against the
  specific way a restore can corrupt it.
- The cross-store invariant ADR-0022 decision 7 establishes for the live path now
  holds for the backup path too, in both directions, rather than by luck of timing.
- Excluding the reconstructible mirror keeps backups small enough that the
  two-copies rule and the restore test are realistic rather than aspirational.
- Custody reuses the CA's model, so an operator learns one discipline for both.

### Negative

- **A forgotten passphrase is an unrecoverable backup.** This is a real way to lose
  the data while believing it is protected, and no amount of documentation fully
  removes it. Decision 7 accepts it in exchange for not adding a fourth key.
- **Restores can refuse to run**, and the operator hitting decision 8's refusal is
  someone already having a bad day. The reconciliation they are asked to do requires
  knowing which serials reached hardware, which the ERP cannot tell them.
- **A backup is a second home for operator-private data**, in more locations than the
  live store. Encryption bounds the exposure; it does not remove the fact that the
  data now exists in more places, including ones the operator may not control.
- **The exclusions must be maintained.** If a future feature puts irreplaceable data
  somewhere decision 1 does not name — a new collection, a new object prefix — the
  backup silently does not cover it. That is the cost of an allowlist, chosen for the
  same reason ADR-0022 decision 7 chose one.
- **Two mechanisms, one procedure.** The Mongo dump and the object copy fail
  independently, so a partial backup is possible and the procedure has to notice.

## Deferred decisions

- **Retention and cadence figures** — how many copies, how long, how often, and how
  often a restore is tested. Values belong in the deployment runbook, not here
  (ADR-0000 decision 2).
- **Scheduling and automation.** Whether the backup runs from a timer on the ERP host,
  from the operator's workstation, or by hand is an operational choice; nothing here
  requires a daemon.
- **Point-in-time recovery.** This ADR decides periodic whole-store copies. Continuous
  archiving (oplog tailing) is a different mechanism with different costs and is not
  needed while the store is small and low-churn.
- **The stage-11 disposition.** When IndustryFlow becomes the durable home (ADR-0021
  decisions 2–3), the ERP's `[F]` data has a second durable copy by definition and
  this ADR's role shrinks to the `[D]` layer — the same lifecycle demotion ADR-0020
  decision 1 describes for the gateway store. What backup means after that migration
  is decided with ADR-IF-0001.
- **Restoring a subset.** Recovering one deleted document or one integration record,
  rather than the whole store, is not addressed; the unit here is the whole store.

## References

- ADR-0021 (rev 3): the ERP as pre-cloud system of record — decision 2 (only copy
  pre-cloud), decision 4 (serial-allocation authority, the reason this ADR exists),
  decision 7 (the document index over the object store, whose invariant decision 9
  verifies), decision 14 (operator-private data), and the deferred *"backup/restore"*
  this record realizes.
- ADR-0022 (rev 1): the API — decision 7's blob-first write ordering, which decisions
  3 and 4 invert for capture and replay.
- ADR-0024: the operator CA — decisions 5–7's two-copies-separate-locations custody,
  reused by decision 6, and the CA material decision 1 excludes.
- ADR-0025: profile signing — decision 4's argument against key proliferation, applied
  by decision 7; decision 5's custody, excluded by decision 1.
- ADR-0004 (rev 1) / ADR-0020: the stateless gateway and its enumerated persistent
  state — why the edge is out of scope.
- ADR-0023: the type registry in git, and `store_sync` as the mirror's restore path.
- ADR-0017 (rev 2): decision 15's identifier-as-object-key, which is what makes the
  index-to-blob verification of decision 9 a simple key existence check.
