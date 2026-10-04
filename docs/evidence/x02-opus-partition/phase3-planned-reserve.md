# Phase 3 — Planned flash reserve (not in size probe)

Captured **2026-10-04** alongside [phase3-growth-analysis.md](phase3-growth-analysis.md).

The **X02 + Opus size probe** links the full v1 shell and retained codec paths only. It does **not** include durable outbox, chunk recovery, streaming playback, FLSK2 timeline, or production logging. This document gives explicit **flash** reserve ranges for that missing work.

**Headroom policy note (2026-10-04 correction):** The **262,144 B** threshold below applies to the **current 1,536,000 B** factory slot. For a **2,228,224 B** (2.125 MiB) candidate slot, required margin is **334,234 B** — see [phase4-corrections.md](phase4-corrections.md) and `scripts/opus_partition_model.py`.

References: [durable-outbox-demo.md](../../plans/durable-outbox-demo.md), [LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md), [STREAMING-PLAYBACK.md](../../STREAMING-PLAYBACK.md), [MESSAGE-PROTOCOL.md](../../MESSAGE-PROTOCOL.md).

## Method

- **Low** = reuse existing X02 modules (`v1_record`, `v1_api`, `v1_connect`, mbedtls) with minimal new code.
- **High** = backend-neutral outbox interface, both PCM and Opus code paths, richer diagnostics, and carousel-integrated playback state machine.
- Ranges are **flash text + rodata** only (same basis as probe measurements).
- **Not included:** Opus library **~175 KiB** (already in probe), partition metadata, or removable-media capacity (outbox bytes live on attached storage per architecture).

## Reserve table

| Planned work | Low (KiB) | High (KiB) | Rationale |
|--------------|----------:|-----------:|-----------|
| **Durable outbox + FS driver** | 24 | 48 | Queue/manifest JSON, atomic rename helpers, SHA-256 over chunks (mbedtls already linked), mount abstraction from [durable-outbox-demo.md](../../plans/durable-outbox-demo.md). Add **FAT/VFS/SD** pieces if not already in X02 link — **not** in baseline X02 archive set. On-chip SPIFFS/WL is an alternative with similar driver cost. |
| **Chunk upload / recovery state machine** | 12 | 28 | h31 island demo **~5.4 KiB** in `h31_opus_chunks.c` plus protocol client; X02 merge with existing HTTP client and message IDs is larger than the island but shares Wi-Fi/TLS. Covers create/status/complete, backoff, idempotent PUT, provisional ID association ([LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md) lifecycle). |
| **Bounded Ogg streaming + index client** | 18 | 42 | Lightweight Ogg **page** walk (no libogg per h31), HTTP range resume, `index.json` fetch/parse, playback SM (buffering/underrun/reconnect) per [STREAMING-PLAYBACK.md](../../STREAMING-PLAYBACK.md). Replaces full-blob carousel download — net flash may be **partially offset** by removing dead full-file paths later; reserve assumes **additive** until measured. |
| **FLSK2 timeline** | 8 | 20 | Sparse sketch chunks on audio clock, load/decode `.flsk2` segments during record/playback ([SKETCH-TIMELINE.md](../../SKETCH-TIMELINE.md)). |
| **Protocol compat (admin-independent)** | 10 | 24 | PCM 16 kHz fallback decode path, capability negotiation, dual codec parameters in manifests — required interop baseline in [LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md). |
| **Logging / recovery diagnostics** | 8 | 16 | Serial tags for chunk seq/checksum/ack, outbox recovery reasons, visible “storage full / pending upload” strings — bounded; not a full debug console. |
| **Total planned reserve** | **80** | **178** | Sum of rows |

**Midpoint planning figure:** **~125 KiB** flash for long-message integration **after** Opus is linked.

### Cross-checks from architecture docs

| Doc statement | Reserve implication |
|---------------|---------------------|
| [STREAMING-PLAYBACK.md](../../STREAMING-PLAYBACK.md) ~**180 KiB** Opus firmware growth | Matches probe; **not** double-counted above |
| 32–64 KiB playback ring | **RAM**, not in table |
| [durable-outbox-demo.md](../../plans/durable-outbox-demo.md) island scope | Flash for driver + queue; message **bytes** on removable media |
| [LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md) “partition budget” still open | This table supplies the missing integration slice for Phase 4 partition modeling |

## Headroom policy (experiment)

From [x02-opus-partition-feasibility.md](../../plans/x02-opus-partition-feasibility.md):

**Reserve threshold** = `max(256 KiB, 15% × app_partition)`  
With `SINGLE_APP_LARGE` factory slot **1,536,000** bytes:

- 15% = **230,400** bytes  
- **Threshold = 262,144 bytes (256 KiB)**

### Apply to measured images

| Image | App bytes | Free bytes in current slot | vs current-slot threshold | Verdict |
|-------|----------:|---------------------------:|---------------------------|---------|
| X02 baseline | 1,484,272 | +51,728 (3.4%) | Below threshold | **Marginal** (already before Opus) |
| X02 + Opus probe | 1,665,808 | **−129,808** | Overflow | **Hard failure** |
| Probe + planned reserve (low) | ~1,746,000 | — | — | **Hard failure** on 1.5 MiB slot |
| Probe + planned reserve (high) | ~1,838,000 | — | — | **Hard failure** |

**Candidate fit in the current slot** (fits with its **262,144 B** policy margin before counting planned features): **Neither** X02 nor probe qualifies.

**Product-ready fit** applies the policy to the candidate slot itself:

```text
free = slot - (probe_app + planned_reserve)
required_margin(slot) = max(262,144, ceil(0.15 * slot))
require free >= required_margin(slot)
```

The 15% branch dominates for all three reserve cases. The reproducible model gives:

| Reserve | Projected image | Exact minimum slot | Exact minimum (MiB) | Rounded up to ESP-IDF's 4 KiB app-size alignment |
|---------|----------------:|-------------------:|--------------------:|-----------------------------------------:|
| Low (80 KiB) | 1,747,728 B | 2,056,151 B | 1.960898 MiB | `0x1F6000` = 2,056,192 B (1.960938 MiB) |
| Mid (125 KiB) | 1,793,808 B | 2,110,363 B | 2.012599 MiB | `0x204000` = 2,113,536 B (2.015625 MiB) |
| High (178 KiB) | 1,848,080 B | 2,174,212 B | 2.073490 MiB | `0x213000` = 2,174,976 B (2.074219 MiB) |

The proposed **2.125 MiB** slot covers the high reserve case and leaves **45,910 B** beyond its applicable **334,234 B** policy margin. ESP-IDF separately requires application **offsets** on 64 KiB boundaries; the candidate layouts use 64 KiB-multiple slot sizes so consecutive OTA slots stay aligned without gaps. These are modeled code-reserve requirements, not measured final product image sizes.

### Decision implication (Phase 4 input)

- **Partition enlargement** and/or **feature trimming** is required; linking Opus alone overflows by **~127 KiB**.
- X02 baseline was already **marginal** on `SINGLE_APP_LARGE`; Opus does not create the only constraint.
- Product-ready on unchanged partition is **not** supported by these numbers.

## RAM note (policy companion)

Reserve threshold applies to **flash app partition** only. Codec task **32 KiB** stacks, streaming ring buffers, and LVGL heap remain separate go/no-go gates ([phase3-growth-analysis.md](phase3-growth-analysis.md)).
