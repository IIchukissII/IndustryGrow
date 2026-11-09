<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ERP backup and restore

The manual of [ADR-0026](../ADR/ADR-0026-erp-backup-and-restore-across-two-stores.md): how the ERP backup is captured, encrypted, verified and restored across the document store and the warehouse.

## ADR-0026 dec 1

The backup covers every ERP collection and the warehouse objects whose key is a lifecycle key. Every other warehouse key, the `store/` mirror among them, is skipped; the manifest names what is excluded.

| Code | Role |
|---|---|
| `erp/app/backup.py::BACKED_UP_COLLECTIONS` | Every `foundation.*` and `domain.*` collection the ERP defines |
| `erp/app/backup.py::is_backed_up_object` | Selects the warehouse keys to copy, by the lifecycle-key grammar |
| `erp/app/models/identifiers.py::LIFECYCLE_KEY_RE` | The lifecycle-key grammar, in every form a writer produces |
| `erp/app/backup.py::save` | Writes the manifest, the dump and the selected objects into one archive |
| `erp/tests/test_backup.py` | Asserts each key form, the near misses, the mirror exclusion and the collection allowlist |
| `erp/tests/test_api.py::test_every_key_the_upload_writes_is_backed_up` | Uploads through the API and asserts every written and every indexed key is selected |

### Selection by key

| Key form | Selected |
|---|---|
| `<instance>-QP`, `-QR`, `-CP`, `-CC`, `-PR` | yes |
| `<instance>-CP-` or `-CC-` with `YYYYMMDD` or `YYYYMMDDThhmmss`, optionally `-n` | yes |
| `<instance>-<suffix>-superseded-YYYYMMDDThhmmss`, optionally `-n` | yes |
| any other key | no |

## ADR-0026 dec 2

The manifest records the serial counter's high-water mark per module and version. Verification prints it, and restore compares it before anything is written (dec 8).

| Code | Role |
|---|---|
| `erp/app/backup.py::_counter_state` | Reads `last_serial` per counter from `foundation.serial_counter` |
| `erp/app/backup.py::verify` | Reports the archived counters |

## ADR-0026 dec 3

`save` reads the counters and the indexed object keys and takes the dump, then copies the warehouse objects. Any indexed key missing from the copied objects aborts the capture as a fault of the live store.

| Code | Role |
|---|---|
| `erp/app/backup.py::save` | Index first, blobs second; aborts on a dangling key |
| `erp/app/backup.py::_mongodump` | Takes the dump, or accepts one taken in the `mongo` service |
| `erp/deploy/backup.sh` | Takes the dump in the `mongo` service before calling `save` |

## ADR-0026 dec 4

`restore` writes every archived object to the warehouse, then replays the dump. A failure after the object pass leaves the objects in place; a second run puts them again and finishes the index.

| Code | Role |
|---|---|
| `erp/app/backup.py::restore` | Blobs first, then `mongorestore --drop` |

## ADR-0026 dec 5

The document store is captured with `mongodump --archive` and replayed with `mongorestore`. The application image carries no MongoDB Database Tools; `backup.sh` takes the dump in the `mongo` container and hands it to `save`.

| Code | Role |
|---|---|
| `erp/app/backup.py::_mongodump` | Runs `mongodump`, or copies a prepared dump |
| `erp/deploy/backup.sh` | Streams the dump from the `mongo` container into the `erp` container |

## ADR-0026 dec 6

The script writes one encrypted archive into the output directory and prunes older ones when told to. The second copy outside the host and outside the warehouse account is an operator step: the script ends with a notice that the backup is not finished until that copy exists.

| Code | Role |
|---|---|
| `erp/deploy/backup.sh` | Writes the first copy, applies retention, prints the custody notice |

## ADR-0026 dec 7

`save` writes a plain archive and says it is unencrypted. `backup.sh` refuses to run without a non-empty passphrase file, encrypts symmetrically from that file with `gpg`, shreds the plaintext and leaves only the encrypted file. The application never handles the passphrase.

| Code | Role |
|---|---|
| `erp/deploy/backup.sh` | Passphrase check, encryption, plaintext removal |
| `erp/app/backup.py::save` | Produces the plaintext archive inside the container |

## ADR-0026 dec 8

Before any write, `restore` compares the live counters with the archived ones. A counter the live store has advanced beyond the archive stops the restore with the list of counters; `--force` proceeds after the operator has reconciled. A restore into another database (`--into`) skips the comparison.

| Code | Role |
|---|---|
| `erp/app/backup.py::counters_ahead` | Counters where the live store is ahead, including counters the archive lacks |
| `erp/app/backup.py::restore` | Refuses unless `--force` |
| `erp/tests/test_backup.py` | Asserts the ahead, unknown and behind cases |

## ADR-0026 dec 9

The cross-store check runs at three points: at capture against the copied objects, on an archive before restore, and against the running store after restore.

| Code | Role |
|---|---|
| `erp/app/backup.py::save` | Aborts when an indexed key was not copied |
| `erp/app/backup.py::verify` | Every indexed key in the manifest has an object in the archive |
| `erp/app/backup.py::check_live` | Every `object_key` in `foundation.lifecycle_doc` resolves in the warehouse |

## ADR-0026 dec 10

`backup.sh` verifies each archive before encrypting it and discards one that fails. A rehearsal decrypts an archive, verifies it and restores it with `--into` into a throwaway database. `check_live` reads the configured database only, so on a rehearsal the cross-store check runs on the archive, not on the restored database.

| Code | Role |
|---|---|
| `erp/deploy/backup.sh` | Verify before encrypt |
| `erp/app/backup.py::build_parser` | The `save`, `verify`, `restore --into` and `check-live` commands |

Links: [erp/deploy/README.md](../erp/deploy/README.md) (taking, rehearsing and restoring a backup; retention, cadence, copies and encryption values).
