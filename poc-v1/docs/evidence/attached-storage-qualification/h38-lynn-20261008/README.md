# H38 bounded FAT32 — Lynn BOX / 32 GB SDHC (2026-10-08)

Public record for a **completed** bounded I/O qualification epoch on Lynn's storage-prep BOX-3 (USB serial ending `B0:48`) with the bound 32 GB card after H37 epoch `ae4f2867…`. Private captures and digests remain under `~/family-link-storage-experiments/`.

## Outcome

**Pass.** Firmware terminal `COMPLETE` with `result=io_complete`, host `run-result-private.json` `status=verified`, `restore_verified=true`, and mandatory BOX restore proof `verified` after the epoch.

Qualifying private epoch: `4d9b4346164e14f2add083c0c7aa0654` (`h38-lynn-20261008-attempt3`).

## What was demonstrated

- H37-bound MBR and H35 identity chain for this hardware (`h37-boxb-20261008`, `h35-boxb-20261008`).
- Bounded FAT32 format, full IO matrix (40 files), remount/probe/reclaim fixtures, and `FINISH` within profile limits (`media_writes=2074` reported).
- Parser-validated capture (`BIND` → `LAYOUT` → `FORMAT` → `IO` → `FINISH`).
- Restored original `ota_0` application boot markers without panic after full 16 MiB + NVS readback.

## Host note (this hardware)

Lynn's preserved BOX backup uses a single `ota_0` app slot at `0x20000` (not legacy `factory` at `0x10000`). Qualification required aligning H38 validate, experiment flash offset, and restored-boot checks with `qual_app_partition` (commit on `main` 2026-10-08).

## Prior incomplete attempts (same card, not qualifying)

- `h38-lynn-20261008` — validate blocked on pre-fix factory-only headroom check (archived as `h38-lynn-20261008-validate-blocked-ota`).
- `h38-lynn-20261008` (epoch `c1a987ea…`) — `run` stopped at `master_preflash` during fresh current full-image backup (read timeout under clock budget).
- `h38-lynn-20261008-attempt2` — partial capture; BOX manually restored before retry.

## Scope unchanged

Does not establish H32 cadence, fault matrix, near-full card, or production outbox behavior.
