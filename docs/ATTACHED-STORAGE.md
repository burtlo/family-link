# Attached storage qualification

This document is the canonical overview of removable storage qualification on the
BOX-3. It describes the implemented SENSOR microSD test path and its current
evidence. For product storage roles, capacity, and purchasing guidance, see
[`STORAGE.md`](STORAGE.md).

## Scope and non-goals

The qualified path is an isolated SDMMC experiment on an ESP32-S3-BOX-3 mounted
on the SENSOR accessory. H35 discovers a card, H37 classifies its partition and
filesystem metadata without writing, and H38 formats and exercises one bounded
FAT32 volume.

This work does **not** implement the X02 durable outbox or change
`firmware/v1/`. The server archive and PSRAM recording path are separate from
BOX removable storage. It also does not qualify H32 on-chip storage, recording
cadence, fault or power-loss recovery, near-full behavior, physical removal, the
whole card, or a production partition layout.

## Hardware and media constraints

- The tested accessory is **SENSOR**, whose microSD slot uses SDMMC on
  GPIO 9/11/12/13/14/42. SENSOR replaces the DOCK on the gold-finger connector.
- The supported target is a **16 or 32 GB microSDHC** card with 512-byte sectors.
  Capacity comes from the card's reported geometry; it is never inferred from
  the label or Disk Utility.
- The passing card reports 61,071,360 sectors
  (31,268,536,320 bytes). The retired 64 GB SDXC card and its
  121,503,744-sector geometry are not valid inputs for new runs.
- DOCK USB-A storage is a different, currently unimplemented qualification
  path. The DOCK port also conflicts with a directly attached USB camera.
- Flashing and serial capture use the main unit's USB-C port.

See [`HARDWARE.md`](HARDWARE.md) for board facts and [`STORAGE.md`](STORAGE.md)
for accessory and media selection.

## Implemented components

All device code is isolated under `firmware/demos/`; none is product firmware.

| Component | Responsibility |
|---|---|
| `h35_sdmmc_discovery.c` | Initialize SENSOR SDMMC once, report card geometry and a private CID-derived identity, then stop without media writes. |
| `h37_sdmmc_classification.c` | Accept bounded host-directed sector reads and expose no write command. |
| `h38_sdmmc_filesystem.c` | Run the fixed BIND → LAYOUT → FORMAT → IO → FINISH protocol and report a closed result set. |
| `h38_disk_guard.*` / `h38_fatfs_adapter.*` | Map a virtual disk onto the approved card interval, enforce bounds and budgets, split driver calls, and block trim/erase. |
| `h38_filesystem_io.*` | Run the deterministic file, rename, remount, retention, directory, and reclamation fixtures. |

The matching host orchestrators are:

- `scripts/h35_attached_discovery.py`: `inventory`, `prepare-run`, `build`,
  `validate-build`, `flash-capture`, `parse`, `restore`, and `host-checks`.
- `scripts/h37_attached_classification.py`: `prepare`, `build`, `validate`,
  `flash-capture`, `parse`, `restore`, and `host-checks`.
- `scripts/h38_attached_filesystem.py`: `preflight`, `prepare`, `build`,
  `validate`, and `run`.

Each controller uses immutable run metadata, strict record parsing, app-only
experiment flashing, and mandatory BOX restoration. H38 additionally binds a
canonical intent to the build and enforces host and firmware time/byte limits.

## Qualification stages and current result

1. **H35 discovery:** prove that SDMMC detects this card and record its geometry
   and private identity. The 32 GB card passed with `result=detected`; see
   [`h35-32gb-20261007/summary.json`](evidence/attached-storage-qualification/h35-32gb-20261007/summary.json).
2. **H37 read-only classification:** read only the metadata sectors selected by
   the classifier, confirm the H35 identity and geometry, and require
   `read_complete`, zero writes, and verified BOX restoration. The card passed
   with two 512-byte reads, an MBR partition, and a FAT32 signature; see
   [`h37-32gb-20261007/`](evidence/attached-storage-qualification/h37-32gb-20261007/).
3. **H38 bounded filesystem/I/O:** bind the card and current MBR, replace only
   LBA 0 with the reviewed layout, format a virtual 512 MiB FAT32 volume at
   `[32768,1081344)`, run the fixed I/O profile, and restore the BOX. Epoch
   `d953a7fbfd1f9054aacff6f6c79f8ae1` passed with `io_complete`; see
   [`h38-32gb-20261008/`](evidence/attached-storage-qualification/h38-32gb-20261008/).

The detailed pass criteria and limits remain normative in
[`plans/h38-bounded-sd-filesystem.md`](plans/h38-bounded-sd-filesystem.md).
The H38 pass qualifies only `sdmmc_bounded_fat32_v1` on the bound 32 GB card.

## Per-card and per-epoch binding

The stages form one identity and geometry chain:

- H35 records the measured sector count and a CID-derived identity digest.
- H37 must use that H35 capture. It rechecks geometry and identity, then
  preserves the actual LBA 0 sector and classification read plan.
- H38 intent takes `card_sector_count` and `old_mbr_sha256` from that completed
  H37 run, plus the H35 reference epoch and private identity digest. Firmware
  receives the sector count at build time and rejects a different live card.
- `BIND` performs no writes. It must match the live geometry, CID identity,
  expected current MBR, intent digest, and runtime ELF before `LAYOUT`.

After a successful H38 `LAYOUT`, the card's MBR no longer matches the original
H37 snapshot. A retry must use either a fresh read-only H37 capture or
`--prior-h38-run-dir` pointing to independently validated evidence of the actual
prior layout. Reusing the pre-LAYOUT digest causes the expected BIND failure.

The H35 digest is an identity cross-check, not proof against card cloning.

## Private and public evidence

Private epochs live outside the repository under
`~/family-link-storage-experiments/`. `scripts/attached_storage_paths.py`
enforces an outside-repository directory, mode `0700`, and no symlink. It also
resolves the active H35 capture in this order: explicit argument, environment
variable, current-capture pointer, then the historical default.

Keep raw serial captures, sector bytes and hashes, CID fields and digests,
device fingerprints, full flash/NVS images and hashes, private paths, filenames,
credentials, and household data private. Private run files normally use mode
`0600`.

Commit only sanitized summaries under
`docs/evidence/attached-storage-qualification/`. Public evidence may include
epochs, build identifiers, measured geometry, approved volume bounds, aggregate
counts and timings, parser verdicts, limitations, and BOX restoration status.
The card-content backup waiver applies only to the identified disposable card;
it does not waive BOX restoration.

## Operator workflow

1. Mount the BOX-3 on SENSOR, insert the supported microSDHC card, connect the
   main-unit USB-C, and confirm exactly one intended serial device.
2. Select a verified 16 MiB BOX restore image whose private device fingerprint
   matches the connected BOX.
3. Create and run a fresh H35 epoch: `prepare-run` → `build` →
   `validate-build` → `flash-capture` → `parse --set-current`. Require
   `detected` and verified restore.
4. Create and run H37 against that H35 capture: `prepare` → `build` →
   `validate` → `flash-capture` → `parse`. Require `read_complete`,
   `media_writes=0`, matching geometry, and verified restore.
5. Commit the H38 source set before `prepare`; H38 refuses uncommitted source
   inputs. Run `preflight`, then `prepare --h37-run-dir <private-h37>` →
   `build` → `validate`.
6. Run one H38 epoch with the intended serial port and backup directory. Use
   `--use-existing-restore-image` only when the verified baseline is the
   authorized restore target. Require `io_complete`, a verified host result,
   full BOX/NVS readback, and healthy original-app boot.
7. Generate and commit only the sanitized evidence summary. Record the
   unqualified stages explicitly.

Stop rather than bypass a gate when:

- **BIND fails after LAYOUT:** the expected MBR is stale. Bind a reviewed prior
  H38 layout or take a fresh H37 read-only capture; never auto-resume.
- **Backup fingerprint differs:** the restore image belongs to another device
  or device state. Select or create the correct verified baseline.
- **H38 prepare reports uncommitted source:** commit or deliberately discard the
  relevant source changes before creating the immutable intent.
- **Geometry or H35 identity differs:** treat the card as different media and
  restart at H35.
- **A phase times out or lacks a valid terminal:** preserve partial private
  evidence, perform mandatory BOX recovery, and report failed/incomplete. Do not
  format, repair, or continue later commands.

## Implemented versus planned

| Capability | Status |
|---|---|
| SENSOR SDMMC discovery and per-card geometry/identity | Implemented; H35 passed on the bound 32 GB card |
| Read-only MBR/FAT signature classification | Implemented; H37 passed with zero writes |
| Bounded 512 MiB FAT32 layout, format, mount, file I/O, five software remounts, rename and reclamation checks | Implemented; H38 v1 passed |
| Strict host parsing, intent/build binding, app-only experiment flash, full BOX restore/readback | Implemented and exercised by the passing epoch |
| H32 recording-cadence comparison on attached storage | Planned; not run |
| Fault matrix, power-loss durability, near-full refusal/reclamation, and physical removal/reinsertion | Planned; not qualified |
| Full-card or general 16/32 GB card-family qualification | Not established |
| DOCK USB mass-storage qualification | Not implemented |
| X02 durable outbox on SD or USB | Not implemented |
| Production partition/backend selection | Open |

## Detailed references

- Overall staged campaign:
  [`plans/attached-storage-qualification.md`](plans/attached-storage-qualification.md)
- SDHC geometry and identity migration:
  [`plans/h38-sdhc-geometry-qualification.md`](plans/h38-sdhc-geometry-qualification.md)
- Frozen H38 profile and parser contract:
  [`plans/h38-bounded-sd-filesystem.md`](plans/h38-bounded-sd-filesystem.md)
- Sanitized run evidence:
  [`evidence/attached-storage-qualification/`](evidence/attached-storage-qualification/)

Plan status tables and this evidence index were updated after the 2026-10-08 pass.
Committed run evidence remains the authority for what completed on hardware; plans
remain authoritative for scope, limits, and unqualified follow-on stages.
