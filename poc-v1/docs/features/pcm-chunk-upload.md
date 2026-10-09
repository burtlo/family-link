# Feature: PCM chunk message upload (server)

| Field | Value |
|-------|-------|
| **Status** | **Verified** (API + archive tests); **Experimental** for end-to-end product (firmware still uses multipart WAV) |
| **Areas** | Server |
| **Last updated** | 2026-10-08 |

## Purpose

Support resumable, bounded-memory upload of long PCM audio per [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) before devices adopt the same path.

## UX

- No box UI today — integrators use JSON create + chunk PUT + complete POST.
- Device users still see classic multipart send flow.

## Behavior

- Create: `POST /v1/messages` JSON `family-message/1`, `audio.codec=pcm_s16le`.
- Chunks: idempotent PUT with SHA-256 and timing headers; 409 on conflict.
- Complete: assembles WAV, publishes inbox, removes sender upload view.
- `GET /v1/outgoing/{client_message_id}` for send receipt after commit.
- Opus, abort, and abandoned-upload cleanup **not** implemented (README subset).

## Implementation

`demos/server/v1_product/chunk_api.py`, `archive.py` incoming/complete paths.

## Constraints

- Firmware does not call these routes (README 2026-10-07).
- Normative extensions belong in MESSAGE-PROTOCOL, not this feature doc.

## Verification

- `test_chunk_protocol_resume_complete_replay_and_authorization`
- Archive: `test_chunks_resume_idempotent_complete_and_hash_conflict`, crash resume tests.

## Remaining Work

- Firmware chunk cadence (~2 s) and completion handshake.
- Align operational timeouts with chunk upload on device.
- Opus / FLSK2 / culling endpoints per protocol doc.
