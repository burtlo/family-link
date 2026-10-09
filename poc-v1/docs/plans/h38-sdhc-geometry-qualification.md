# H38 SDHC geometry binding and qualification (32 GB track)

| Field | Value |
|---|---|
| **Status** | **Complete for bound 32 GB card:** Phase 0 landed; hardware **H35** ([`h35-32gb-20261007`](../evidence/attached-storage-qualification/h35-32gb-20261007/summary.json)), **H37** ([`h37-32gb-20261007`](../evidence/attached-storage-qualification/h37-32gb-20261007/README.md)), **H38** bounded I/O pass ([`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/README.md), epoch `d953a7fbfd1f9054aacff6f6c79f8ae1`). Partial attempt: [`h38-32gb-20261007`](../evidence/attached-storage-qualification/h38-32gb-20261007/README.md). |
| **Private data root** | `~/family-link-storage-experiments/` (`scripts/attached_storage_paths.py`) |
| **Supersedes** | 64 GB geometry constants and H37 epoch `43d7e2e5792ca6c1e494ff7cb06f3353` for all new prepare/build/run work |
| **Depends on** | [`h38-bounded-sd-filesystem.md`](h38-bounded-sd-filesystem.md) (frozen protocol, I/O matrix, parser gates) |
| **Hardware** | BOX-3 on **SENSOR** perch, **32 GB microSDHC** (FAT32), main-unit USB-C for serial/flash |
| **BOX baseline (operator)** | Device running **h31**; operator states no separate backup required — scripts still fail closed unless a verified full image exists or a fresh backup is taken at epoch start |
| **Identity (operator)** | **New physical card** → run **fresh H35** read-only discovery on this card before H37/H38 |
| **Success** | One H38 epoch: BIND → LAYOUT → FORMAT → IO → FINISH, parser `io_complete`, mandatory BOX restore + original-app boot, sanitized evidence committed |

## Why this plan exists

The H38 v1 **protocol, budgets, and 512 MiB test volume** (`volume_start_lba=32768`, `volume_sector_count=1048576`) remain frozen. What was wrong for a replacement card was treating **total card capacity** as a compile-time constant tied to the retired 64 GB media:

| Constant (retired) | Value |
|---|---|
| `card_sector_count` | `121,503,744` |
| `capacity_bytes` | `62,209,916,928` |

A supported **32 GB SDHC** reports a different CSD capacity (typically on the order of **62,586,880** sectors — **must be measured**, not assumed from Disk Utility). Host and firmware currently **reject** any other GEOMETRY, and H38 `prepare` still pins `old_mbr_sha256` to the **old** H37 epoch. This plan is the implementation and execution gate **before** any card mutation.

Historical 64 GB evidence stays in the repo as **failed / superseded** context; it is not authority for the new card.

## Frozen vs configurable (after this work)

| Field | Policy |
|---|---|
| `volume_start_lba`, `volume_sector_count`, format/io caps, record cardinalities | **Frozen** per H38 v1 |
| `card_sector_count`, `capacity_bytes`, `old_mbr_sha256` | **Per epoch**, from **live H37** (and H35 identity for BIND) |
| `h35_reference_epoch`, `private_cid_sha256` | **Per card lineage** — new 32 GB card → **new H35 epoch** and digest in intent |
| MBR layout bytes written in `LAYOUT` | **Frozen canonical H38 MBR** (one type `0x0C` entry at 32768, count 1048576) — independent of whether the card shipped with a whole-card FAT32 MBR at different LBAs |

**Pre-write validation:** `volume_start_lba + volume_sector_count ≤ card_sector_count` and `card_sector_count` within published SENSOR spec (≤ 32 GB class). If the live whole-card MBR already uses LBAs that overlap the test volume, `LAYOUT` still replaces **only** LBA 0 with the reviewed layout; sectors outside `[32768, 1081344)` are not formatted by H38.

## Software work (Phase 0 — no device writes)

Complete before H37/H38 hardware. Re-run host suites to exit 0 after each milestone.

### 0.1 H37 — measured geometry

| Area | Change |
|---|---|
| `firmware/demos/h37_sdmmc_classification.c` | Remove `EXPECTED_SECTORS=121503744`; emit **measured** `GEOMETRY`; reject only unsupported cards (non-SD mem, MMC, SDIO, sector size ≠ 512, capacity below volume end, above SDHC policy max). |
| `scripts/h37_attached_classification.py` | Remove module-level `SECTORS=121_503_744`; validate GEOMETRY against **this run’s** captured values; persist `card_sector_count` in private run metadata for H38. |
| `scripts/h37_sd_metadata.py` | Replace `EXPECTED_CARD_GEOMETRY` with **replay from capture** or bounds checks; synthetic fixtures use a **reference 32 GB-class** example sector count, not 64 GB. |

### 0.2 H38 — intent-driven card size

| Area | Change |
|---|---|
| `scripts/h38_sd_contract.py` | `validate_intent()`: `card_sector_count` is **any** integer in allowed range with `volume` contained; remove hard-coded `121_503_744`. Keep `VOLUME_*` frozen. Update synthetic success transcripts to use a **documented reference** 32 GB sector count (constant `REFERENCE_SDHC32_SECTORS` for tests only). |
| `scripts/h38_attached_filesystem.py` | `_intent()`: set `card_sector_count` from **bound H37 run** metadata, not `contract.CARD_SECTORS`. **`current_h37_mbr()`**: bind to **`h37_run_dir` epoch** from `run-private.json`, not `H37_CURRENT_EPOCH`. Live GEOMETRY check: expect intent’s `card_sector_count` and derived `capacity_bytes`. |
| `firmware/common/h38_disk_guard.h` (and `.c`) | `H38_GUARD_CARD_SECTORS` from **build** (see below). |
| `firmware/common/h38_filesystem_io.c` | Same for `H38_IO_CARD_SECTORS`. |
| `firmware/demos/h38_sdmmc_filesystem.c` | Compare CSD capacity to **compiled** `H38_CARD_SECTOR_COUNT`; emit matching `GEOMETRY`. |
| Build | Extend private `h38-intent-private.h` (or CMake `-D`) with `H38_CARD_SECTOR_COUNT` matching intent `card_sector_count`. |

### 0.3 H35 identity chain (new card)

Operator chose **fresh H35** on the 32 GB card.

| Area | Change |
|---|---|
| `scripts/h38_sd_contract.py` | `validate_intent()`: accept `h35_reference_epoch` from prepare (not only the historical `a1617be8…` constant). |
| `scripts/h37_attached_classification.py` / `h38_attached_filesystem.py` | `read_h35_identity()` (or successor): read from **operator-selected** private H35 capture path recorded in run metadata; geometry check uses **that capture’s** measured sectors, not 64 GB. |
| `prepare` | Record `h35_capture_dir` + `h35_reference_epoch` in `run-private.json`; intent `private_cid_sha256` must match new H35 digest. |

**Deliverable:** commit geometry + identity binding; update [`h38-bounded-sd-filesystem.md`](h38-bounded-sd-filesystem.md) intent schema paragraph to say `card_sector_count` is measured and `h35_reference_epoch` is per lineage.

### 0.4 Host regression gates

```bash
python scripts/h38_attached_filesystem.py preflight
python scripts/h38_controller_checks.py
python scripts/h38_sd_contract_checks.py
python scripts/h38_disk_guard_checks.py
python scripts/h38_fatfs_format_checks.py
python scripts/h38_io_marker_checks.py
python scripts/h37_sd_metadata_checks.py
```

Independent source/linked-image review gates are **waived** for this operator track; `validate` + host contract checks remain required before `run`.

## Hardware execution (after Phase 0)

Private directories **outside the repo**, mode `0700`. Exactly one `/dev/cu.usbmodem*`.

### Phase 1 — H35 on 32 GB (read-only discovery)

1. New private epoch under `~/family-link-storage-experiments/` (default run dir `h35-YYYYMMDD` if `--run-dir` omitted). `--backup-dir` defaults to the archived BOX backup under the same root.
2. `scripts/h35_attached_discovery.py` flow per [`attached-storage-qualification.md`](attached-storage-qualification.md) — **detected** terminal, CID/geometry in private capture.
3. Select that capture for H37/H38 (any one):
   - `parse … --set-current` → writes `~/family-link-storage-experiments/current-h35-capture.json`;
   - `export FAMILY_LINK_H35_CAPTURE_DIR=…`;
   - `--h35-capture-dir` on `h37_attached_classification.py prepare` or `h38_attached_filesystem.py prepare`.

**Pass:** SDMMC detected, identity digest recorded, geometry within ≤32 GB class.

### Phase 2 — H37 on 32 GB (read-only classification)

1. `prepare` → `build` → `validate` → flash-capture → **restore** → parse → host-checks.
2. **Pass:** `read_complete`, `media_writes=0`, sanitized `summary.json` with **new** `sectors` / `capacity_bytes` / partition bounds, BOX `restored: verified`.
3. Commit `docs/evidence/attached-storage-qualification/h37-<date>/`.

### Phase 3 — H38 prepare / build / validate

```bash
python scripts/h38_attached_filesystem.py prepare \
  --run-dir <private> --backup-dir <private> --h37-run-dir <new-h37>
```

- Intent: `old_mbr_sha256` from H37 LBA 0 snapshot; `card_sector_count` from H37 GEOMETRY.
- `build` → `validate` → linked map review (no stock SDMMC disk, no `sync()`, stack OK).

### Phase 4 — H38 run + restore

```bash
python scripts/h38_attached_filesystem.py run \
  --port <cu.usbmodem*> --run-dir <private> --backup-dir <private>
```

Use `--use-existing-restore-image` only if backup dir contains a **verified** full image whose fingerprint matches the connected BOX. Operator reported **h31** on device: either capture **h31** as the restore baseline in private backup before `run`, or use an existing verified image that matches — orchestrator will stop if binding fails.

**Firmware pass:** single `COMPLETE` with `result=io_complete` per H38 plan § parser gates.

**Host pass:** `RESTORE_RESULT` readback + boot check (reject real panic/watchdog; accept pinned IDF label).

### Phase 5 — Evidence

- Public: `docs/evidence/attached-storage-qualification/h38-<date>/` README + `summary.json`.
- Update H38 plan status: **Stage B bounded I/O passed** or **failed** (not full attached qualification).

## Handoff statement (required after run)

- Bounded **H38 v1** passed or failed (stage).
- Cadence / fault / near-full / removal **not done**.
- Durable-outbox on attached storage **still blocked** until Stages C–D (cadence, fault, near-full, etc.) and product integration; bounded H38 v1 **passed** on this 32 GB card. On-chip H32 remains unqualified.
- BOX restored to **h31** or agreed baseline; user-normal operation.

## Stop conditions

- Multiple USB serial devices or wrong perch (dock USB).
- Phase 0 host gates not exit 0.
- H35/H37 not complete for **this** card, or intent geometry ≠ H37 capture.
- BIND identity / MBR / manifest mismatch.
- Any failed `COMPLETE`, missing terminal, or restore/boot failure.
- Whole-card format, exFAT enablement, or `firmware/v1/` changes without review.

## Open items / operator follow-ups

| Item | Notes |
|---|---|
| **Exact `card_sector_count`** | Determined only by Phase 2 H37 `GEOMETRY` — do not hard-code before measurement. |
| **h31 restore baseline** | Fail-closed scripts require a **16 MiB + NVS** private backup matching the device. Operator waived “extra” backup; first `prepare`/`run` should still **record** h31 (or confirm an existing private image) so restore returns to h31, not wake-word stock. |
| **Prior H38 LAYOUT on this card** | If none, omit `--prior-h38-run-dir`. If a partial 64 GB-era layout existed on a **different** card, irrelevant. |
| **Plan doc cross-links** | Done — `h38-bounded-sd-filesystem.md` status and evidence index reflect the 2026-10-08 pass. |

## Estimated effort

| Phase | Wall clock (indicative) |
|---|---|
| Phase 0 software | 1–2 focused sessions (contract, firmware defines, tests) |
| H35 + H37 | ~30–60 min each including restore |
| H38 run + restore | up to ~90 min one epoch |
