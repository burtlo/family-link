# Storage — on-chip, removable, and product rules

**See also:** [`STORAGE.md`](STORAGE.md), [`ATTACHED-STORAGE.md`](ATTACHED-STORAGE.md), [`plans/onchip-storage-qualification.md`](../plans/onchip-storage-qualification.md), [`plans/attached-storage-qualification.md`](../plans/attached-storage-qualification.md).

**Operator procedures** (H35 → H37 → H38, BIND/LAYOUT, restore gates): use [`ATTACHED-STORAGE.md`](ATTACHED-STORAGE.md) — not duplicated here.

## Product rule

| Label | Statement |
|---|---|
| **Inferred** (architecture) | **Canonical inbox and playhead live on the server.** NVS holds Wi‑Fi + tokens. |
| **Observed** (x02 as-built) | Failed upload → **clip discarded** (PSRAM only); **no retry queue** on flash or SD. |
| **Specification** (intent) | **Product must not depend on removable SD (or USB stick) unless explicitly chosen** as the durable-outbox backend after qualification and integration. |

Removable media is for **outbox / logs / cache**, not replacing the server archive ([`STORAGE.md`](STORAGE.md)).

## On-chip flash (16 MiB module)

| Topic | Label | Detail |
|---|---|---|
| Today’s layout | **Observed** | **`SINGLE_APP_LARGE`**: app **~1.5 MiB**; **15,175,680 B** tail **unpartitioned** (not FAT). |
| X02 + Opus fit | **Measured** | Current slot too small; **2.125 MiB** app candidates validated in tools only ([`evidence/x02-opus-partition/phase4-corrections.md`](../evidence/x02-opus-partition/phase4-corrections.md)). |
| Modeled on-chip outbox (single-factory fixture) | **Inferred** | **~12.26 MiB** usable FAT after wear-leveling model (~**31.5** × 3‑min Opus-sized messages at planning size). |
| H32 qualification | **Observed** | Island demo `h32_onchip_storage` + partition `factory` @ `0x10000` **0x220000** + `outbox` FAT @ `0x230000`. **Opus** 2 s cadence: **0 misses**. **PCM** stress: **failed** (79/90 and 44/90 deadline misses). Phases 5–6 **not reached**. |
| H32 status | **Observed** | **Unqualified** — follow [`plans/h32-pcm-cadence-corrections.md`](../plans/h32-pcm-cadence-corrections.md). Write-coalescing correction **closed** at source gate (4 KiB cluster). |
| Production partition | **Unknown** | Single-factory vs dual-OTA **open** (recovery vs OTA rollback tradeoff). |

After power loss: **PSRAM empty**; anything not on server or durable local media is **lost**.

## PSRAM (16 MiB)

| Role | Label | Survives reboot? |
|---|---|---|
| Record buffer + upload copy | **Observed** (x02) | **No** |
| Inbound preview / hangout buffers | **Observed** | **No** |
| Practical outbox | **Specification** (intent) | **No** — not a durable queue today |

[`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md): **180 s** record is configured; **~12 MiB** identifiable allocations at upload — **marginal** until measured on hardware.

## Attached storage — SENSOR microSD

### Hardware

| Item | Label |
|---|---|
| Slot | **Specification** — **SENSOR** brick only (not BOX-3B main, not DOCK). |
| Bus | **Specification** — SDMMC **4-bit**, GPIO **9/11/12/13/14/42** (overlaps DOCK Pmod I2S map — **dock audio vs SENSOR storage** is a physical choice). |
| Supported target media | **Specification** + **Observed** — **16 or 32 GB microSDHC**, **512-byte** sectors; capacity from **card geometry**, not label. |
| Retired | **Observed** — **64 GB SDXC** (**121,503,744** sectors, exFAT) — **do not** reuse for new qual epochs. |

### Qualification status (H32 vs H38)

| Stage | What it proves | Status (32 GB bound card) |
|---|---|---|
| **H35** | Detection + identity + geometry | **Pass** (discovery only) |
| **H37** | Read-only MBR/FAT metadata, **zero writes** | **Pass** (`read_complete`) |
| **H38 v1** | Bounded **512 MiB** FAT32 @ LBAs `[32768,1081344)`, fixed I/O matrix, 5 remounts | **Pass** (`io_complete`) — profile **`sdmmc_bounded_fat32_v1`** |

**Observed passes (public evidence, 2026-10-08):**

- Mazi card lineage: [`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/)
- Lynn BOX (`B0:48`): [`h38-lynn-20261008`](../evidence/attached-storage-qualification/h38-lynn-20261008/)

**Not qualified:** H32 **cadence** on SD, fault matrix, near-full card, physical removal, arbitrary 16/32 GB family, **full-card** use, **x02 outbox**, production partition choice ([`evidence/attached-storage-qualification/README.md`](../evidence/attached-storage-qualification/README.md)).

**Audrey** unit: card present; **no** qual epochs yet (greenfield H35→H37→H38) — [`plans/three-box-usb-handoff-20261008.md`](../plans/three-box-usb-handoff-20261008.md).

### Post-H38 MBR

| Label | Detail |
|---|---|
| **Observed** | After H38 **`LAYOUT`**, card MBR **differs** from pre-H38 H37 snapshot. Retries need fresh H37 or `--prior-h38-run-dir` — see ATTACHED-STORAGE. |

## DOCK USB-A (mass storage)

| Label | Detail |
|---|---|
| **Specification** | USB **1.1 FS** host; FAT32 stick plausible. |
| **Observed** | **No** MSC qual, **no** product mount in tree. |
| **Inferred** | Shares **one** USB-A with UVC camera; hub splits **12 Mbps**. USB OTG vs Serial/JTAG PHY — host path needs external PHY or accepted debug risk ([`plans/attached-storage-qualification.md`](../plans/attached-storage-qualification.md)). |

## Exhaustion and capacity (planning)

| Scenario | Label | Order of magnitude |
|---|---|---|
| On-chip outbox (if partition lands) | **Inferred** | ~**35–40** × 10 s PCM clips, or ~**2** × 3 min PCM diaries, or **~30+** Opus-sized chunks ([`STORAGE.md`](STORAGE.md)) |
| 16 GB microSD | **Specification** | Vast for this write rate; **not** the bottleneck — **Wi‑Fi** and **qualification** are |
| Server `data/` | **Observed** | Desktop SSD; no special hardware |

## What works today for v1 tryout

| Path | Works without SD? |
|---|---|
| Record → POST → server inbox | **Yes** (if network OK) |
| Survive upload failure | **No** (any medium) |
| Qualified local durable queue | **No** (H32 failed; H38 is island-only) |
