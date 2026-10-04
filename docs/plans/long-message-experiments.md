# Plan: Prove durable long messages

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `version-roadmap` |
| **Owners / areas**             | Device firmware, server protocol, storage, audio, sketches |
| **Status**                     | `draft` |
| **Targets**                    | Island demonstrations followed by a separately approved X02 integration |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Extends [`v1-product-spec.md`](v1-product-spec.md) for durable long media |
| **As-built**                   | None |

## At a glance

Prove long messages in isolated steps while protecting the current X02 path. The order removes the known playback ceiling first, establishes canonical server storage and protocol behavior, proves recordings survive failure, then evaluates compression and full-message sketches before product integration.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Record the baseline](#phase-1--record-the-baseline) | Current X02 and server limits have reproducible evidence | `todo` |
| [Phase 2 — Prove streaming playback](#phase-2--prove-streaming-playback) | BOX-3 plays long server files with bounded RAM and range resume | `todo` |
| [Phase 3 — Prove canonical server storage](#phase-3--prove-canonical-server-storage) | Chunk sessions finalize into restart-safe message directories | `todo` |
| [Phase 4 — Prove the durable outbox](#phase-4--prove-the-durable-outbox) | Recordings survive offline operation, interruption, and reboot | `todo` |
| [Phase 5 — Prove the end-to-end codecs](#phase-5--prove-the-end-to-end-codecs) | Preferred Opus and fallback PCM both survive the durable lifecycle | `todo` |
| [Phase 6 — Evaluate Opus](#phase-6--evaluate-opus) | h30/h31 select 16 kbps VOIP, Ogg finalization, and indexed range playback | `done` (island proof) |
| [Phase 7 — Prove full-message sketching](#phase-7--prove-full-message-sketching) | Sparse FLSK2 chunks synchronize for the entire message and through seeks | `todo` |
| [Phase 8 — Prepare X02 integration](#phase-8--prepare-x02-integration) | Proven helpers and migration steps are ready for a separately authorized product change | `todo` |

---

## Background

The current X02 can record up to three minutes but downloads playback into a buffer that holds only about 10.24 seconds. Recording is staged in volatile PSRAM, upload is one large request, failed sends are discarded, and the server materializes complete media in RAM. Existing h18 and h22 demos already prove important pieces but not the durable combined lifecycle.

This roadmap does not authorize X02 implementation. Each phase is an island experiment with evidence and handoff. Opus Phase 6 completed early because h30/h31 were independent island proofs; its remaining product checks are explicitly carried into Phases 5 and 8. Do not weaken an earlier acceptance criterion to claim a later integration phase complete.

**Related docs:** [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), [`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md), [`durable-outbox-demo.md`](durable-outbox-demo.md), [`opus-demo.md`](opus-demo.md).

## Phase 1 — Record the baseline

**Goal.** Preserve measurements of current behavior so improvements and regressions are visible.

**Deliverables**

- Record X02 firmware revision, ESP-IDF/BSP version, BOX-3 variant, PSRAM, attached storage, and server host details.
- Capture free/largest internal RAM and PSRAM before recording, during recording, before upload, during upload, and during playback.
- Record successful full-playback boundary and behavior above it.
- Record upload time for 10, 60, and 180 seconds on the test network where practical.
- Record current server process memory and on-disk files for the same messages.
- Store results in a dated feature or experiment-results document; do not overwrite estimates in `MESSAGE-COSTS.md` with measurements lacking provenance.

**Acceptance**

- Evidence confirms or corrects every relevant current-limit claim in [`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md).
- All hardware and build identifiers needed to repeat the measurements are present.

**Status:** `todo`

## Phase 2 — Prove streaming playback

**Goal.** Remove duration-dependent playback RAM without changing X02.

**Deliverables**

- Extend the h18-style island path to v1 authentication and completed-message metadata.
- Server file streaming with correct content length, ETag, and byte ranges.
- Device playback state machine and 32–64 KiB bounded buffering described in [`STREAMING-PLAYBACK.md`](../STREAMING-PLAYBACK.md).
- Three-minute and longer PCM fixtures.
- Pause, range resume, card exit, and forced reconnect cases.

**Acceptance**

- A message longer than three minutes plays completely with memory independent of duration.
- Resume uses a range rather than downloading and discarding the prefix.
- UI remains responsive and position follows samples sent to the speaker.
- Forced connection interruption produces visible recovery and no false completion.

**Status:** `todo`

## Phase 3 — Prove canonical server storage

**Goal.** Implement the server half of the protocol in an isolated host with consistent filesystem evidence.

**Deliverables**

- Create, chunk PUT, upload status, complete, and streaming GET endpoints from [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).
- `incoming/`, `messages/`, `trash/`, and mutable state layout from [`SERVER-MESSAGE-STORAGE.md`](../SERVER-MESSAGE-STORAGE.md).
- Stream-to-temporary-file receive path with checksum and atomic rename.
- Restart index rebuild from manifests.
- PCM finalizer that writes canonical WAV sequentially and removes redundant chunks after commit.
- Administrator cull preview, trash, and restore proof using test messages.

**Acceptance**

- Three-minute input does not create a payload-sized Python memory allocation in application code.
- Server restart preserves open upload status and completed inbox/media lookup.
- Duplicate and conflicting chunks follow the standard responses.
- Only complete directories appear in inbox results.
- Cull preview reports exact IDs and bytes; trash and restore preserve manifest/media hashes.

**Status:** `todo`

## Phase 4 — Prove the durable outbox

**Goal.** Make capture safe before network delivery.

**Deliverables**

- Execute every phase in [`durable-outbox-demo.md`](durable-outbox-demo.md) against the Phase 3 server.
- Qualify the storage currently attached to the device.
- Retain audio and sketch chunks until exact durable acknowledgements.
- Reboot and failure campaign with saved logs and filesystem evidence.

**Acceptance**

- The durable-outbox plan's acceptance criteria all pass.
- A good three-minute recording made while the server is unavailable eventually becomes one completed server message.
- Device reset at each listed interruption point does not lose a committed chunk.

**Status:** `todo`

## Phase 5 — Prove the end-to-end codecs

**Goal.** Demonstrate the complete durable standard with preferred Opus and confirm that PCM remains a functioning fallback.

**Deliverables**

- One island firmware combining bounded Opus capture, durable outbox, background upload, completion, inbox fetch, streaming playback, and playhead.
- One island server combining canonical storage, inbox reference, notification, streaming, restart recovery, and administrator trash.
- Simultaneous recording and background retry where storage capacity permits.
- End-to-end identifiers and checksums visible in logs.
- A PCM fallback fixture through the same message lifecycle and interfaces.

**Acceptance**

- A three-minute 16 kbps Opus recording succeeds when online.
- A three-minute Opus recording made offline survives reboot and succeeds later.
- Recipient hears one gapless message and sees one inbox entry.
- Server and device memory remain bounded by chunk/buffer sizes rather than total duration, except canonical media on disk.
- A broadcast creates multiple inbox references and one canonical media directory.
- PCM fallback finalizes to WAV and streams without changing outbox or inbox semantics.

**Status:** `todo`

## Phase 6 — Evaluate Opus

**Goal.** Record the completed island codec decision and carry its remaining product risks forward.

**Deliverables**

- Completed h30 local encode/decode at 16 and 24 kbps.
- Completed h31 two-second chunks, idempotency/conflict checks, server-side Ogg mux, `index.json`, and range playback.
- Decision recorded in [`opus-demo.md`](opus-demo.md): 16 kbps VOIP preferred, 24 kbps AUDIO optional, PCM fallback retained.
- Carry formal 180-second soak, p95 codec timing, bounded product streaming, and X02 partition sizing into Phases 5 and 8.

**Acceptance**

- [`opus-demo.md`](opus-demo.md) is marked complete with desk evidence and known limits.
- The approved packet framing, Ogg representation, and seek index are specified in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).
- Remaining product risks are acceptance work for integration rather than unresolved codec design.

**Status:** `done` (island proof)

## Phase 7 — Prove full-message sketching

**Goal.** Keep sparse synchronized drawing available for the entire audio message.

**Deliverables**

- FLSK2 capture and validation from [`SKETCH-TIMELINE.md`](../SKETCH-TIMELINE.md).
- Thinning measurements for sparse, intermittent, and continuous drawing.
- Durable sketch chunks sharing the message outbox and retry lifecycle.
- Playback synchronized to streaming audio, including pause, reconnect, restart, and seek.
- Optional derived keyframe experiment for dense-seek performance.

**Acceptance**

- Drawing near the end of a three-minute message records and renders at the correct audio time.
- No-drawing intervals consume no sketch chunk files.
- Reboot with pending sketch and audio preserves both.
- Seeking produces the correct canvas and subsequent animation.
- Continuous drawing stays within protocol limits using measured thinning behavior.

**Status:** `todo`

## Phase 8 — Prepare X02 integration

**Goal.** Hand off proven components and a migration sequence without changing the product binary in this roadmap.

**Deliverables**

- Named reusable modules for storage backend, outbox, chunk client, stream player, codec adapter, and sketch timeline.
- Server migration plan from current `UserMailbox` blobs to message directories and durable inbox state.
- Compatibility plan for existing single-POST clients and stored messages.
- X02 UI state map for safely stored, pending, uploading, sent, reconnecting playback, storage full, and server full.
- Updated resource budget from measured results.
- Rollback plan retaining the existing short-message path until end-to-end acceptance passes.
- A separate X02 implementation plan for user approval.

**Acceptance**

- Future agents can identify exactly which demo helpers to import and which demo-only code to leave behind.
- Every protocol requirement maps to a tested component or an explicit remaining task.
- No unresolved codec, storage format, directory layout, or timeline representation remains hidden in demo code.

**Status:** `todo`

## Experiment discipline

- One primary uncertainty per island demo.
- Record hardware, firmware revision, server revision, storage medium, and network conditions with results.
- Preserve raw logs and manifest examples under a documented non-secret results directory.
- Do not put family recordings in committed fixtures; use generated tones and consenting test speech.
- Do not call a phase complete from a smoke test alone when its acceptance requires reboot or interruption evidence.
- Update standards only when evidence changes a decision; record compatibility implications.

## References

- Existing streaming proof: [`firmware/demos/h18_playback_screen.c`](../../firmware/demos/h18_playback_screen.c)
- Existing chunk proof: [`firmware/demos/h22_diary.c`](../../firmware/demos/h22_diary.c)
- Current product firmware: [`firmware/v1/`](../../firmware/v1)
- Current product server: [`demos/server/v1_product/`](../../demos/server/v1_product)
