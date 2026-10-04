# Phase 4 — Partition strategy comparison

**2026-10-04** · commit `9788f013` · inputs: [phase1-summary.md](phase1-summary.md), [phase2-summary.md](phase2-summary.md), [phase3-summary.md](phase3-summary.md), [phase3-planned-reserve.md](phase3-planned-reserve.md).

**Corrected 2026-10-04:** arithmetic, OTA geometry, headroom policy, and capacity model repaired per [phase4-corrections.md](phase4-corrections.md). Tool-validated CSV fixtures under [fixtures/](fixtures/).

Analysis only — **no** project-owned partition CSV or `sdkconfig` change in this phase.

## Sizing inputs (held constant across strategies)

| Input | Value | Source |
|-------|------:|--------|
| Module flash | **16 MiB** (16,777,216 B, `0x1000000`) | N16R16, [HARDWARE.md](../../HARDWARE.md) |
| X02 baseline app image | 1,484,272 B | Phase 1 |
| X02 + Opus probe app image | 1,665,808 B | Phase 2 |
| Planned long-message flash (not in probe) | **80–178 KiB** (low / mid / high — do not collapse to one midpoint) | [phase3-planned-reserve.md](phase3-planned-reserve.md) |
| Policy margin @ **current** 1.5 MiB slot | **262,144 B** | `max(256 KiB, 15%×1,536,000)` |
| Policy margin @ **2.125 MiB** candidate slot | **334,234 B** | `max(256 KiB, ceil(15%×2,228,224))` |
| **Product-ready check** | `slot − (probe + reserve) ≥ required_margin(slot)` | [partition-model-output.txt](partition-model-output.txt) |
| 3‑min Opus @16 kbps mono (preferred) | **~360–400 KB** per message (+ Ogg/container) | [STORAGE.md](../../STORAGE.md) |
| Per-message planning size | **389,120 B** (~380 KiB) | Mid of 360–400 KB range |

Probe on the **current 1.5 MiB factory slot** is a **hard failure** (−129,808 B). X02 alone was already **marginal** (+51,728 B &lt; 256 KiB policy). No strategy that keeps **1.5 MiB** app without trimming can reach **product-ready** app headroom with Opus.

---

## 1. Current `SINGLE_APP_LARGE` layout (decoded)

Selected in [`firmware/sdkconfig.defaults`](../../../firmware/sdkconfig.defaults) via `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`. Generated table is identical for x02 / h30 / h31 ([partition-tables.txt](partition-tables.txt)).

### Flash map (logical)

```text
0x000000  ┌─────────────────────────────────────┐
          │  Bootloader (IDF default, ~32 KiB)   │
0x008000  ├─────────────────────────────────────┤
          │  Partition table (4 KiB)             │
0x009000  ├─────────────────────────────────────┤
          │  nvs (data, nvs)        24 KiB       │
0x00F000  ├─────────────────────────────────────┤
          │  phy_init (data, phy)    4 KiB       │
0x010000  ├─────────────────────────────────────┤
          │  factory (app, factory)  1500 KiB    │  ← sole application slot
0x187000  ├─────────────────────────────────────┤
          │  **Unused / unpartitioned tail**     │
          │  15,175,680 B (~14.48 MiB)           │
0x1000000 └─────────────────────────────────────┘
```

### Partition rows (exact)

| Name | Type / subtype | Offset | Size (bytes) | Size (human) |
|------|----------------|-------:|-------------:|--------------|
| nvs | data / nvs | `0x9000` | 24,576 | 24 KiB |
| phy_init | data / phy | `0xF000` | 4,096 | 4 KiB |
| **factory** | **app / factory** | **`0x10000`** | **1,536,000** | **1500 KiB (`0x177000`)** |

- **Factory app ends at** `0x10000 + 0x177000` = **`0x187000`** (1,601,536 B from flash base).
- **Unallocated tail:** `0x1000000 − 0x187000` = **`0xE79000`** = **15,175,680 B** (~**14.48 MiB**). Rounded desk copy “~14.4 MiB” is informal; use exact bytes for planning. Tail is **not** a mounted volume until a custom partition table defines it.
- **OTA:** none (`otadata` / second app slot absent).
- **On-chip outbox today:** **0 B usable** (no FAT/WL partition; failed uploads are not persisted — [STORAGE.md](../../STORAGE.md)).

### Current layout vs product needs

| Check | Result |
|-------|--------|
| Probe fits 1.5 MiB slot | **No** (overflow 129,808 B) |
| Probe + mid reserve + slot-dependent policy | Needs **2,110,363 B** before alignment; **2,113,536 B (2.015625 MiB)** after 4 KiB size rounding |
| Tail could host outbox **if** app were shrunk | Irrelevant while app must **grow**; tail is only useful after repartition |

---

## 2. Modeled strategies (16 MiB flash)

Assumptions for all custom layouts:

- Keep **nvs** and **phy_init** at the same offsets/sizes as today unless a migration note says otherwise (preserves Wi-Fi/calibration keys if flash is not fully erased).
- **4 KiB** flash-sector alignment; **application** partitions at **64 KiB** (`0x10000`) boundaries (ESP-IDF v5.4).
- Outbox uses **on-chip wear-leveled FAT** (`data,fat` + WL mount APIs) — **modeled** capacity only until formatted/measured.
- **Product-ready app slot** target: **2.125 MiB** (`0x220000`, 2,228,224 B) — passes probe + low/mid/high reserve vs **334,234 B** margin; **high** reserve leaves only **45,910 B** beyond policy (see §4).

### (a) Larger single factory app + on-chip FAT outbox (wear-leveled)

**Intent:** One factory slot sized for Opus + long-message integration + policy margin; dedicate essentially all remaining flash to outbox.

| Name | Type / subtype | Offset | Size | End |
|------|----------------|-------:|-----:|----:|
| nvs | data / nvs | `0x9000` | 24 KiB | `0xF000` |
| phy_init | data / phy | `0xF000` | 4 KiB | `0x10000` |
| **factory** | **app / factory** | **`0x10000`** | **`0x220000` (2.125 MiB)** | **`0x230000`** |
| **outbox** | **data / fat** (WL-backed) | **`0x230000`** | **`0xDD0000`** | **`0x1000000`** |

- **App bytes/slot:** 2,228,224 B (one slot).
- **Raw outbox partition:** **14,483,456 B** (`0xDD0000`, ~**13.81 MiB**). **Tool-validated:** [fixtures/partitions_single_factory.csv](fixtures/partitions_single_factory.csv).
- **OTA:** No in-flash second app slot (USB reflash or future layout change required for OTA).
- **Recovery:** Single app image; bricked flash needs serial reflash (same as today).

### (b) Dual OTA app slots + smaller on-chip FAT outbox

**Intent:** Reserve **two** equal app partitions for ESP-IDF OTA (`ota_0` / `ota_1` + `otadata`); trade flash for OTA at the cost of a smaller outbox than (a).

| Name | Type / subtype | Offset | Size | End |
|------|----------------|-------:|-----:|----:|
| nvs | data / nvs | `0x9000` | 24 KiB | `0xF000` |
| phy_init | data / phy | `0xF000` | 4 KiB | `0x10000` |
| otadata | data / ota | `0x10000` | 8 KiB (`0x2000`) | `0x12000` |
| *(alignment gap)* | — | `0x12000` | through `0x1FFFF` | — |
| **ota_0** | **app / ota_0** | **`0x20000`** | **`0x220000` (2.125 MiB)** | **`0x240000`** |
| **ota_1** | **app / ota_1** | **`0x240000`** | **`0x220000` (2.125 MiB)** | **`0x460000`** |
| **outbox** | **data / fat** (WL-backed) | **`0x460000`** | **`0xBA0000`** | **`0x1000000`** |

- **App bytes/slot:** 2,228,224 B × **2** (only one runs at a time). **No `factory` slot** — first image must target `ota_0` (manufacturing flow TBD).
- **Raw outbox partition:** **12,189,696 B** (`0xBA0000`). **Tool-validated:** [fixtures/partitions_dual_ota.csv](fixtures/partitions_dual_ota.csv) → [partition-tool-validation-dual.csv](partition-tool-validation-dual.csv).
- **OTA:** Flash geometry supports dual-slot **when** OTA stack + signing exist — **not implemented**.
- **Recovery:** Rollback to prior `ota_*` slot only after OTA infrastructure exists; otherwise serial reflash like today.

### (c) Keep current 1.5 MiB app + removable-only outbox

**Intent:** **No** on-chip repartition for a larger app or FAT outbox; rely on **USB MSC** (DOCK) or **SENSOR microSD** for durable queue bytes after outbox firmware exists.

| Name | Type / subtype | Offset | Size | Notes |
|------|----------------|-------:|-----:|-------|
| (unchanged) | — | — | factory **1,536,000 B** | Same as §1 |

- **App bytes/slot:** 1,536,000 B — **probe still overflows**; product-ready Opus + reserve **not achievable** without **feature trimming** or a different build profile.
- **On-chip usable outbox:** **~0 B** (tail remains unpartitioned or could be left unused deliberately).
- **Removable outbox:** **External** — practical capacity **gigabytes** once mount code exists; **not** available on every desk configuration (BOX on SENSOR loses DOCK USB-A; see [STORAGE.md](../../STORAGE.md)).
- **OTA:** Unchanged (none).

**Note:** Allocating the 14.4 MiB tail **only** to on-chip FAT while keeping a 1.5 MiB app does **not** solve the **application** blocker; this strategy is listed as a **product/storage** choice (accessory-backed queue), not a flash geometry that fixes codec integration size.

---

## 3. Comparison table

Planning message count uses **usable** outbox bytes (§4) and **389,120 B** per 3‑min Opus message. Ranges reflect **360–400 KB** payload spread.

| Strategy | App slots | App bytes/slot | Usable outbox estimate | 3‑min Opus messages (usable) | OTA | Migration risk |
|----------|----------:|---------------:|-----------------------:|-----------------------------:|-----|----------------|
| **Current `SINGLE_APP_LARGE`** | 1 | 1,536,000 | **0** (tail unpartitioned) | **0** on-chip | No | **Low** (baseline) |
| **(a) Larger factory + on-chip FAT** | 1 | **2,228,224** | **12,258,797 B modeled** (~11.7 MiB) | **~31.5** | No | **High** |
| **(b) Dual OTA + on-chip FAT** | 2 | **2,228,224** each | **10,317,358 B modeled** (~9.8 MiB) | **~26.5** | **Geometry only** | **High** |
| **(c) Current app + removable outbox** | 1 | 1,536,000 | **External** (not on-chip) | **Many** (media-limited) | No | **Low–med** (no partition change; still need outbox FW) |

### Product-ready app headroom (2.125 MiB slot, strategies a/b)

Projected image = probe **1,665,808** + planned reserve (**80 / 125 / 178 KiB**). Policy margin for this slot = **334,234 B**.

| Reserve case | Projected image (B) | Free in slot (B) | Beyond policy (B) | Product-ready? |
|--------------|--------------------:|-----------------:|------------------:|:--------------:|
| Low (80 KiB) | 1,747,728 | 480,496 | 146,262 | **Yes** |
| Mid (125 KiB) | 1,793,808 | 434,416 | 100,182 | **Yes** |
| High (178 KiB) | 1,848,080 | 380,144 | **45,910** | **Yes** (tight) |
| Current 1.5 MiB slot | (probe alone) | −129,808 | — | **No** |

Self-consistent minimum slot when the 15% margin dominates is `ceil(projected / 0.85)`. The exact low/mid/high minima are **2,056,151 / 2,110,363 / 2,174,212 B**, or **1.960898 / 2.012599 / 2.073490 MiB**. Rounded up to ESP-IDF v5.4.2's **4 KiB app-size alignment** for this non secure boot model, they are **2,056,192 / 2,113,536 / 2,174,976 B** (**1.960938 / 2.015625 / 2.074219 MiB**). App **offsets** still require 64 KiB alignment. The **2.125 MiB** candidate covers the high reserve case and lets consecutive OTA slots remain 64 KiB-aligned without a gap.

---

## 4. Filesystem metadata and safety floor

Do **not** treat raw `outbox` partition size as queue capacity. Values below are **modeled**, not measured on hardware.

| Deduction | Planning assumption | Applied here |
|-----------|---------------------|--------------|
| Wear-leveling + FAT overhead | **8%** of raw partition | × **0.92** |
| Free-space safety floor | **8%** of post-WL space | × **0.92** again |
| Per-message metadata | few KiB per clip | Negligible vs **389,120 B** audio |

**Modeled usable outbox:**

```text
modeled_usable = floor(raw_outbox × 0.92 × 0.92)
```

| Strategy | Raw outbox (B) | Modeled usable (B) | 3‑min @ 389,120 B |
|----------|---------------:|-------------------:|------------------:|
| (a) Single factory | 14,483,456 | **12,258,797** | **31.5** |
| (b) Dual OTA | 12,189,696 | **10,317,358** | **26.5** |
| **Δ (a − b)** | 2,293,760 | 1,941,439 | **~5.0 messages** |

Chunked upload ([STORAGE.md](../../STORAGE.md)) can keep **one** logical diary in the outbox as **many small retained chunks**; message **count** is still bounded by total bytes. PCM 3‑min diaries (~5.8 MiB) would dominate — **~2** full PCM diaries in (a) usable space vs **~30+** Opus diaries at the same geometry.

---

## 5. Attached storage vs on-chip outbox

| Point | Implication |
|-------|-------------|
| **USB stick / microSD** can hold a large pending queue once mount + outbox code exist | Reduces **urgency** to maximize on-chip FAT in strategy (b), but **does not** free app partition bytes. |
| **Firmware not mounting removable media** for outbox today | Field behavior is still “discard on POST failure” regardless of shopping-list media. |
| **Product should not depend on accessory-only** | A kid desk with BOX on DOCK may have USB; SENSOR perch has SD not USB-A. On-chip outbox remains the **only** path that works on **every** hardware configuration without extra purchases. |
| **Hybrid (recommended direction in architecture docs)** | On-chip FAT for **default** retry queue; removable for logs, bulk export, or overflow policy — Phase 5 can name this; Phase 4 only shows that **(a)** and **(b)** both supply **useful** on-chip capacity **if** app slots are enlarged. |

---

## 6. Migration and erase implications

| Action | Effect |
|--------|--------|
| **Flash new partition table** | **Destructive** for any data in regions whose offsets/sizes change. Unpartitioned tail today has **no** user data; risk is mainly **future** outbox content after (a)/(b) ship. |
| **Keep nvs @ `0x9000` / 24 KiB** | Wi-Fi credentials and device prefs **may survive** table update **if** flash is not chip-erased and NVS entry layout stays compatible — still require a **backup/export** procedure before migration in production. |
| **Move or shrink `factory`** | Requires **full reflash** of app image; in-field incremental update **not** supported without OTA infrastructure. |
| **Introduce `outbox` FAT** | First mount must **format** (or run `esp_vfs_fat_spiflash_mount` with format-on-fail policy). Expect **empty** outbox on first boot after migration. |
| **Revert to `SINGLE_APP_LARGE`** | Reflash old table + old app; **any on-chip outbox clips lost** unless copied off-device first. |
| **Removable-only strategy (c)** | No partition migration for flash geometry; users still face **data loss on reformat** of USB/SD if implementation formats blindly. |

**Desk / dev devices:** Phase 5 one-device proof (feasibility plan) should assume **save server-side inbox** + treat first custom-table flash as **wiping** experimental on-chip storage.

**Authorization:** This repo spike does **not** authorize fleet repartition or erase of attached storage.

---

## 7. Phase 4 conclusion (validated choices; product decision open)

### Does any layout achieve **product-ready app headroom** and **useful on-chip outbox**?

| Layout | Product-ready app | Useful on-chip outbox |
|--------|:-----------------:|:---------------------:|
| Current 1.5 MiB | **No** | **No** (unless repartition tail without enlarging app — still **no** on Opus size) |
| **(a) Larger factory + FAT** | **Yes** (2.125 MiB slot) | **Yes** (**31.5** modeled 3‑min Opus) |
| **(b) Dual OTA + FAT** | **Yes** (same per-slot size) | **Yes** (**26.5**; **~5** fewer than (a)) |
| **(c) Removable-only** | **No** (without trim) | **External only** |

**At least one layout provides both** — **(a)** and **(b)** satisfy Phase 4 acceptance on flash geometry alone. **(c)** does **not** fix the app partition blocker and should not be the **sole** answer for Opus + long-message integration.

### Product layout decision

The repository establishes that endpoints will operate on remote Wi-Fi, but it does not establish whether an operator can physically recover each deployed endpoint by USB serial or whether remote firmware update and rollback are product requirements. The final choice between **(a)** and **(b)** therefore remains **open**.

1. Choose **(a) single factory + larger FAT** if the product explicitly accepts physical USB-serial recovery and no rollback. It provides **~31.5** modeled three-minute messages.
2. Choose **(b) dual OTA + smaller FAT** if remote update and rollback are required and the OTA signing, boot-slot, and operating procedures will be implemented. It provides **~26.5** modeled three-minute messages. Parser-valid geometry does not make OTA operational.
3. Treat **(c)** as a complement after an on-chip choice, not as the solution to the current application overflow.

The additional **~5.0** modeled messages in (a) do not decide the recovery tradeoff. The product owner must decide whether deployed endpoints can rely on physical serial recovery and whether remote update with rollback is required before a production CSV is selected.

**Blockers unchanged:** Runtime RAM/IRAM (probe DRAM unchanged; IRAM ~100% on baselines), three-minute codec soak, and actual WL/FAT mount proof remain **outside** this partition spreadsheet.

### Implementation handoff after the product decision

- Adopt **(a)** vs **(b)** vs hybrid with removable overflow.
- Exact app slot size (2.125 MiB vs 2.25 MiB) after any feature trim from Phase 3 growth analysis.
- Project-owned `partitions.csv`, `sdkconfig.defaults`, flash script, and migration runbook.

---

## Artifact index

```text
docs/evidence/x02-opus-partition/phase4-partition-strategies.md  (this file)
docs/evidence/x02-opus-partition/phase4-corrections.md
docs/evidence/x02-opus-partition/fixtures/partitions_*.csv
docs/evidence/x02-opus-partition/partition-model-output.txt
scripts/opus_partition_model.py
```

Cross-links: [partition-tables.txt](partition-tables.txt), [phase3-planned-reserve.md](phase3-planned-reserve.md), [x02-opus-partition-feasibility.md](../../plans/x02-opus-partition-feasibility.md).
