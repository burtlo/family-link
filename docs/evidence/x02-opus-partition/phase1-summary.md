# Phase 1 — Reproducible baselines (x02 / h30 / h31)

Captured **2026-10-04** on commit `9788f0130fdc9826503f48f39eba032db6be6593` (see [`toolchain.txt`](toolchain.txt)).

## Build results

| Demo | Build dir | Build | Partition check |
|------|-----------|-------|-----------------|
| x02 | `firmware/build/x02_product_shell` | OK | Warning: 3% app partition free |
| h30 | `firmware/build/h30_opus_local` | OK | OK (42% free) |
| h31 | `firmware/build/h31_opus_chunks` | OK | Warning: 1% app partition free |

No image exceeded the factory app partition. **X02 baseline and h31 are partition headroom blockers for future growth** (3% and 1% margin), not overflow failures.

## Partition (all three builds — identical table)

Decoded from `partition_table/partition-table.bin` → [`partition-tables.txt`](partition-tables.txt).

| Name | Type | Offset | Size (bytes) | Size (hex) |
|------|------|--------|-------------:|-----------:|
| nvs | data | 0x9000 | 24 KiB | — |
| phy_init | data | 0xf000 | 4 KiB | — |
| **factory (app)** | **app** | **0x10000** | **1,536,000** | **0x177000** |

App partition size = ESP-IDF `SINGLE_APP_LARGE` **1500 KiB** factory slot.

## Measurement record (canonical table)

App bytes = `wc -c` on `family_link_demo.bin` (flashed application image, unstamped).  
Free bytes = app partition − app bytes. Free % = free / app partition.  
Flash text / rodata = ESP-IDF `idf.py size` Flash Code `.text` and Flash Data `.rodata`.  
DRAM / IRAM = `idf.py size` DIRAM and IRAM **Used [bytes]** totals.

| Build | App bytes | App partition bytes | Free bytes | Free % | Flash text | Flash rodata | DRAM | IRAM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| X02 baseline | 1,484,272 | 1,536,000 | 51,728 | 3.4% | 1,101,922 | 253,684 | 232,431 | 16,383 |
| h30 Opus local | 888,864 | 1,536,000 | 647,136 | 42.1% | 662,922 | 133,376 | 147,475 | 16,383 |
| h31 Opus chunks | 1,519,440 | 1,536,000 | 16,560 | 1.1% | 1,180,374 | 210,428 | 196,799 | 16,383 |

Hex cross-check (ESP-IDF `check_sizes.py` at link): x02 `0x16a5f0`, h30 `0xd9020`, h31 `0x172f50` — matches `wc -c`.

## Linker maps

| Demo | Map path |
|------|----------|
| x02 | `firmware/build/x02_product_shell/family_link_demo.map` |
| h30 | `firmware/build/h30_opus_local/family_link_demo.map` |
| h31 | `firmware/build/h31_opus_chunks/family_link_demo.map` |

## Saved size reports

| Demo | size | size-components | size-files |
|------|------|-----------------|------------|
| x02 | [`x02-size.txt`](x02-size.txt) | [`x02-size-components.txt`](x02-size-components.txt) | [`x02-size-files.txt`](x02-size-files.txt) |
| h30 | [`h30-size.txt`](h30-size.txt) | [`h30-size-components.txt`](h30-size-components.txt) | [`h30-size-files.txt`](h30-size-files.txt) |
| h31 | [`h31-size.txt`](h31-size.txt) | [`h31-size-components.txt`](h31-size-components.txt) | [`h31-size-files.txt`](h31-size-files.txt) |

Note: `idf.py size` re-ran ninja and appended the memory summary after build log noise; the table rows above are parsed from the **Memory Type Usage Summary** at the end of each `*-size.txt`.

## WHO stamp / `.who.bin`

Phase 1 used `--build-only` (`make build-firmware`). Identity stamping runs only on flash (`scripts/flash.py` → `stamp_who`); baseline evidence is the unstamped `family_link_demo.bin`. Stamped size not measured here.

## Discrepancies vs [`opus-demo.md`](../../plans/opus-demo.md)

| Claim (opus-demo Phase 1 as-built) | Phase 1 evidence | Notes |
|-----------------------------------|------------------|-------|
| h30 app **~888 KiB** | **888,864** bytes (`0xd9020`) | Aligns with rounded decimal KB (~889 KB) or informal “888” from hex size; binary KiB ≈ **867 KiB**. |
| h31 app **~1.73 MiB** | **1,519,440** bytes (`0x172f50`) ≈ **1.45 MiB** (1024²) | **~214 KiB smaller** than the doc figure. Provenance: opus-demo desk snapshot 2026-10-04 without committed size artifacts; this run is fullclean + ESP-IDF v5.4.2 at `9788f013`. Retain both; do not silently rewrite opus-demo history. |
| h30 vs h25 **+~181 KiB** codec growth | h30 888,864 B; h25 not rebuilt in Phase 1 | Delta vs h25 still plausible; re-measure h25 in Phase 3 if needed for exact delta. |

## Observations for later phases

- **X02 product shell alone** uses **~96.6%** of the `SINGLE_APP_LARGE` factory slot — comparable to h31 ( **~99.0%** ) and far above h30 (**~57.9%**).
- Merging Opus into X02 without a partition or feature-size change is very likely to **overflow** the current 1500 KiB app slot (Phase 2 probe expected to quantify).
- IRAM at **16383 / 16384** bytes (99.99% used) on all three builds — shared constraint, not Opus-specific in this snapshot.

## Artifact index

```
docs/evidence/x02-opus-partition/
  toolchain.txt
  partition-tables.txt
  phase1-summary.md
  x02-size.txt  x02-size-components.txt  x02-size-files.txt
  h30-size.txt  h30-size-components.txt  h30-size-files.txt
  h31-size.txt  h31-size-components.txt  h31-size-files.txt
```
