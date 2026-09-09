# Server demos — plan

Throwaway-feeling scripts, kept as the seed of the real server. This tree owns the **host**: protocol, persistence, and a pair of **device twins** that pretend to be the boxes.

Hardware today: this Mac is the server. One BOX-3 is on USB; a second kit exists. Server demos **do not wait** on firmware. Python twins speak the same HTTP/WebSocket contract the boxes will use. Whether the kit can actually record, play, PTT, and do that on Wi-Fi is [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) — island hardware apps that can run in parallel.

## What we are proving

Four capabilities, in this order. Each has a discrete script and a pass/fail check. Later server code should **import** these modules, not rewrite the ideas.

| # | Capability | Why it exists |
|---|---|---|
| 1 | Auth | Known devices, manual pairing, no public signup |
| 2 | Heartbeat / presence | A user can tell whether sending or calling is possible |
| 3 | Async messages | Text + audio files, store-and-forward, server-owned playhead |
| 4 | Live hangout | Signaling + a two-way media relay through this Mac |

Photos are the same blob path as audio with a different `content_type`. They are not a separate demo.

## Architecture (locked for demos)

```
device A (BOX-3 or Python twin)
        \  HTTPS (files, cursor, auth)
         \ WebSocket (presence, alerts, hangout)
          -->  server on this Mac  <--  same contract
         /                         /
        /  HTTPS + WebSocket      /
device B (BOX-3, twin, or later the parent phone page)
```

**Peers, not “parent vs child.”** Every **endpoint** is an `endpoint_id` plus a bearer token. **Users** (people) have inboxes and PINs; they sign in on endpoints. The web `/app` is another client for the same user accounts.

**Product v1** ([`plans/v1-product-spec.md`](plans/v1-product-spec.md)): a **hangout** with users and endpoints; per-user inbox; 1:1 and broadcast audio; Lynn’s box + web admin. Island demos still use legacy `peer` 1:1 YAML until the server grows hangout routes.

### Why the server stays in the middle for calls

The boxes should **not** stream audio to each other after a handshake.

- ESP32-S3 has no practical WebRTC/ICE stack for this product. NAT traversal (other house ↔ your phone on cellular) will fail or be a science project.
- Even on this LAN, DHCP leases move, AP isolation exists, and the production path is **not** LAN.
- Group hangout mixing is already specified as a server job ([`PHASES.md`](PHASES.md)). One media path now means we do not throw away the hangout demo later.
- Half-duplex floor control (who may talk) is a server decision, not a peer-to-peer one.

What “negotiate the network connection” means here: **signaling** (invite, ring, accept, hangup) plus **copy-through relay** of audio frames. The server does not hand device A the LAN IP of device B.

Live audio is **half-duplex push-to-talk**. Full-duplex speakerphone next to a Switch/TV will echo; that is already decided. Demo 4 may optionally copy both directions at once as a **latency measurement**, labeled as such, not as the product.

Target feel: walkie-talkie, not a phone call. On this LAN, 20 ms PCM frames through a Python process should land under ~150 ms mouth-to-ear if we do not transcode.

### Transport split

| Plane | Transport | Used for |
|---|---|---|
| Control + files | HTTP/1.1 | Pairing check, heartbeat (also), upload/download of message blobs, playhead |
| Push + live | WebSocket | Presence, “new message” alert, hangout signaling, binary audio frames |

Not MQTT (extra broker; Safari and ESP-IDF both speak HTTP/WS). Not WebRTC. Not HTTP/2 as a requirement. Blobs stay on HTTP so a large WAV is a normal POST, not a giant WebSocket message.

LAN demos run **plain HTTP**. Headers and URLs stay TLS-ready. Wrap with `scripts/dev_https.py` (`mkcert` or a throwaway cert) for iPhone and firmware **h16**; details in [`TLS.md`](TLS.md). Production still needs SNTP plus a CA bundle on the ESP32 — h16 skip-verifies on purpose.

### State the server remembers (devices forget)

Boxes lose power and RAM. The server is the source of truth for:

- Hangout registry (users, endpoints, tokens)
- Per-**user** inbox and message blobs
- Per-user session fields: `last_viewed_seq`, per-message `read`, `position_ms`
- Presence (`last_seen`) — optional; dropped from v1 UI
- Live session state (later)

Legacy demos also track per-`device_id` **peer** and playhead until migrated.

SQLite + a `data/blobs/` directory. No Postgres, no Redis for this phase.

## Protocol sketch

Stable enough for a firmware agent to implement against. Demos implement this; the real server keeps it.

Base: `http://<this-mac>:8080/v1`

Auth: `Authorization: Bearer <token>` on every request and as the first WebSocket message (`{"type":"hello","device_id":"...","token":"..."}`). Unknown or missing token → `401`. Token is provisioned by hand (YAML on the server, NVS on the box). Never committed.

### Registry (manual)

`devices.local.yaml` (gitignored; `devices.example.yaml` committed):

```yaml
devices:
  - id: box-a
    token: change-me-a
    role: child
    peer: box-b
  - id: box-b
    token: change-me-b
    role: child
    peer: box-a
```

`peer` is the 1:1 inbox/hangout counterpart in **legacy demos**. Product v1 uses **hangout membership** and **per-user inbox** — see [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

### Product v1 routes (to implement)

| Method | Path | Purpose |
|---|---|---|
| GET | `/v1/hangout` | Members (names, ids) for recipient picker |
| GET | `/v1/me` | Session: `user_id`, inbox summary, `last_viewed_seq`, unread |
| POST | `/v1/messages` | `to_user_id` or `broadcast: true`; multipart audio |
| PUT | `/v1/messages/{seq}/read` | Mark read + optional `position_ms` |
| PUT | `/v1/session/view` | `{seq}` carousel focus |
| POST | `/v1/admin/pin-reset` | Web auth; reset user box PIN |
| POST | `/v1/admin/welcome` | Upload First Message WAV |

Legacy demo routes below remain for island flashes.

### REST (legacy demos)

| Method | Path | Purpose |
|---|---|---|
| GET | `/v1/me` | Identity, peer id, playhead, unread count, peer presence |
| POST | `/v1/heartbeat` | `{uptime_s?, rssi?}` → `{ok, peer_online, server_time}` |
| POST | `/v1/messages` | multipart: `kind=text\|audio`, optional `text`, optional `blob` (WAV) |
| GET | `/v1/messages?after=<seq>` | Inbox **after** playhead (or after the given seq). Default “what’s new” |
| GET | `/v1/messages?before=<seq>&limit=N` | Archive: older messages still inside TTL |
| GET | `/v1/messages/{id}/blob` | Download audio (or later JPEG) |
| PUT | `/v1/playhead` | `{seq}` — device reports it finished this message. Server stores it. |
| DELETE | `/v1/messages/{id}` | Optional in demos; needed before real retention UX |

Message ids are **monotonic `seq` per recipient inbox**, starting at 1. Devices ask for “after 7”, not timestamps. A reboot does `GET /v1/me` and continues from the stored playhead.

TTL for demos: **1 hour** so expiry is visible the same evening. Production retention is still an [open question](OPEN-QUESTIONS.md).

### WebSocket `/v1/ws`

Text frames are JSON envelopes `{ "type": "...", ... }`. Binary frames are hangout audio only.

| `type` | Direction | Meaning |
|---|---|---|
| `hello` | device → server | Auth (if not done via query — prefer first message) |
| `hello_ok` | server → device | `{device_id, peer_id, playhead}` |
| `presence` | server → device | `{peer_id, online, last_seen}` |
| `inbox` | server → device | `{seq, kind, from}` — alert only; body fetched over HTTP |
| `invite` / `ring` / `accept` / `reject` / `hangup` | both | Hangout signaling |
| `floor` | server → both | `{holder: device_id\|null}` |
| `floor_request` / `floor_release` | device → server | PTT |

Live audio: **PCM s16le, 16 kHz, mono**, 20 ms frames (640 bytes) as WebSocket binary, only while this device holds the floor. Server copies bytes to the peer with no codec. Opus can replace PCM later without changing signaling.

## Demo layout

One package, many entry points. Do not write six unrelated Flask files.

```
server/                     # importable; grows into the real server
  family_link/
    config.py               # YAML registry
    auth.py
    presence.py
    inbox.py                # messages, blobs, playhead, TTL
    hangout.py              # signaling + relay
    app.py                  # FastAPI app
demos/
  run_server.py             # uvicorn on :8080
  twin.py                   # one fake device (CLI)
  01_auth.py
  02_heartbeat.py
  03_messages.py
  04_cursor_archive.py
  05_hangout_signaling.py
  06_audio_relay.py
  fixtures/                 # tiny WAV, sample tokens
devices.example.yaml
data/                       # gitignored sqlite + blobs
```

Stack: Python 3.11+ (this machine has 3.14), FastAPI + uvicorn (OpenAPI falls out for the firmware agent), stdlib `sqlite3`, blobs on disk. Add those deps to `requirements.txt` when implementation starts. Device USB tools (`esptool`) stay separate.

`twin.py` is the stand-in for firmware: `--id box-a` connects, heartbeats, send/recv, PTT. Two terminals = two boxes. A later BOX-3 HTTP client should be drop-in compatible; we do not special-case “Python vs ESP”.

## Discrete demos

Run `demos/run_server.py` once. Each numbered script is a client-side proof with a printed `PASS` / `FAIL`.

### Demo 1 — Auth

**Script:** `demos/01_auth.py`

- Twin with a good token: `GET /v1/me` returns its id and peer.
- Twin with a bad token or none: `401`.
- Twin `box-a` cannot use `box-b`’s token to impersonate `box-a`.

**Reuse later:** `auth.py`, registry loader, bearer dependency.

### Demo 2 — Heartbeat and presence

**Script:** `demos/02_heartbeat.py`

- Both twins POST `/v1/heartbeat` every 2 s for ~10 s.
- Each `GET /v1/me` shows `peer_online: true`.
- Stop twin B. After a 10 s stale window, twin A sees `peer_online: false`.
- Restart twin B (simulates power loss). Presence recovers without any client-stored state.

Same facts must appear on the WebSocket as `presence` events (can be the second half of this script).

**Reuse later:** `last_seen` table, stale threshold, `/v1/me` payload the parent UI will show as “box is reachable.”

### Demo 3 — Send / receive messages (text + audio)

**Script:** `demos/03_messages.py`

- Twin A POSTs a text message. Twin B is alerted on WS (`inbox`) and GETs the body.
- Twin A POSTs `fixtures/beep.wav`. Twin B downloads the blob; byte-identical to the fixture.
- Twin B can retrieve **all unconsumed** messages in one list (burst after a gap).
- Unauthenticated upload fails.

Messages are addressed to the configured `peer`. No group inbox in v1.

**Reuse later:** multipart handler, blob store, `inbox` event, GET-by-seq.

### Demo 4 — Playhead, reboot, archive

**Script:** `demos/04_cursor_archive.py`

This is the answering-machine demo. Devices are allowed to forget everything.

1. A sends messages seq 1, 2, 3 to B.
2. B plays 1 and 2, `PUT /v1/playhead {seq: 2}`.
3. Kill twin B (no local storage).
4. New twin B process: `GET /v1/me` → playhead 2. `GET /v1/messages` (default after playhead) returns only seq 3.
5. `GET /v1/messages?before=2&limit=10` returns 1 and 2 (archive / “play older”).
6. After TTL, archive GET returns empty for those ids (script can set a 2 s TTL via env for the test).

Default fetch = **after playhead**. Archive = **explicit before/limit**. The server never requires the box to remember seq.

**Reuse later:** playhead column, TTL reaper, the exact fetch rules firmware will call on boot.

### Demo 5 — Hangout signaling (no audio yet)

**Script:** `demos/05_hangout_signaling.py`

- A sends `invite`. B receives `ring`.
- B `accept`. Both get `session_start` and `floor: null`.
- A `floor_request` → both see `floor: box-a`. B’s `floor_request` is denied while A holds it.
- A `floor_release` → `floor: null`.
- A `hangup` → both return to idle. A second invite works.
- If B never accepts, invite times out (use a short timeout in the demo).

**Reuse later:** session state machine. Firmware can implement buttons against these events before codecs work.

### Demo 6 — Audio relay (the call)

**Script:** `demos/06_audio_relay.py`

- Complete demo 5’s accept path.
- A holds floor and sends N frames of PCM (fixture or Mac mic).
- B writes received bytes to `data/relay-capture.wav`.
- Pass: capture matches sent payload (or SNR/length check if using live mic).
- Print **relay latency** (server timestamps on first/last frame). On this LAN, aim for a printed one-way hop under 50 ms excluding the codec.

Optional flag `--full-duplex-measure`: copy both ways with no floor, for a number, not a product behavior.

**Reuse later:** binary WS frame copy loop. No mixer yet (phase 3). Do not transcode.

### Not in this series (on purpose)

- PIN, display, codecs, Wi-Fi, and flashing the BOX-3 — those are [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md).
- Parent Safari PWA (same API; different client).
- TLS, tunnels, iPhone push ([`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)).
- USB camera photos.

When firmware has even a tiny HTTP client, point it at server demos 1–2 (device h07). Hangout on the box is device h11 against server demo 6.

Device UI fixtures (not numbered 01–06): `h18_playback` serves a catalog (generated melody + imported inbox voice clips) for firmware **h18** and a 320×240 web twin at `/box/`. `GET /demo/h18/message?i=N` wraps. Inbox **list + detail** is firmware **h19** (no host). Do not fold those into 03_messages.

Two-box open line (Mazi / Arlo, names in `devices.example.yaml`): `h20_presence` (mute as `available` on `/v1/heartbeat`), `h21_talk` (full-duplex PCM copy, no floor; JSON `status` / `peer_status` for the friend’s mute line), `h22_diary` (dated WAV chunks on `POST /v1/diary`), `h26_draw` (JSON `stroke` / `clear` copy on `/v1/ws`, **high impact**), `h27_sketch` (timed stroke clip on `POST /v1/sketches`, inbox GET + replay, **high impact**). Firmware **h20–h22** / **h26** / **h27**. Plan: [`plans/mazi-arlo-open-line.md`](plans/mazi-arlo-open-line.md). Not the product hangout.

### v1 product host

**Script:** `demos/server/v1_product/` — `make v1-server` / `make demo-v1`.

Hangout users + endpoints (`hangout.example.yaml`), per-user inbox, broadcast, First Message, web admin `/app/v1.html`, box twin `/box/`, WS `/v1/ws`. Firmware **x02**. Contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

## How this maps to a later server

Keep `server/family_link/` as the application. Promotion to “real” is operational, not a rewrite:

- Bind `0.0.0.0` + TLS in front (Caddy / mkcert).
- Same SQLite until it hurts; blobs can move to a disk volume.
- Parent page is another twin with `role: parent`.
- Firmware replaces `twin.py` for each box.
- Stale window, TTL, and hangout timeout become config, not new code.

If a demo module is messy, clean it **in place** before adding the next demo. Do not start `server2/`.

## Suggested implementation order

When building (separate step from this plan):

1. Package skeleton, example YAML, `run_server.py` health check.
2. Demo 1 → 2 → 3 → 4. Stop if playhead/reboot is wrong; everything else depends on it.
3. Demo 5, then 6.
4. Hand `GET /docs` (FastAPI OpenAPI) and this file to the firmware agent.

Server demos 1–6 do not wait on a box. Device island demos ([`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) h01–h05) can run on the USB kit in parallel. Pair the real box with `twin.py` for h07–h11; do not wait on the second kit.

## Decisions this plan makes (so we do not silently reverse them)

- Server **relays** live audio; it does not introduce peers for P2P.
- Hangout is **half-duplex PTT** with server floor control.
- Playhead lives **only** on the server.
- Two boxes in demos are **two peers**; the phone is a third peer later, not a different protocol.
- Python twins are first-class; hardware is a compatibility target, not a blocker.
