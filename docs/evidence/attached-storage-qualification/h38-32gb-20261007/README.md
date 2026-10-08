# H38 bounded FAT32 — 32 GB SDHC track (2026-10-07)

Public record for the first hardware mutation epoch on the bound 32 GB card after H37 epoch `a8a144e6ccaddf20809ff2dc7faf838c`. This does **not** establish bounded I/O qualification, cadence, fault, near-full, or outbox behavior.

## Outcome

**Failed / incomplete.** Firmware reached `LAYOUT` and `FORMAT` with a validated initial mount, then failed at `IO` entry on the first epoch (`5374073963356b0568c4fb2124b9a7df`) due to a stale hardcoded H35 reference in `h38_filesystem_io.c` (fixed in commit `c92e63a`). Subsequent epochs on the same card could not complete `BIND` until the card MBR baseline is rebound (post-layout state) or a fresh H37 read-only capture is taken.

## What was demonstrated (partial)

- Host preflight, contract, and controller checks passed before mutation.
- One authorized 512-byte MBR layout write (`mbr_write_count=1`).
- Bounded FAT32 `f_mkfs` and initial mount within profile limits on the 512 MiB virtual volume.
- Mandatory BOX restore was attempted after the failed capture; private restore proof records readback and original-application boot markers, but the host run remained `failed_or_incomplete` because the IO phase hit the 900 s wall deadline while recovery was still in flight.

## What was not demonstrated

- Single firmware `COMPLETE` with `result=io_complete`.
- Parser cardinalities for the full matrix, remount cycles, directory/reclaim fixtures, and `FINISH`.
- Verified host `run-result-private.json` with `restore_verified=true` on the qualifying epoch.

## Repository fixes landed during this attempt

- `scripts/h38_attached_filesystem.py`: bind H38 prepare to per-card H37 read-plan sectors and H35 geometry (32 GB partition-boot LBA `8192`, not the retired `32768` constant).
- `firmware/common/h38_filesystem_io.c`: bind IO context validation to compiled `H35_REFERENCE_EPOCH` instead of a retired hardcoded digest.

## Private run directories (operator)

Immutable private epochs under `~/family-link-storage-experiments/` include `h38-32gb-20261007` (partial hardware), `h38-32gb-20261007b`–`d` (host/port or BIND failures). Do not publish private captures, digests, or sector dumps.

## Next gate

Superseded for qualification: a passing epoch is recorded in [`h38-32gb-20261008/README.md`](../h38-32gb-20261008/README.md) (`d953a7fbfd1f9054aacff6f6c79f8ae1`). Retain this folder as the partial-attempt history only.
