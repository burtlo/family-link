# Feature: Message playback and sync

| Field | Value |
|-------|-------|
| **Status** | **Verified** (server read/position/restart); **Implemented** (firmware blob playback) |
| **Areas** | Server, firmware, web twin |
| **Last updated** | 2026-10-08 |

## Purpose

Recipients play voice messages from the server, scrub with persisted position, mark messages read on first play, and resume on another endpoint where they left off.

## UX

- Play/pause on center card; timeline always visible; scrub drag updates position.
- Switching cards pauses and saves position (spec).
- **Read** state distinct from merely focusing a card — set on first **Play**, not on select.
- Offline: firmware shows offline ribbon; playback may require live blob fetch (spec vs as-built conflict documented in operational-contract plan).

## Behavior

- `GET /v1/messages/{seq}/blob` — WAV stream; HTTP **206** byte ranges supported (Starlette `FileResponse`).
- `PUT /v1/messages/{seq}/read` with `position_ms` on first play.
- `PUT /v1/messages/{seq}/position` on scrub without necessarily marking read.
- State survives server restart (SQLite + archive tests).
- Optional sketch: `GET /v1/messages/{seq}/sketch` when manifest includes FLSK1.

## Implementation

| Layer | Location |
|-------|----------|
| Server state | `user_mailbox.mark_read`, `set_position`, `MAILBOX.media_path` |
| Firmware | `v1_carousel.c` (download, playback, API sync) |
| Web twin | `box.js` audio element + API calls |

## Constraints

- No automatic message expiry on v1 product server.
- Streaming playback architecture for very long messages is planned in [`STREAMING-PLAYBACK.md`](../STREAMING-PLAYBACK.md) — device still downloads full blob for typical messages.

## Verification

- `test_legacy_upload_stream_range_restart_state` (read, profile, restart, range 206/416).
- `test_accepted_broadcast_restart_and_state` (read flag after restart).

## Remaining Work

- Offline playback without server (OPEN — docs vs firmware toast `can't play right now`).
- Progressive/streaming play for chunk-origin messages when firmware adopts chunks.
- WebSocket-triggered inbox refresh vs poll on device (WS connected but inbox refresh path is implementation-specific).
