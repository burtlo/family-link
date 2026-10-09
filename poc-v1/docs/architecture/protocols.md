# V1 protocols (as built)

This document summarizes **current** wire behavior. Normative future work for resumable long messages lives in [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) — implementers extending chunk upload, Opus, or abort semantics should treat that doc as the target contract.

## Transport

| Link | Protocol | Notes |
|------|----------|-------|
| Device ↔ server | HTTPS (TLS optional via `--ssl-certfile` / Tailscale in deployment) | Base URL from firmware build/config |
| Inbox push | WebSocket `wss://…/v1/ws` | Hello with endpoint token; server pushes `inbox` events |
| Browser admin/twin | Same origin HTTP to v1 server | CORS `*` on API |

## REST headers

| Header | When |
|--------|------|
| `Authorization: Bearer <endpoint_token>` | All device/twin API calls |
| `X-User-Id: <user_id>` | Inbox, send, profile, read/position, blob fetch as recipient |
| `Authorization: Bearer <admin_token>` | `/v1/admin/*` after login |

## Session login

`POST /v1/session/login` — JSON `{ "user_id", "pin" }`.

Success: inbox payload fields plus `ok`, `name`, optional `pin_reset`. Failure: `401` wrong PIN.

## Inbox

`GET /v1/inbox` — default **8** messages (oldest-first within page), `limit` 1–16, optional `before_seq` for older pages. Response includes `has_more`, `next_before_seq`, `total_messages`.

Firmware today loads inbox without paging older pages (spec notes same limitation).

## Message send (device)

### Multipart (firmware today)

`POST /v1/messages` — `multipart/form-data`:

- `kind=audio`
- `to_user_id` or `broadcast=true`
- `blob` — WAV file
- optional `sketch` (FLSK1), `client_message_id` (UUID)

Response: `{ "messages": [ { "seq", "kind", "from", "to", "broadcast_id", "message_id" } ] }`.

Idempotent retry with same `client_message_id` returns same logical message (`GET /v1/outgoing/{uuid}` receipt).

### JSON + PCM chunks (server only today)

`POST /v1/messages` — `application/json` `family-message/1` with `audio.codec=pcm_s16le`, 16 kHz mono.

Then per [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md):

- `PUT /v1/messages/{id}/audio/{sequence}` with chunk hash/timing headers
- `GET /v1/messages/{id}/upload` — sender status
- `POST /v1/messages/{id}/complete` — assemble WAV, fan-out inbox
- `GET /v1/messages/{id}/audio` — recipient stream (until complete, sender gets 404 on recipient routes)

Limits documented in [`demos/server/v1_product/README.md`](../../demos/server/v1_product/README.md) (chunk size, duration, body cap).

## Playback and state

| Action | Route |
|--------|-------|
| Carousel focus | `PUT /v1/session/view` `{ "seq" }` |
| Mark read + position | `PUT /v1/messages/{seq}/read` `{ "position_ms" }` |
| Scrub only | `PUT /v1/messages/{seq}/position` |
| Download audio | `GET /v1/messages/{seq}/blob` (supports `Range`) |
| Profile | `PUT /v1/profile` `{ avatar_slot, accent_hex, autoplay_new }` |

## WebSocket

1. Client connects to `/v1/ws`.
2. Sends `{"type":"hello","token":"<endpoint_token>"}`.
3. Server replies `{"type":"hello_ok","endpoint_id":"..."}`.
4. Server may send inbox notifications after commits (payload type `inbox` in `ws.py`).

## Admin

| Route | Body |
|-------|------|
| `POST /v1/admin/login` | username + web password |
| `POST /v1/admin/pin-reset` | user_id + new pin |
| `POST /v1/admin/welcome` | multipart WAV → system First Message fan-out |
| `POST /v1/admin/messages` | same shape as device multipart send |

Admin tokens expire after 1 hour (in-memory).

## Verification

Automated: `demos/server/v1_product/tests/test_api.py` (HTTP + WS hello), `test_archive.py` (chunk + multipart persistence).

Firmware protocol usage is not covered by CI unit tests; rely on device checklist and `make check-v1-parity` for timing only.
