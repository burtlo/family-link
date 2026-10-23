# Opus chunk end-to-end island demo (h31)

| Field | Value |
|-------|--------|
| **Status** | Owner-validated on desk hardware (Oct 2026) |
| **Demo ID** | `h31` (legacy island; not a v2 `LAB-*`) |
| **Uniqueness** | Only archive demo that **records Opus**, **uploads timed chunks**, **finalizes to Ogg on the server**, and **plays back** with HTTP Range — separate from x02 WAV send and from h30 local Opus soak |
| **Active doc** | This file is the canonical product/engineering record; implementation remains in the v1 POC archive |

## Purpose

Prove that a BOX-3 class device can:

1. Capture **16 kHz mono** voice (and light music) with **Opus** on the wire.
2. Upload **~2 s** binary chunks with checksum headers under **`family-message/1`**.
3. Complete a message so the host builds **`audio/ogg`** plus **`index.json`** seek metadata.
4. Play the last take on-device with acceptable continuity and level.

This is the reference execution for **v2 Opus + chunk lifecycle** discussions (see [inbox and media contract](../product/communication-contracts/inbox-and-media.md) and [message delivery](../product/communication-contracts/message-delivery.md)). It is **not** the x02 product shell, carousel, or WAV `post_wav` path.

## What it is not

| Nearby artifact | Gap |
|-----------------|-----|
| **x02** product shell | Sends **PCM WAV** multipart; no Opus chunk PUT on the wire today |
| **x02-opus-probe** | Links Opus for **size** qualification; send path still WAV-shaped |
| **h30** | **Local** encode/decode soak; no message server or Ogg |
| **LAB-15** | Active-tree **memory envelope** lab (PCM budget + UI); different goal and binary |
| **LAB-16** | SD streaming qualification; not Opus messaging |

## Operator UX (island)

| Control | Behavior |
|---------|----------|
| **Red circle** | Start / stop record (max **180 s**) |
| **Boot short** | Play **last completed** message |
| **Boot long** | Toggle next record profile: **16 kbps VOIP** vs **24 kbps audio** (status line shows `16 kbps` / `24 kbps` / `PASS`) |
| **Mute latch** | Must be open to record |

Status is **`board_status`** text (not a product SCR-* screen).

## Wire protocol (summary)

- **Create:** `POST /v1/messages` with `protocol: family-message/1`, `audio.codec: opus`, `target_chunk_ms: 2000`.
- **Chunks:** `PUT /v1/messages/{id}/audio/{seq}` — body is **uint16 LE length + Opus payload** per **20 ms** frame, repeated for ~2 s of audio per chunk (variable chunk duration if upload is slow).
- **Complete:** `POST /v1/messages/{id}/complete` with `audio_chunks` count and `duration_ms`.
- **Playback fetch:** `GET /v1/messages/{id}/audio` with **`Range:`**; server serves muxed **Ogg Opus** (`media.ogg`).
- **Index:** `GET …/index.json` — byte seek points (device play path today starts at offset 0).

Host process: dedicated **h31 Opus messages** Python server (default desk port **8080**), not the v1 product WAV server.

## Device audio pipeline (as built Oct 2026)

### Capture

- **Sample rate:** 16 kHz mono PCM → Opus **20 ms** frames.
- **Profiles:** `FL_OPUS_PROFILE_VOIP_16K` (16 kbps) or `FL_OPUS_PROFILE_APP_24K` (24 kbps).
- **Mic gain:** **38 dB** (product x02 record path historically used **42 dB** without a peak limiter).
- **Peak limiter:** If frame peak **> 26000** (~0.79 full scale), scale frame down before encode — reduces voice-following buzz from **input clipping** (distinct from codec bitrate).
- **Chunk upload:** **Dual buffers** + **async upload task** so the record loop **does not stop** reading the mic during `PUT` (fixes ~2 s dropouts that were obvious on music).

### Playback

- **Preferred path:** **Local mirror** in PSRAM — copy of length-prefixed packets from the last take; **Boot play** decodes mirror first (serial log: `play local mirror …`). Avoids depending on Ogg parse during desk iteration.
- **Fallback:** Download full Ogg (up to **~704 KiB** prefetch, **256 KiB** Range steps), **Ogg segment lacing** reassembly, then Opus decode.
- **Timing:** Wall-clock **paced** PCM write at 16 kHz (not burst-then-sleep only).
- **Speaker:** Volume **100**; **~80 ms** zero **drain** after play (same idea as h30/h25 island demos; x02 carousel voice play does not use post-clip drain).
- **Decoder:** `OPUS_RESET_STATE` before each play and before each new record session.

## Owner validation (Oct 2026)

| Scenario | Result |
|----------|--------|
| Voice @ **24 kbps**, normal level | Clean; volume acceptable at **100** |
| Voice @ **24 kbps**, louder / closer | Good after limiter + gain tweak (earlier take showed envelope-following static = clipping) |
| Music @ **16 kbps** VOIP | Buzz / crackle on loud material; **24 kbps** materially better |
| **~180 s** music soak @ **24 kbps** | Exercised long mirror/chunk path (Elton John *Rocket Man* desk test) |
| Playback continuity | Fixed during session: mid-play HTTP gaps, Ogg page vs packet confusion, record blocking on upload |

**Qualitative codec note:** At 16 kHz, **24 kbps Opus** is a strong voice default; **PCM WAV** on x02 is still **lossless at the same sample rate** — better for avoiding encoder coloration on music, at ~10× bytes. Neither path is “CD quality” on this mic.

## Engineering fixes captured in firmware (session arc)

1. **Range loop** — single small read stopped play after seconds; then full-file prefetch.
2. **Interleaved download + decode** — ~3 s silence every ~15 s; prefetch-before-decode.
3. **Per-page Opus decode** — missing Ogg **segment lacing** caused out-of-order/missing phrases; firmware reassembles packets; host mux appends **0** segment when payload ends on 255-byte boundary.
4. **Synchronous chunk PUT from record task** — ~2 s holes in music; dual-buffer async upload.
5. **Play mirror** — faithful replay of encoded packets for bench A/B.
6. **Gain / limiter / drain** — clipping and second-take static.

## How to run (desk)

Prerequisites: BOX-3 on USB, Wi-Fi and device token aligned with host hangout roster, **h31** server listening on the configured host (typically `0.0.0.0:8080`).

1. Flash island **h31** with sunset profile and box identity (`make flash DEMO=h31 …` in v1 POC archive).
2. Start **h31 Opus messages** server (not v1 WAV product server).
3. Record → wait for PASS / idle → **Boot** play.
4. Optional: long Boot for **24 kbps** before a music or long soak take.

Serial tags: `h31` — `message_id`, `chunk`, `play local mirror`, `SOAK PASS h31 180s`, `opus_decode` warnings.

## Proven vs open

| Proven | Not proven |
|--------|------------|
| Opus chunk create/PUT/complete against h31 server | x02 shell + same wire format |
| Ogg mux + Range GET playback (fallback path) | Production partition **2125** promotion for this binary (~1% app headroom on default slot) |
| **180 s** record with async chunks + mirror play | Carousel/inbox integration |
| 16 vs 24 kbps profile toggle | Formal lab manifest / scripted harness |
| Desk owner acceptance voice + improved music @ 24k | Multi-device inbox, sketch timeline |

## v2 direction (informing, not approved)

Treat **h31** as **as-built evidence** for:

- **2125 geometry + Opus on the wire** as a reasonable v2 media baseline.
- Integrate **chunk upload + Ogg or streaming decode** into product shell and v2 server inbox APIs.
- Carry forward **async chunk upload**, **capture peak limiter**, and **playback pacing** patterns; reconcile **mic gain** and **ROOMVOL** with [h18-style](../product/device-ui/assessments/ASSESS-DEMO-h18.md) stepped volume policy.

## Related indexes

- [Demos catalog README](README.md)
- [Demo explorer index](../product/device-ui/assessments/DEMO-EXPLORER-INDEX.md) — row **h31**
- [Recording/send v1 assessment](../v1-assessments/recording-send.md) — PCM product path
- [Labs registry](../labs/README.md) — v2 measured experiments (orthogonal to h31)
