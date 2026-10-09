# Feature: Async audio record and send

| Field | Value |
|-------|-------|
| **Status** | **Verified** (server multipart + idempotence); **Partial** (firmware — multipart only, discard-on-fail, 25 s send UI) |
| **Areas** | Server, firmware, web twin (simulated send) |
| **Last updated** | 2026-10-08 |

## Purpose

Capture a voice note after recipient selection, enforce mute and duration rules, upload to the household server, and fan out to recipient inboxes without blocking the UI indefinitely.

## UX

- Tap circle → picker → recording starts (start chirp).
- Stop: circle tap, **5 s silence** (VAD), or **3 min** cap; shoulder → discard (no upload).
- Mute latched → block with `unmute first`.
- Send UI: `Finishing` / `Sending...` / `Sent` (firmware); twin mimics receipt states.
- Failed upload: current as-built **discards** (no device outbox) per operational-contract plan.

## Behavior

- **Firmware:** PCM → WAV in RAM, `POST /v1/messages` multipart (`kind=audio`, `to_user_id` or broadcast, optional `client_message_id`, optional FLSK1 sketch).
- **Server:** Streams upload to disk archive; on commit, creates inbox rows and fires WebSocket notifications (bounded wait).
- **Chunk path:** JSON `family-message/1` + PCM chunks supported on server only — see [pcm-chunk-upload](pcm-chunk-upload.md).
- Multipart body cap 7 MiB; concurrent upload slots enforced (`body_limit.py`).

## Implementation

| Layer | Location |
|-------|----------|
| Record/upload | `firmware/v1/v1_record.c` |
| Multipart ingest | `user_mailbox.post_audio_stream`, `archive.py` |
| Limits | `shared/v1/timing.yaml` (record cap, silence), `V1_SEND_STUCK_MS` in record |
| Admin send | `POST /v1/admin/messages` (same multipart shape) |

## Constraints

- Mic live **only while recording** (no wake word).
- Server durable incoming chunks do **not** fix device 25 s sending screen until firmware adopts chunk/outbox design.
- Sketch capture exists in firmware but drawing notes are **out of v1 merge** scope in product spec.

## Verification

- Server: `test_legacy_upload_stream_range_restart_state`, `test_multipart_retry_receipt_after_restart`, `test_reject_invalid_media_metadata_and_oversized_body`, archive multipart/chunk tests.
- Firmware: device record → recipient receives message (manual).

## Remaining Work

- Resumable PCM chunk upload on device per [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md).
- Explicit outbox / retry UX (OPEN in operational-contract).
- Align upload HTTP timeout with `timing.yaml` (partially hardcoded 20 s / 25 s).
