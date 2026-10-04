# Plan: Prove Opus on BOX-3

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `research/exploration` |
| **Owners / areas**             | Device audio, codec, host media |
| **Status**                     | `draft` |
| **Targets**                    | Isolated BOX-3 Opus demo; no X02 integration |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Resolves the codec experiment in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) |
| **As-built**                   | None |

## At a glance

Prove whether the BOX-3 can encode and decode intelligible 16 kHz mono Opus continuously with enough CPU and memory margin for the product. Compare 16 and 24 kbps, choose a seekable server representation, and leave PCM as the fallback.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Codec and container spike](#phase-1--codec-and-container-spike) | The build, library footprint, frame contract, and candidate container are known | `todo` |
| [Phase 2 — Local encode and decode](#phase-2--local-encode-and-decode) | Real BOX-3 microphone audio survives continuous encode/decode playback | `todo` |
| [Phase 3 — Chunk and server round trip](#phase-3--chunk-and-server-round-trip) | Opus chunks upload, finalize, stream, seek, and decode through a demo host | `todo` |
| [Phase 4 — Decision record](#phase-4--decision-record) | Evidence selects 16 kbps, 24 kbps, or PCM fallback | `todo` |

---

## Background

Current PCM uses 256 kbps and produces about 5.76 MB for three minutes. Opus at 16–24 kbps would produce approximately 360–540 KB before container overhead. Size alone is not enough: the device must encode in real time while capturing, decode without speaker underruns, and preserve enough CPU and memory for UI, Wi-Fi, TLS, outbox, and sketch capture.

This is an island demo. Do not modify X02 until the decision record is complete.

**Related docs:** [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), [`STREAMING-PLAYBACK.md`](../STREAMING-PLAYBACK.md).

## Phase 1 — Codec and container spike

**Goal.** Establish a reproducible Opus build and the exact frame/container choices before touching product code.

**Deliverables**

- New isolated firmware demo and matching server demo names added to the normal demo maps and Make targets.
- Documented Opus component source, version, license, ESP-IDF compatibility, configuration, flash growth, static RAM, and dynamic allocation behavior.
- 16 kHz mono, 20 ms input frame contract: 320 samples / 640 PCM bytes per frame.
- Two encode profiles: 16 kbps and 24 kbps voice.
- Error handling policy for a missing or corrupt frame.
- Evaluation of a standard seekable representation, preferably Ogg Opus if its memory and muxing cost are acceptable. Do not define a private final file format without recording why a standard container failed.
- Host-side inspection command or script that reports duration, bitrate, and decodability.

**Acceptance**

- Firmware builds within the existing partition constraints.
- The component source and license are recorded.
- A known PCM fixture encodes and decodes on the host with correct duration.
- The candidate final container supports streaming and time-based seek or a documented index.

**Status:** `todo`

## Phase 2 — Local encode and decode

**Goal.** Prove continuous real-hardware operation without network variables.

**Deliverables**

- Capture real BOX-3 microphone input for at least three minutes.
- Encode the same spoken sample at 16 and 24 kbps.
- Store the encoded output on the currently attached storage backend when available; otherwise use a bounded short fixture and report the limitation.
- Decode through the BOX-3 speaker using the normal 16 kHz mono codec path.
- Serial metrics at least every 10 seconds: encoded bytes, frames, dropped frames, encode time, decode time, free internal RAM, largest internal block, free PSRAM, largest PSRAM block, task stack watermark, and underrun count.
- Inject one missing frame and one corrupt frame and record audible and state-machine behavior.
- Record short representative samples or operator notes for quiet speech, normal speech, speech near the expected game/room noise, and silence.

**Acceptance**

- Three continuous minutes encode and decode without reset, watchdog, heap exhaustion, or cumulative timing drift.
- Encode p95 is below 10 ms per 20 ms frame; preferred target is below 5 ms.
- Decode p95 is below 10 ms per 20 ms frame.
- No unexplained frame loss or speaker underruns in the local path.
- Measured memory leaves documented margin for Wi-Fi/TLS and the UI; do not infer this only from successful allocation.
- A human listener can understand the tested speech at the chosen room volume.

**Status:** `todo`

## Phase 3 — Chunk and server round trip

**Goal.** Prove that Opus fits the standard chunk lifecycle and streaming playback model.

**Deliverables**

- Two-second Opus chunk upload using message ID, sequence, timing, size, and SHA-256 from [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).
- Server storage under one isolated demo directory with incoming and completed message directories.
- Final standard container or indexed immutable representation.
- Streaming playback to BOX-3 with pause, reconnect, and seek near the beginning, middle, and end.
- Forced loss of one chunk, duplicate retry, and out-of-order arrival.
- Comparison table for PCM, Opus 16 kbps, and Opus 24 kbps: encoded bytes, upload duration, encode/decode CPU, peak memory, seek behavior, and subjective intelligibility.

**Acceptance**

- A three-minute message completes and plays through the server without loading the full media into device or server application RAM.
- Duplicate identical chunks are harmless; conflicting duplicate data is rejected.
- Missing chunks prevent finalization and are named in the response.
- Playback resumes from a meaningful time position after reconnect.
- Container duration and message manifest duration agree within one Opus frame.

**Status:** `todo`

## Phase 4 — Decision record

**Goal.** Make the codec choice explicit for later agents.

**Deliverables**

- A dated result section or feature record with raw measurements and hardware/firmware versions.
- Decision: preferred `opus` profile, PCM baseline only, or further work required.
- If Opus passes, update the optional codec details in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), including container media type and seek/index behavior.
- If Opus fails, keep chunked PCM and record the exact blocker rather than leaving the codec question open.
- Identify reusable codec, mux, demux, and streaming helpers for later X02 integration.

**Acceptance**

- Another agent can tell which codec ships, why, and with what measured limits without repeating the experiment.
- The protocol document contains no unresolved representation details for an approved Opus path.

**Status:** `todo`

## Test matrix

| Dimension | Cases |
|---|---|
| Bitrate | 16 kbps, 24 kbps |
| Speech | Quiet, normal, noisy-room, silence |
| Duration | 10 s, 60 s, 180 s |
| Failure | Missing frame, corrupt frame, duplicate chunk, dropped connection |
| Playback | Start, pause/resume, seek middle, seek near end |
| Metrics | CPU time, heap, PSRAM, stack, bytes, underruns, drift |

## Open questions

1. Which Opus component version best matches the repository's ESP-IDF version?
2. Does Ogg muxing on-device add useful value, or should the server mux accepted Opus packets during finalization?
3. Is in-band forward error correction useful for stored messages, where reliable chunk retry already exists?

## References

- Existing PCM record path: [`firmware/v1/v1_record.c`](../../firmware/v1/v1_record.c)
- Existing streaming proof: [`firmware/demos/h18_playback_screen.c`](../../firmware/demos/h18_playback_screen.c)
- Protocol: [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md)
