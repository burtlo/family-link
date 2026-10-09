# Attached-storage qualification evidence

Canonical overview and operator boundaries: [`ATTACHED-STORAGE.md`](../../hardware/ATTACHED-STORAGE.md). Full stage definitions: [`attached-storage-qualification.md`](../../plans/attached-storage-qualification.md).

## 32 GB SDHC track (current authority)

| Stage | Folder | Verdict | Notes |
|---|---|---|---|
| Phase 0 geometry binding | (software, in tree) | **Landed** | Per [`h38-sdhc-geometry-qualification.md`](../../plans/h38-sdhc-geometry-qualification.md) |
| H35 discovery | [h35-32gb-20261007](h35-32gb-20261007/summary.json) | **Pass** (discovery only) | `detected`, 61,071,360 sectors; not backend qualification |
| H37 classification | [h37-32gb-20261007](h37-32gb-20261007/README.md) | **Pass** (Stage A) | `read_complete`, zero media writes; FAT32 signature at metadata reads |
| H38 bounded I/O | [h38-32gb-20261008](h38-32gb-20261008/README.md) | **Pass** (Stage B profile) | `io_complete`, mandatory BOX restore verified — see [summary.json](h38-32gb-20261008/summary.json) |

**Qualified today:** frozen **H38 v1** bounded FAT32 layout/format/mount/I/O on the bound 32 GB card only. **Not qualified:** H32 cadence, fault matrix, near-full, physical removal, production partition choice, durable outbox.

### Partial / failed history (same track)

| Folder | Verdict |
|---|---|
| [h38-32gb-20261007](h38-32gb-20261007/README.md) | Failed/incomplete — LAYOUT+FORMAT OK; IO phase did not reach `io_complete` |
| [h38-stack-failure-20261007](h38-stack-failure-20261007/README.md), [h38-framing-failure-20261007](h38-framing-failure-20261007/README.md) | 64 GB–era hardware diagnostics (archival) |
| [h37-readonly-20261006](h37-readonly-20261006/README.md) | Retired **64 GB** card attempts (superseded by 32 GB H37 above) |

## Historical footnote — 2026-10-05 H35 (retired card lineage)

The first H35 attempt stopped with `host_capture_dependency_failure`; restoration/privacy review passed. The corrected retry detected SDMMC and passed strict allowlist and public-mapping review — discovery only.

- [Corrected preflight retry](h35-preflight-retry-20261005/summary.md) — discovery-only, unqualified.
- [H35 epoch summary](h35-20261005/summary.md) — build bounds, capture failure, verified BOX restoration.

Private raw captures, digests, and run locations stay outside the repository.

## Qualification boundary

H35 establishes detection/identity only. H37 Stage A establishes read-only metadata classification. H38 Stage B establishes the reviewed bounded filesystem/I/O profile when `io_complete` and restore gates pass. None of these alone establishes full attached-backend or product qualification without the later stages in the master plan.
