# V1 product server

Implementation: `demos/server/v1_product/` (FastAPI + Uvicorn). Runbook: [`demos/server/v1_product/README.md`](../../demos/server/v1_product/README.md).

## Process model

- **One process per archive** — `MessageArchive` holds a `.server.lock`; a second process opening the same `FAMILY_LINK_MESSAGE_STORE` fails.
- **No reload** against production household data (README warning).
- **Lifespan** — logs archive path and directory-fsync capability; closes archive on shutdown.

## Configuration

| Variable | Effect |
|----------|--------|
| `FAMILY_LINK_DATA_DIR` | Default OS app-support path for data |
| `FAMILY_LINK_MESSAGE_STORE` | Override archive root (`message_store/`) |
| `FAMILY_LINK_HANGOUT_REGISTRY` | Private hangout YAML (tokens, PINs, web passwords) |
| `FAMILY_LINK_START_FRESH=1` | Allow empty archive when checkout legacy media exists |
| `FAMILY_LINK_FREE_DISK_FLOOR_BYTES` | Reject writes with HTTP 507 when free space low |

Legacy `blobs/` / `shared/` under data dir **block startup** until metadata migration exists.

## HTTP surface (summary)

| Area | Routes |
|------|--------|
| Hangout | `GET /v1/hangout` |
| Session | `POST /v1/session/login`, `PUT /v1/profile`, `PUT /v1/session/view` |
| Inbox | `GET /v1/inbox?limit=&before_seq=` |
| Playback state | `PUT /v1/messages/{seq}/read`, `PUT /v1/messages/{seq}/position` |
| Media | `GET /v1/messages/{seq}/blob`, `GET /v1/messages/{seq}/sketch` (HTTP Range on blobs) |
| Send (device) | `POST /v1/messages` — multipart WAV and/or JSON PCM create |
| Send receipt | `GET /v1/outgoing/{client_message_id}` |
| PCM chunks | `PUT .../audio/{seq}`, `GET .../upload`, `POST .../complete`, `GET .../audio` — see [protocols](protocols.md) |
| Admin | `POST /v1/admin/login`, `POST /v1/admin/pin-reset`, `POST /v1/admin/welcome`, `POST /v1/admin/messages` |
| Static | `/` → redirect `/box/`; `/app` → `demos/parent/web`; `/box` → product web twin |

Auth: endpoint Bearer on almost all `/v1/*`; admin Bearer on `/v1/admin/*` except login.

## Storage layout

Committed messages live under `messages/<year>/<month>/<uuid>/` with `manifest.json`, `media.wav`, optional `sketch.flsk`, `complete` marker. In-flight PCM uploads use `incoming/<uuid>/`. Mutable per-user state (read flags, scrub positions, profiles, PIN overrides) in `state/user-state.sqlite3`.

Details: [`demos/server/v1_product/README.md`](../../demos/server/v1_product/README.md), [`SERVER-MESSAGE-STORAGE.md`](../SERVER-MESSAGE-STORAGE.md).

## Core modules

| Module | Role |
|--------|------|
| `server.py` | FastAPI app, route wiring, static mounts |
| `archive.py` | Disk archive, chunk assembly, hash verification at startup |
| `chunk_api.py` | MESSAGE-PROTOCOL chunk routes |
| `body_limit.py` | Multipart body size and concurrent upload slots |
| `ws.py` | Endpoint WebSocket registry and `inbox` push |
| `demos/server/_shared/user_mailbox.py` | Inbox model, PIN verify, broadcast, First Message seed, archive bridge |

## WebSocket

- `GET /v1/ws` — first message `{"type":"hello","token":"<endpoint_token>"}` → `hello_ok`.
- On new inbox row after commit, server calls `notify_inbox` (bounded timeout; send path does not block indefinitely on stalled WS).

## Verification

```bash
python -m unittest discover -s demos/server/v1_product/tests -v
```

Covers restart persistence, chunk resume, multipart idempotence, inbox paging, admin send, range requests, corrupt archive fail-closed, and notification timeout behavior.
