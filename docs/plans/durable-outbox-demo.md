# Plan: Prove a durable message outbox

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `feature-plan` |
| **Owners / areas**             | Device storage, record path, upload recovery, demo server |
| **Status**                     | `draft` |
| **Targets**                    | Isolated BOX-3 outbox demo using the qualified on-chip FAT/WL backend first |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Implements the persistence contract in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) |
| **As-built**                   | None |

## At a glance

Prove that a valuable recording survives server loss, Wi-Fi interruption, application restart, and device reboot. The demo records small chunks to qualified on-chip storage, retries idempotently, and deletes local data only after a durable matching server acknowledgement.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Adopt the qualified storage backend](#phase-1--adopt-the-qualified-storage-backend) | The measured on-chip backend is available through a backend-neutral interface | `todo` |
| [Phase 2 — Persistent local queue](#phase-2--persistent-local-queue) | Closed chunks and queue metadata survive reboot and partial writes | `todo` |
| [Phase 3 — Retry protocol](#phase-3--retry-protocol) | Server outage and reconnect complete without losing or duplicating a message | `todo` |
| [Phase 4 — Failure campaign](#phase-4--failure-campaign) | Power and network interruption cases have repeatable evidence | `todo` |
| [Phase 5 — Product handoff](#phase-5--product-handoff) | Reusable storage/outbox boundaries and measured limits are ready for X02 | `todo` |

---

## Background

h22 already proves one-second PCM recording chunks and sequential multipart writes, but drops a failed chunk. h24 demonstrates an acknowledgement cursor for small in-memory event batches. The completed h31 demo proves preferred two-second Opus chunk framing and upload. This demo combines those shapes with durable media storage.

The on-chip `data,fat` partition is the first outbox backend. Complete [`onchip-storage-qualification.md`](onchip-storage-qualification.md) before implementing the queue, then consume its measured capacity, safe free-space floor, latency, mount, and recovery behavior through a backend-neutral interface. Attached removable storage remains useful as optional overflow, export, or alternate-backend capacity; it is not a prerequisite for the first durable queue proof.

This is an island demo. Do not implement it inside X02.

**Related docs:** [`onchip-storage-qualification.md`](onchip-storage-qualification.md), [`STORAGE.md`](../STORAGE.md), [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).

## Local filesystem contract

Suggested mount-relative layout:

```text
/family-outbox/
  queue.jsonl
  messages/
    <client-message-id>/
      manifest.json
      audio/
        000000.chunk
        000001.chunk
      sketch/
        000000.flsk2
      closed
```

The device writes new files to a sibling `.part` name, flushes and closes them, then atomically renames them. A chunk becomes queue-visible only after its final file exists. Manifest replacement uses write-new, flush, rename; it is never edited in place.

The local manifest records protocol version, provisional/client ID, sender/recipient selection, codec parameters, chunk sequence/timing/checksum/size, sketch metadata, acknowledgement state, whether recording is closed, and whether server completion was acknowledged.

Deletion order after completion:

1. Receive and validate final server completion response.
2. Persist local completion acknowledgement.
3. Delete acknowledged chunk files.
4. Delete manifest and message directory.
5. Compact the queue journal later.

This order favors duplicate recovery over recording loss.

## Phase 1 — Adopt the qualified storage backend

**Goal.** Reuse the device-proven on-chip filesystem behind a small interface before building queue semantics.

**Deliverables**

- Consume the completed evidence and safe free-space floor from [`onchip-storage-qualification.md`](onchip-storage-qualification.md); do not replace its measured limits with the earlier capacity model.
- Define a small backend interface: mount, free bytes, atomic replace, enumerate message directories, open/read/write/flush/close, checksum, and remove acknowledged data.
- Implement the first backend with the qualified on-chip FAT-over-wear-leveling partition and preserve its demonstrated no-auto-format recovery behavior.
- Keep partition, FAT, and wear-leveling details outside the queue state machine so a removable backend can implement the same interface later.
- Record the experiment partition fixture and clarify that its single-factory geometry does not settle the production choice between single-factory and dual OTA.

**Acceptance**

- The on-chip qualification plan is complete and linked to device evidence for boot, NVS, mount, capacity, cadence, interruption recovery, safe floor, and restore.
- Backend conformance repeats mount, write/flush/close, SHA-256 read-back, atomic rename, enumeration, delete, and reboot/remount through the interface rather than direct filesystem calls.
- The interface refuses writes at the measured free-space floor and preserves previously committed files.
- Queue code has no dependency on FAT paths, wear-leveling handles, or removable-media APIs outside the backend implementation.

**Status:** `todo`

## Phase 2 — Persistent local queue

**Goal.** Record multi-minute PCM chunks locally without depending on the network.

**Deliverables**

- Start from h31's two-second length-prefixed Opus chunks and the local layout above. Keep the queue codec-neutral so PCM chunks remain a fallback test case.
- Generate a strong provisional client message ID before the first chunk.
- Calculate SHA-256 while writing or immediately after close.
- Persist sequence, start time, duration, bytes, and checksum.
- Recover queue and next sequence after device reboot.
- Close a recording with button, silence, duration cap, and recovered-after-reset reasons where applicable.
- Enforce a reserved-free-space floor before starting and before each chunk.
- User-visible demo state for recording, safely stored, pending upload count, uploading, sent, storage full, and storage unavailable.

**Acceptance**

- A three-minute 16 kbps Opus recording survives reboot before any upload occurs. A shorter PCM fallback fixture proves that queue semantics do not depend on Opus.
- Every recovered final chunk matches its manifest checksum and sequence.
- Temporary or corrupt files are quarantined or ignored with a visible diagnostic; valid prior chunks remain available.
- Storage exhaustion stops safely and preserves the already closed portion of the recording.

**Status:** `todo`

## Phase 3 — Retry protocol

**Goal.** Deliver the stored message exactly once at the product level despite repeated transport attempts.

**Deliverables**

- Demo server implementing create, chunk PUT, upload status, and complete from [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).
- Background uploader independent from the capture loop.
- Recovery of local-only messages by creating/associating the server message after connectivity returns.
- Status query on reconnect to avoid unnecessary retransmission.
- Exact validation of durable acknowledgement before local deletion.
- Exponential retry backoff with jitter and a bounded maximum; user recording remains available while retry sleeps.
- Support recording a second message while the first remains pending, within configured storage limits.

**Acceptance**

- Starting with the server offline, a complete recording remains local and uploads after the server starts.
- Interrupting Wi-Fi during any chosen chunk does not lose earlier or later recorded data.
- Duplicate requests create one server chunk and one completed message.
- Losing the completion response leads to an idempotent recovery, not a duplicate inbox message.
- The server's accepted source-audio checksum and duration match the local closed manifest. The server separately records the checksum of finalized `media.ogg` or `media.wav`.

**Status:** `todo`

## Phase 4 — Failure campaign

**Goal.** Produce evidence that the outbox protects recordings at every meaningful interruption point.

**Deliverables**

- Repeatable runbook and result table for the failure matrix below.
- Serial and server logs containing message ID, sequence, checksums, acknowledgement, reboot recovery, and deletion.
- Filesystem inspection before and after recovery.
- At least ten repeated cycles of the highest-risk power interruption cases.

**Acceptance**

- No accepted test loses a committed chunk.
- No test produces two completed server messages for one client message ID.
- No local chunk is deleted without a matching durable acknowledgement.
- Every failure ends in either a completed message or an intelligible pending/error state that retains the recording.

**Status:** `todo`

### Failure matrix

| Interruption point | Expected recovery |
|---|---|
| During `.part` write | Temporary file removed/quarantined; prior chunks retained |
| After chunk rename, before manifest update | Startup reconciliation discovers or quarantines the orphan without corrupting prior data |
| After manifest update, before upload | Chunk uploads after reboot |
| During request body | Same chunk retries |
| After server commit, before response | Status query discovers durable chunk; local copy then deletes |
| Between last chunk and complete request | Completion resumes after reboot |
| After server completion, before response | Idempotent complete returns existing message |
| After completion acknowledgement, during local cleanup | Startup cleanup removes only data already recorded as completed |
| Storage becomes unavailable or unmounted | Recording stops safely; existing data is not treated as sent |
| Server disk full (`507`) | Local message remains pending and retry is delayed |

## Phase 5 — Product handoff

**Goal.** Make the proven work importable without copying the demo wholesale.

**Deliverables**

- Storage backend interface and durable outbox state machine separated from demo UI.
- Memory, stack, storage, throughput, and retry measurements.
- Recommended chunk duration and maximum number/bytes of pending messages.
- Exact UI states and privacy-safe pending count behavior.
- Documented interaction with sign-out, recipient selection, silence stop, and recording cap.
- Feature record or dated result document identifying the tested storage hardware and firmware revision.
- Update [STORAGE.md](../STORAGE.md) from “not built” to the measured as-built state only after implementation passes.

**Acceptance**

- A future X02 agent can import named helpers and state transitions rather than merging the demo source.
- All failure-campaign evidence is linked from the handoff.
- Remaining backend-specific risks are explicit.
- Optional removable USB or microSD qualification is a separate backend/overflow experiment after the on-chip outbox passes; it does not block this handoff.

**Status:** `todo`

## References

- Chunk capture proof: [`firmware/demos/h22_diary.c`](../../firmware/demos/h22_diary.c)
- Chunk server proof: [`demos/server/h22_diary/server.py`](../../demos/server/h22_diary/server.py)
- Storage constraints: [`STORAGE.md`](../STORAGE.md)
- Protocol: [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md)
- On-chip backend prerequisite: [`onchip-storage-qualification.md`](onchip-storage-qualification.md)
