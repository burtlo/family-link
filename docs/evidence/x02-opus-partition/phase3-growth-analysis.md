# Phase 3 — Growth attribution (X02 → Opus size probe)

Captured **2026-10-04** on commit `9788f0130fdc9826503f48f39eba032db6be6593` (same builds as [phase1-summary.md](phase1-summary.md) and [phase2-summary.md](phase2-summary.md)).

## Summary numbers

| Metric | X02 baseline | X02 + Opus probe | Δ |
|--------|-------------:|-----------------:|--:|
| App bytes (`wc -c`) | 1,484,272 | 1,665,808 | **+181,536** (~177 KiB) |
| Flash `.text` | 1,101,922 | 1,260,786 | +158,864 |
| Flash `.rodata` | 253,684 | 276,356 | +22,672 |
| DRAM (used) | 232,431 | 232,431 | 0 |
| IRAM (used) | 16,383 | 16,383 | 0 |

Negative-control **x02-opus-dep** added **0** bytes vs X02 (Opus stripped at link). All growth below is from live encode/decode references, not `PRIV_REQUIRES` alone.

## Component delta (archive `flash_total`)

Sorted by contribution to the **+181,536** byte app growth. KiB = bytes ÷ 1024.

| Archive / bucket | Δ bytes | Δ KiB | Notes |
|------------------|--------:|------:|-------|
| **`lib78__esp-opus.a`** | +179,234 | **+175.0** | Full retained Opus encoder + decoder (CELT + SILK + tables) |
| **`libopus.a` (`fl_opus.c`)** | +656 | **+0.6** | Product wrapper; new component archive in probe only |
| **`libmain.a` (net)** | +314 | **+0.3** | Probe TU + tiny X02 object layout shifts (see files) |
| `libmbedcrypto.a` | +296 | +0.3 | Link order / reachable symbol ripple (SHA/AES already in X02) |
| `libesp_app_format.a` | +164 | +0.2 | App descriptor / rodata packing |
| `liblwip.a` | +212 | +0.2 | Minor object deltas in TCP/IP |
| Other IDF archives | ±≤112 each | &lt;0.1 each | Wi-Fi, BSP, websocket, GPIO, etc. |
| **Sum of archive Δ** | ~+181,060 | ~+177.0 | ~476 B gap vs app Δ (app descriptor / padding) |

### Named buckets (probe growth)

| Bucket | Δ KiB | % of +181,536 |
|--------|------:|--------------:|
| Opus library (`78__esp-opus`) | **175.0** | **98.7%** |
| Wrapper (`fl_opus.c` in `libopus.a`) | **0.6** | **0.4%** |
| Probe-only (`fl_opus_size_probe.c`) | **0.1** | **0.1%** |
| Linker / IDF noise (all other archives) | **~1.6** | **~0.9%** |
| **Attributed (named + noise)** | **~177.3** | **~100%** |

**Acceptance:** &gt;90% of material growth is named; **~99.1%** is Opus library + wrapper + probe TU.

## Object-file highlights (`size-files` / per-object `flash_total`)

Largest **new** contributions (probe minus X02), all from retained Opus unless noted:

| Δ bytes | Object |
|--------:|--------|
| +18,516 | `lib78__esp-opus.a:celt_encoder.c.obj` |
| +13,557 | `lib78__esp-opus.a:bands.c.obj` |
| +13,474 | `lib78__esp-opus.a:opus_encoder.c.obj` |
| +9,065 | `lib78__esp-opus.a:modes.c.obj` |
| +8,667 | `lib78__esp-opus.a:celt_decoder.c.obj` |
| +5,783 | `lib78__esp-opus.a:cwrs.c.obj` |
| +5,411 | `lib78__esp-opus.a:NSQ_del_dec.c.obj` |
| +656 | **`libopus.a:fl_opus.c.obj`** (wrapper) |
| +123 | **`libmain.a:fl_opus_size_probe.c.obj`** (probe TU) |

Encoder-side CELT/SILK objects dominate; decoder path is smaller but still linked because the probe calls `fl_opus_decode_frame` (including PLC with `NULL` packet).

X02 **does not** link `fl_opus.c`, `fl_opus_size_probe.c`, or `lib78__esp-opus.a` (confirmed by dep-only control and [phase2-map-audit.md](phase2-map-audit.md)).

## h31-style upload tasks **not** in the probe

The probe intentionally retains **codec only** — no chunk uploader, no Ogg page walk, no `index.json` client, no outbox.

Island evidence (archive `flash_total` on **h30** vs **h31**):

| Item | h30 | h31 | Δ (h31 − h30) |
|------|----:|----:|--------------:|
| `libmain.a` | 4,230 | 6,982 | **+2,752** (~2.7 KiB) |
| `lib78__esp-opus.a` | 178,414 | 178,854 | +440 (build noise / flags) |
| `libopus.a` | 636 | 636 | 0 |

Per-object, almost all incremental demo logic sits in one file:

| Δ bytes | Object |
|--------:|--------|
| +5,411 | `libmain.a:h31_opus_chunks.c.obj` |
| +1,011 | `libmain.a:wifi_sta.c.obj` (shared STA path; much of this overlaps X02’s larger `wifi_sta.c`) |

**Estimate for product (X02 base, not h30):** island chunk upload + HTTP status/recovery is **~6–15 KiB** flash when merged into existing `v1_*` HTTP/Wi-Fi (reuse TLS client, trim duplicate demo UI). The **h31 − X02** app delta (**+35,168** bytes) is **not** a valid upload-only estimate — h31 is a minimal island image without the X02 shell (~**−96 KiB** `libmain.a` vs X02).

## Large / demo-only code that should **not** ship in product X02

| Item | Action |
|------|--------|
| `firmware/common/fl_opus_size_probe.c`, `firmware/demos/x02_opus_size_probe.c`, `x02_opus_dep_only` | **Remove** after measurement; build-only artifacts |
| `x02-opus-probe` / `x02-opus-dep` flash targets | Keep only as CI/size regression hooks if desired |
| h31 island screens, standalone Wi-Fi UX, demo HTTP servers | **Do not** port wholesale; extract protocols only |
| Full **24 kbps AUDIO** Opus profile | Optional trim if flash budget requires (probe already touches `FL_OPUS_PROFILE_APP_24K` for link retention — product may ship VOIP-only first) |
| Entire Opus **stereo** / wideband table objects | Review `opus` component compile flags only if a later trim pass is authorized (not measured here) |
| Expanding carousel to **download full message** into `V1_PLAYBACK_BUF_CAP` | Architectural mismatch; streaming path replaces blob buffer ([LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md)) |

## RAM / IRAM / PSRAM / stacks (flash ≠ runtime)

Phase 2 probe shows **no** flash-attributed DRAM/IRAM growth vs X02, but that does **not** prove runtime safety.

| Concern | Evidence |
|---------|----------|
| **IRAM** | **16,383 / 16,384** bytes used on X02, probe, h30, and h31 — already at ceiling before Opus tasks |
| **Opus task stack** | h30 uses **`h30_audio` with 32 KiB stack**; running encode/decode on `app_main` caused stack overflow reboot ([opus-demo.md](../../plans/opus-demo.md)) |
| **PSRAM** | h30 places large stacks in PSRAM when available — product must follow same pattern for codec + upload workers |
| **Playback buffer** | Streaming design uses **32–64 KiB RAM** ring buffer ([STREAMING-PLAYBACK.md](../../STREAMING-PLAYBACK.md)), not app flash; replaces 320 KiB full-message download buffer over time |
| **Soak** | Formal **180 s** Opus soak and p95 timing remain open per [LONG-MESSAGE-ARCHITECTURE.md](../../LONG-MESSAGE-ARCHITECTURE.md) |

Flash growth quantified here is independent of those runtime budgets.

## Source artifacts

- Components: [x02-size-components.txt](x02-size-components.txt), [x02-opus-probe-size-components.txt](x02-opus-probe-size-components.txt)
- Files: [x02-size-files.txt](x02-size-files.txt), [x02-opus-probe-size-files.txt](x02-opus-probe-size-files.txt)
- h31 reference: [h31-size-components.txt](h31-size-components.txt), [h30-size-components.txt](h30-size-components.txt)
