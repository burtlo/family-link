# Audio I/O

**See also:** [`SPEAKER.md`](SPEAKER.md), [`HARDWARE.md`](HARDWARE.md), [`STREAMING-PLAYBACK.md`](../STREAMING-PLAYBACK.md).

## Onboard path (production default)

| Piece | Label | Detail |
|---|---|---|
| Playback | **Specification** | **I2S0** → **ES8311** → **NS4150** → **8 Ω / 1 W** speaker (**GPIO46** PA enable). |
| Capture | **Specification** | Dual mics → **ES7210** (**MIC1+MIC2**); **MIC3** = playback reference (AEC demos); **MIC4** typically unpopulated. |
| Digital ceiling | **Observed** (tree) | Codec volume to **100** / 0 dB — not a product “too quiet” bug without hardware change. |
| Sample format | **Observed** (demos + x02) | **16 kHz**, **s16le**, **mono** for mailbox/hangout islands; Opus chunks in h30/h31. |

Firmware using onboard path: **h05**, **h08**, **h11**, **x02** (per [`SPEAKER.md`](SPEAKER.md)).

## Recording constraints (product)

| Rule | Label | Detail |
|---|---|---|
| Mic live only while recording | **Specification** (product) | After recipient pick; **no wake word**, no always-on listen ([`REQUIREMENTS.md`](../REQUIREMENTS.md), AGENTS). |
| PTT controls | **Specification** + **Inferred** | Remap **mute** and/or **red circle**; optional **Pmod** arcade button ([`HARDWARE.md`](HARDWARE.md)). |
| Hardware mute latch | **Specification** | **GPIO1** — when latched **down**, onboard mics are **analog-muted**; firmware must still gate on PTT. |
| Screen protector | **Specification** (Espressif) | **Remove** or mics are muffled — called out in AGENTS and HARDWARE. |
| External mic / headset | **Specification** | No jacks on shell; **INMP441 on Pmod I2S1** or PCB mod — **not implemented** in product firmware. |
| USB mic on dock | **Specification** | **Skip** — no USB-audio stack; port reserved for camera. |

## Speaker and environment

| Topic | Label | Detail |
|---|---|---|
| Loudness | **Specification** | **1 W** desk volume, not room-filling; aim at player, not TV. |
| Louder output | **Specification** | Swap larger **8 Ω** cone, or **MAX98357A** / line-out on **Pmod I2S1** — firmware **not wired** for I2S1. |
| Headphones | **Specification** | No jack; **PCM5102 + amp on Pmod** or ES8311 PCB mod ([`SPEAKER.md`](SPEAKER.md)). |
| Half-duplex hangout | **Observed** (h11) | Mute playback path while transmitting; full-duplex + AEC **not** planned for v1. |
| Bluetooth audio | **Specification** | **BLE only** — no A2DP. |

## Message size vs audio hardware

From [`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md) (x02 + v1 server):

| Limit | Label | Value |
|---|---|---|
| Record config | **Observed** | Up to **180 s** WAV (~**5.78 MB**). |
| Playback buffer | **Observed** | **327,679 B** → ~**10.24 s** playable on device. |
| Upload timeout | **Observed** | **20 s** POST — weak uplink fails before memory on long clips. |
| 180 s + multipart copy | **Inferred** | ~**12 MiB** known large buffers in **16 MiB PSRAM** — needs **hardware measurement** before “reliable.” |

Chunked Opus upload (h30/h31) reduces peak memory; **not** integrated in x02 as of docs.

## Push-to-talk (PTT) summary

```
User holds PTT (mute / red circle / future Pmod)
  → firmware opens capture path (ES7210 / future I2S1)
  → on release: encode or WAV finalize → HTTP POST (or chunks)
  → mic path off again; no background listen
```

Stock BOX firmware **must be replaced** before deployment — factory image is wake-word assistant (“Hi E.S.P.”).
