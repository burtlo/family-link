# Plan: Prove Opus on BOX-3

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `research/exploration` |
| **Owners / areas**             | Device audio, codec, host media |
| **Status**                     | `complete` (island demo) |
| **Targets**                    | Isolated BOX-3 Opus demo; no X02 integration |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Resolves the codec experiment in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) |
| **As-built**                   | `h30` + `h31` + `demos/server/h31_opus_messages` (desk verified 2026-10-04) |

## At a glance

Prove whether the BOX-3 can encode and decode intelligible 16 kHz mono Opus continuously with enough CPU and memory margin for the product. Compare 16 and 24 kbps, choose a seekable server representation, and leave PCM as the fallback.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Codec and container spike](#phase-1--codec-and-container-spike) | The build, library footprint, frame contract, and candidate container are known | `done` |
| [Phase 2 — Local encode and decode](#phase-2--local-encode-and-decode) | Real BOX-3 microphone audio survives continuous encode/decode playback | `done` (demo scope) |
| [Phase 3 — Chunk and server round trip](#phase-3--chunk-and-server-round-trip) | Opus chunks upload, finalize, stream, seek, and decode through a demo host | `done` (demo scope) |
| [Phase 4 — Decision record](#phase-4--decision-record) | Evidence selects 16 kbps, 24 kbps, or PCM fallback | `done` |

**Runbook**

| Step | Command |
|------|---------|
| Host | `make install-server` then `.venv/bin/python -m demos.server.h31_opus_messages.server --host 0.0.0.0 --port 8080` |
| Smoke (Mac) | `make demo-opus-messages` |
| Local Opus (kit) | `make flash DEMO=h30` — tap red ≥10 s, tap again → replay; max 180 s |
| Chunk + server (kit) | `make flash DEMO=h31` — tap red start/stop (max 180 s), short **Boot** = play, long **Boot** = 16/24k |

---

## Background

Current PCM uses 256 kbps and produces about 5.76 MB for three minutes. Opus at 16–24 kbps would produce approximately 360–540 KB before container overhead. Size alone is not enough: the device must encode in real time while capturing, decode without speaker underruns, and preserve enough CPU and memory for UI, Wi-Fi, TLS, outbox, and sketch capture.

This is an island demo. Do not modify X02 until product integration is explicitly authorized.

**Related docs:** [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), [`STREAMING-PLAYBACK.md`](../STREAMING-PLAYBACK.md).

## Phase 1 — Codec and container spike

**Status:** `done`

### Phase 1 as-built

- **Firmware demo id:** `h30` → `firmware/demos/h30_opus_local.c` (`make build-firmware DEMO=h30`).
- **Server host tools:** `demos/server/h30_opus/`, `scripts/opus_inspect.py`.
- **Libopus:** ESP Component Registry [`78/esp-opus` ^1.0.5](https://components.espressif.com/components/78/esp-opus) via `firmware/components/opus/` (BSD-style upstream license).
- **Wrapper:** `firmware/common/fl_opus.c` — 16 kHz mono, 320 samples / 640 PCM bytes per 20 ms frame; profiles **VOIP 16 kbps** and **AUDIO 24 kbps**; missing/corrupt decode uses PLC (`fl_opus.h`). Encoder complexity **2** (stack/CPU tradeoff).
- **Container:** Chunks and local soak use **uint16 LE length + Opus payload** per frame. **Server finalizes to Ogg Opus** (`demos/server/h31_opus_messages/ogg_mux.py`).
- **Flash size (h30):** app binary **~888 KiB** vs h25 **~702 KiB** (+~181 KiB). **h31** app binary ~**1.73 MiB** (near `SINGLE_APP_LARGE` ceiling — headroom risk before X02 merge).

## Phase 2 — Local encode and decode

**Status:** `done` (demo scope)

### Phase 2 as-built (h30)

- **Tap red:** start/stop mic → Opus encode (+ decode for metrics); **speaker muted** while recording.
- **Stop (≥10 s):** replay stored packets + fault injection → `-- PASS h30`. **180 s cap:** `-- SOAK PASS h30`.
- **Boot:** toggles 16/24 kbps for the next session (long-press pattern on h31; h30 uses Boot for bitrate).
- **Tasks:** `h30_audio` worker with **32 KiB** stack (PSRAM when available). **Do not** run Opus on `app_main` (stack overflow reboot).
- **Desk verification (2026-10-04):** Record + playback intelligible; no reboot after stack fix.

**Not formally logged in-repo:** 180 s continuous soak, encode/decode p95 tables, speech/noise matrix rows.

## Phase 3 — Chunk and server round trip

**Status:** `done` (demo scope)

### Phase 3 as-built (h31)

- **Firmware:** `h31` → `firmware/demos/h31_opus_chunks.c`.
- **Server:** `demos/server/h31_opus_messages` — `make demo-opus-messages`. Bearer tokens from `devices.*.yaml` **or** `hangout.*.yaml` endpoint tokens (`resolve_device_for_token`).
- **Flow:** `POST /v1/messages` → 2 s `PUT` chunks (protocol headers + SHA-256) → `POST …/complete` → Ogg on disk → `GET …/audio` with ranges.
- **Device upload:** `h31_rec` + `h31_up` tasks (Opus/HTTP off `app_main`). UI `recording…` via `board_status_set` from worker.
- **Playback:** Short **Boot** = play last message; **long Boot** = 16/24k for **next** record only (no profile flip during play).
- **Desk verification (2026-10-04):** Upload + playback working; server log shows PUT + complete.

### Container / seek

- **Completed file:** `audio/ogg` at `GET /v1/messages/{id}/audio` with `Accept-Ranges` / `206`.
- **Device:** `index.json` byte offsets + sequential Ogg page decode from Range start (no libogg). Full pause/reconnect/seek UI not productized on kit.
- **CI:** Chunk idempotency, conflict, missing-seq finalize, out-of-order PUT — `client.py`.

**Deferred for product:** 3 min hardware message, streaming playback without buffering full Ogg in one 16 KiB read, pause/resume/reconnect on device.

## Phase 4 — Decision record

**Status:** `done`

### Verdict

**Opus on BOX-3 is viable for async messages.** PCM remains the protocol baseline until X02 integration; Opus is the **recommended preferred codec** after island proof.

| Choice | Decision |
|--------|----------|
| **Default encode profile** | **16 kbps VOIP** (`FL_OPUS_PROFILE_VOIP_16K`) for voice messages |
| **Optional higher quality** | **24 kbps AUDIO** for A/B or quiet-room clips |
| **Chunk body** | Length-prefixed Opus packets (same as h30 store), 2 s PUTs |
| **Canonical server file** | **Ogg Opus** at finalize; not raw packet catenation |
| **Seek / resume** | Server `index.json` + HTTP Range; device sequential decode from offset |
| **PCM fallback** | Keep if integration cannot afford ~+180 KiB flash or h31-sized binary without partition work |

### Reuse for X02 (when authorized)

| Module | Path |
|--------|------|
| Codec | `firmware/components/opus/`, `firmware/common/fl_opus.c` |
| Chunk framing | h31 PUT body format |
| Server mux | `ogg_mux.py` pattern → v1 message finalize |
| Playback | Bounded buffer + Range + index (extend h18/v1 streaming doc) |

### Known limits (carry forward)

- **Stack:** Opus encode/decode must run on a **large dedicated task**, not `app_main`.
- **Partition:** h31 binary ~99% of factory app slot — grow partition or trim before adding X02 + Opus.
- **Auth:** h31 server accepts hangout endpoint tokens; v1 server already does.
- **UI:** Island demos use minimal `board_status_set`; not carousel/playhead.

### Protocol doc follow-up

Completed 2026-10-04: [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) now records preferred 16 kbps VOIP, two-byte-length-prefixed 20 ms packets, server-finalized `audio/ogg`, and the `index.json` seek helper. This standard update does not by itself change X02.

## Test matrix

| Dimension | Cases | Demo coverage |
|---|---|---|
| Bitrate | 16 kbps, 24 kbps | h30/h31 long Boot |
| Speech | Quiet, normal, noisy-room, silence | Operator informal only |
| Duration | 10 s, 60 s, 180 s | ≥10 s PASS; **180 s** tap-to-record until cap (`-- SOAK PASS h30` / `h31`) |
| Failure | Missing/corrupt frame, duplicate chunk | h30 fault replay; h31 `client.py` |
| Playback | Start, seek, reconnect | Short Boot play; full seek TBD |
| Metrics | CPU, heap, serial on h30 | Available in h30 logs |

## Open questions (resolved)

1. **Component version:** `78/esp-opus` **1.0.5** on ESP-IDF **5.4.x** — acceptable.
2. **Ogg on device:** **No** — server mux at finalize.
3. **FEC:** **No** for stored messages — chunk retry is enough.

## References

- PCM record path: [`firmware/v1/v1_record.c`](../../firmware/v1/v1_record.c)
- Streaming proof: [`firmware/demos/h18_playback_screen.c`](../../firmware/demos/h18_playback_screen.c)
- Device demos: [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md) (h30, h31)
- Protocol: [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md)
