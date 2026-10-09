# H38 bounded FAT32 — 32 GB SDHC track (2026-10-08)

Public record for a **completed** bounded I/O qualification epoch on the bound 32 GB card after H37 epoch `a8a144e6ccaddf20809ff2dc7faf838c`. Private captures and digests remain under `~/family-link-storage-experiments/`.

## Outcome

**Pass.** Firmware terminal `COMPLETE` with `result=io_complete`, host `run-result-private.json` `status=verified`, `restore_verified=true`, and mandatory BOX restore proof `verified` after the epoch.

Qualifying private epoch: `d953a7fbfd1f9054aacff6f6c79f8ae1` (`h38-32gb-20261008d`).

## What was demonstrated

- Path A MBR binding via prior successful `LAYOUT` reference (`h38-32gb-20261008b` canonical MBR digest `f99b0615…`).
- Bounded FAT32 format, full IO matrix (40 files), five remount cycles, retained/probe/reclaim fixtures, and `FINISH` within profile limits.
- Parser-validated capture and dispatch ledger (`BIND` → `LAYOUT` → `FORMAT` → `IO` → `FINISH`).
- Restored original application boot markers without panic after full 16 MiB + NVS readback.

## Fixes required before this pass (on `main`)

- `h38_filesystem_io.c`: `esp_vfs_fat_info` on FAT mount base (not epoch subdirectory).
- `h38_sd_contract.py`: failed-capture identity check uses intent `h35_reference_epoch`.
- `h38_sdmmc_filesystem.c`: refresh USB idle clock before reading `FINISH` after long IO.

## Prior partial attempts (same card, not qualifying)

See `h38-32gb-20261007/` and private dirs `h38-32gb-20261007`–`20261008c` for incomplete epochs (IO semantics, host deadline, BIND/MBR rebind, USB idle timeout).

## Scope unchanged

Does not establish H32 cadence, fault matrix, near-full card, or production outbox behavior.
