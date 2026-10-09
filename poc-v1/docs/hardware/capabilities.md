# Hardware capabilities — spec vs evidence

**See also:** [`HARDWARE.md`](HARDWARE.md), [`REQUIREMENTS.md`](../REQUIREMENTS.md), [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md), [`UVC-CAMERA.md`](UVC-CAMERA.md).

Legend: **Specification** (vendor/docs), **Observed** (measured on project hardware), **Inferred** (reasonable from demos, not fully qualified), **Unknown** (not decided or not tested).

## Core platform

| Capability | Specification | Observed | Inferred | Unknown |
|---|---|---|---|---|
| Desk appliance, USB powered | Yes | Yes (dev desks) | — | Remote-house thermal/noise |
| 2.4 GHz Wi‑Fi | b/g/n only | Join demos (h06, h31, x02) | Remote TLS path ([`TLS.md`](../TLS.md)) | Every deployment SSID quality |
| 320×240 UI + touch PIN | ILI9341 + GT911 | h01+, x02 | — | — |
| Hold-to-talk / record | Mute or red circle | h05, h08, x02 (demo caps vary) | Pmod arcade button | Final PTT UX with Switch |
| Dual mic + speaker | ES7210 + ES8311 | h05, h08, h11, x02 | Desk-distance OK; TV bleed | Minecraft headset path ([`SPEAKER.md`](SPEAKER.md)) |
| Async audio mailbox | Protocol + server | h08–h10, combined, v1 server | — | Durable outbox on device |
| Parent → child photo | Server RGB565 preview | h13, x02 | — | — |
| Child → parent photo | Dock UVC | **Not in tree** | One-still POST feasible | v1 merge scope |
| Live PTT hangout | Half-duplex over Wi‑Fi | h11, h21, h31 Opus islands | — | Full-duplex + AEC |
| Cellular / 5 GHz | No | — | — | — |
| Wake word / always-on mic | Stock demo only | Replaced in product intent | **Forbidden** for ship | — |

## Storage backends

| Backend | Specification | Observed | Inferred | Unknown |
|---|---|---|---|---|
| Server archive | Required for inbox | Yes (combined, v1) | Canonical playhead | — |
| PSRAM staging (one take) | 16 MiB | x02 allocates up to **180 s** WAV buffer | Peak upload ~12 MiB known buffers ([`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md)) | 180 s + sketch **hardware malloc** proof |
| On-chip FAT outbox | ~12 MiB modeled after partition work | H32: boot/mount/Opus cadence OK | PCM cadence **failed** | Production partition + H32 retry |
| SENSOR microSD | Slot on SENSOR; SDMMC GPIO 9/11/12/13/14/42 | H35/H37/H38 on **bound 32 GB** card | Bounded **512 MiB** FAT32 profile only | Stages C–D, other cards, product mount |
| DOCK USB stick | USB 1.1 MSC host possible | **Not qualified** | Conflicts with camera on one USB-A | USB host + debug PHY coexistence |
| Inbox on box | — | **No** (by design) | — | — |

## Audio / video limits (product-relevant)

| Limit | Specification | Observed | Inferred | Unknown |
|---|---|---|---|---|
| Recording cap (x02) | — | **180 s** config (`V1_RECORD_MAX_SEC`) | — | Safe at max length under real UI/Wi‑Fi |
| Playback cap (x02) | — | **327,679 B** (~**10.24 s** WAV) | Longer messages upload but truncate on play | When playback buffer rises |
| Demo record cap (h08) | — | **10 s** | Legacy demos only | — |
| Dock camera | UVC MJPEG, FS ≤ ~4–8 Mbps stream | Espressif example only | 320×240 still POST first | Child→parent in v1 |

## Qualification summary (2026-10-08)

| Track | Observed result | Does **not** prove |
|---|---|---|
| **H32** on-chip | **Failed** PCM cadence (79/90 and 44/90 misses); Opus cadence **passed** | Durable outbox, fault recovery, near-full |
| **H38** attached (32 GB) | **`io_complete`** on bound card(s) — Mazi + Lynn public evidence | H32 cadence on SD, full card, removal, production layout |

Evidence index: [`evidence/onchip-storage-qualification/`](../evidence/onchip-storage-qualification/), [`evidence/attached-storage-qualification/`](../evidence/attached-storage-qualification/).
