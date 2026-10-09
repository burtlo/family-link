# ADR-003: Storage qualification status (H32, H38, product SD)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-10-08 |
| **Labels** | **Observed** (qual runs); **Specification** (do-not-over-claim boundaries) |

## Context

Local durability on the BOX (on-chip flash partition vs SENSOR microSD) was explored through isolated qualification demos **H32** (on-chip FAT outbox) and **H35 → H37 → H38** (attached SDMMC). Product v1 can run **online without qualified device storage** while server archive work proceeds ([`plans/product-no-storage-roadmap.md`](../plans/product-no-storage-roadmap.md)).

Agents must not conflate: (a) server disk archive, (b) H38 lab profile pass, (c) shippable product outbox on SD or flash.

## Decision

### On-chip H32 — **unqualified** (**Observed**)

- Epochs halted at PCM cadence (`stage=pcm,reason=cadence`); Opus-sized 2 s cadence met deadlines; PCM stress at 67,200 B/s did not (e.g. 79/90 and 44/90 missed deadlines in reported runs).
- Phases 5–6 (interruption recovery, near-full) were not reached.
- **Do not** cite H32 as approval for production on-chip outbox until a future pass completes the master plan.

Evidence: [`evidence/onchip-storage-qualification/README.md`](../evidence/onchip-storage-qualification/README.md).

### Attached H38 Stage B — **pass** on bound 32 GB SDHC (**Observed**)

- Profile: **`sdmmc_bounded_fat32_v1`** — frozen 512 MiB FAT32 window, fixed I/O matrix, mandatory full 16 MiB + NVS BOX restore after epoch.
- **Pass** public records:
  - Mazi card track: [`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/README.md)
  - Lynn BOX (`B0:48`): [`h38-lynn-20261008`](../evidence/attached-storage-qualification/h38-lynn-20261008/README.md)
- H37 Stage A and H35 discovery on the same 32 GB track are prerequisites per [`ATTACHED-STORAGE.md`](../hardware/ATTACHED-STORAGE.md).

**Not established by H38 Stage B pass:**

- H32-style PCM cadence on SD (Stage C — planned, not run)
- Fault matrix, power-loss, card removal (Stage D)
- Near-full filesystem behavior
- Production partition choice or **x02** integration (USB MSC / SD mount **absent** in product shell — **Observed**)
- Arbitrary 16/32 GB family without per-card H35→H37→H38 chain

Index: [`evidence/attached-storage-qualification/README.md`](../evidence/attached-storage-qualification/README.md).

### Removable SD — **not required for v1 product merge** (**Specification** + roadmap)

- **GO** for online async audio development without qualified BOX storage; accepted temporary loss of unaccepted outgoing media on reset/power before server acceptance ([`plans/product-without-removable-storage-assessment.md`](../plans/product-without-removable-storage-assessment.md), [`plans/product-no-storage-roadmap.md`](../plans/product-no-storage-roadmap.md)).
- Server-side durable archive and PCM chunk APIs are **host-tested** separately — [`evidence/product-no-storage/03-server-recovery/README.md`](../evidence/product-no-storage/03-server-recovery/README.md).
- **Speculation:** Shipping durable device outbox will require a **future** qualified backend (on-chip retry after H32 fixes, or attached path after Stages C–D and product integration). No date committed here.

### As-built x02 device storage (**Observed**)

- No durable outbox; upload failure → lost clip in PSRAM ([`hardware/limitations.md`](../hardware/limitations.md)).
- Single-shot POST; ~10 s playback cap vs 180 s record setting — separate firmware gaps, not fixed by H38 lab pass.

## Consequences

- H38 qualification work must **not** modify `firmware/v1/` or x02 behavior; flash only the isolated H38 demo interval, then mandatory restore ([`AGENTS.md`](../AGENTS.md)).
- New card or BOX repeats full H35 → H37 → H38 per operator guide.
- Marketing or pilot docs must use the qualification boundaries verbatim — see [`hardware/limitations.md`](../hardware/limitations.md) § Qualification boundaries.

## Evidence and references

- [`ATTACHED-STORAGE.md`](../hardware/ATTACHED-STORAGE.md)
- [`plans/attached-storage-qualification.md`](../plans/attached-storage-qualification.md)
- [`plans/h38-bounded-sd-filesystem.md`](../plans/h38-bounded-sd-filesystem.md)
- [`hardware/storage-summary.md`](../hardware/storage-summary.md)
