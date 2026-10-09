# Known limitations and retired paths

**See also:** [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md), [`decisions/README.md`](../decisions/README.md), [`STORAGE.md`](STORAGE.md), [`ATTACHED-STORAGE.md`](ATTACHED-STORAGE.md), [`UVC-CAMERA.md`](UVC-CAMERA.md).

## Durable findings (decisions)

Authoritative ADR summaries — do not over-claim qualification or product scope:

| Topic | Decision record |
|-------|-----------------|
| v1 async audio merge scope | [`decisions/async-audio-v1-scope.md`](../decisions/async-audio-v1-scope.md) |
| Wake word / mic policy | [`decisions/no-wake-word-mic-only-while-recording.md`](../decisions/no-wake-word-mic-only-while-recording.md) |
| H32 fail, H38 Stage B pass, SD not required for v1 merge | [`decisions/storage-qualification-status.md`](../decisions/storage-qualification-status.md) |
| v1 server host | [`decisions/v1-host-is-v1-product-not-combined.md`](../decisions/v1-host-is-v1-product-not-combined.md) |
| Retired 64 GB card geometry | [`decisions/retired-64gb-sd-geometry.md`](../decisions/retired-64gb-sd-geometry.md) |
| Device incidents INT-001–015 | [`decisions/device-incidents.md`](../decisions/device-incidents.md) |

## Platform and UX

| Limitation | Label | Notes |
|---|---|---|
| No onboard camera | **Specification** | Child→parent images need dock **UVC** — not in firmware tree. |
| LCD idle glow | **Specification** | Not e-ink; dim backlight when locked. |
| Small mute button | **Specification** | Minecraft desk — Pmod button optional. |
| 320×240 UI | **Specification** | Short text; photos downscaled server-side. |
| DOCK vs SENSOR | **Specification** | **One** gold-finger accessory — camera/USB stick **or** microSD perch, not both. |
| SDMMC vs Pmod audio | **Specification** | GPIO overlap — pick **SENSOR storage** or **dock I2S1** mods, not both. |

## Firmware / product gaps (as-built)

| Limitation | Label | Notes |
|---|---|---|
| No durable outbox | **Observed** | Upload fail → **lost** clip (PSRAM). |
| On-chip H32 | **Observed** | **Unqualified** — PCM cadence failure. |
| Attached H38 | **Observed** | **Bounded lab profile only** — not product SD driver. |
| x02 playback vs record | **Observed** | Record **180 s**, play **~10 s** max ([`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md)). |
| Single-shot POST | **Observed** | No chunk retry on x02; 20 s timeout. |
| Opus in product shell | **Inferred** | h30/h31 proved on BOX; x02 still PCM path + partition pressure. |
| USB MSC / SD mount in x02 | **Observed** | **Absent** — qual is isolated demos only. |
| Wake-word stock ROM | **Specification** | Replace before other house; product forbids always-on mic. |

## Storage — retired and unqualified paths

| Path | Status | Action |
|---|---|---|
| **64 GB SDXC** card, **121,503,744** sectors, exFAT H37 epoch | **Retired** | Archival evidence only; **no new runs** |
| H37/H38 on wrong geometry / stale MBR digest | **Observed** failure mode | Fresh H37 or `prior-h38-run-dir` after LAYOUT |
| DOCK USB stick outbox | **Unqualified** | No H35–H38 MSC track; PHY/debug constraints |
| Full microSD card qualification | **Unknown** | H38 used **512 MiB** window only |
| H32 cadence on SD | **Planned, not run** | Stage C in attached plan |
| Fault / power-loss / card removal | **Planned, not run** | Stage D |
| Near-full filesystem | **Planned, not run** | Not H38 v1 scope |

## Partition / flash

| Limitation | Label | Notes |
|---|---|---|
| 1.5 MiB app slot | **Measured** | X02+Opus **overflows** — need **≥2.125 MiB** candidate (not selected). |
| Dual layout choice | **Unknown** | Single-factory vs dual-OTA — recovery vs rollback |
| Mixed BOX backups | **Observed** | **factory @ 0x10000** vs **ota_0 @ 0x20000** (Lynn) — restore/qual must match `qual_app_partition` |
| Unpartitioned 15 MiB tail | **Measured** | Unused until custom table flashed |

## USB and camera

| Limitation | Label | Notes |
|---|---|---|
| USB 1.1 FS host | **Specification** | ~12 Mbps shared; 720p30 sustained **not** realistic |
| Camera + stick | **Specification** | One USB-A; hub splits bandwidth |
| Flash with camera attached | **Observed** (ops) | Unplug dock USB-A for reliable download |

## Networking

| Limitation | Label | Notes |
|---|---|---|
| 2.4 GHz only | **Specification** | Congestion and range vs 5 GHz |
| No cellular fallback | **Specification** | Wi‑Fi outage = no send (and no local queue today) |
| TLS without SNTP | **Specification** | HTTPS to real CA **fails** until time sync |

## Qualification boundaries (do not over-claim)

- **H38 pass** = `sdmmc_bounded_fat32_v1` on **specific bound 32 GB** cards after H35/H37 chain + mandatory BOX restore.
- **H32 fail** = on-chip FAT **not** approved for PCM outbox cadence at tested rates.
- **Server-side** durable archive ≠ **device-side** qualified storage.
