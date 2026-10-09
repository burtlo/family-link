# Parent web page

You, on a phone or this Mac, talking to one child **BOX-3** through the combined
server. Same protocol as the box: bearer token, HTTP files, WebSocket hangout.
Default identity is `box-b` (peer of a kit flashed as `box-a`).

No build step. Vanilla HTML / CSS / JS.

## Open it

Combined server mounts this folder at **`/app/`**:

```
# from the repo root, venv on
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
```

On this Mac:

```
open http://localhost:8080/app/
```

From an iPhone on the same LAN, use the Mac’s Wi-Fi address (not `localhost`):

```
ipconfig getifaddr en0
# then Safari → http://THAT_IP:8080/app/
```

Settings (top row) default to `device_id=box-b`, `token=change-me-b`,
`origin=http://localhost:8080`. They are stored in LocalStorage under `token`,
`device_id`, and `origin`. When this page is already served by the combined
server, requests use **relative** `/v1/…` URLs even if origin still says
localhost.

## If `/app/` 404s

Combined must be the process on that port (`python -m demos.server.combined.server`),
not an island 01–06 server. You can also open `index.html` from disk (`file://`)
and keep the origin field pointed at `http://MAC:8080`.

## Box side

Flash the kit as **`box-a`** (`DEMO_DEVICE_ID` / `DEMO_DEVICE_TOKEN` in
`firmware/secrets.h`, matching `devices.example.yaml`).

```
make flash DEMO=x01          # product shell (locked / PIN / inbox / hangout)
make flash DEMO=h11          # live PTT only
```

USB-C on the **box**, not the dock. Combined server must be listening on
`0.0.0.0:8080` so the box can reach this Mac. Hold mute on the box to talk;
the page’s hold-to-talk is the parent side of the same half-duplex floor.

## What the page does

Connect → `GET /v1/me`, heartbeat every 4s, WebSocket `/v1/ws`. Send text /
voicemail / photo over `POST /v1/messages`. Inbox is **your** mail from the
box. Hangout is invite / accept / hold-to-talk PCM (16 kHz s16le, 20 ms
frames). No WebRTC.

## Microphone (getUserMedia)

Safari and Chrome only open the mic in a **secure context**: `https://`, or
`http://localhost` / `127.0.0.1` on this Mac. An iPhone loading
`http://192.168.x.x:8080/app/` **cannot** capture audio until you put TLS in
front (mkcert or Caddy; the combined server accepts `--ssl-certfile` /
`--ssl-keyfile`). Text, camera-roll photos, and a WAV file still work on
plain HTTP.

Live hold-to-talk and in-page voicemail recording need the mic. If it fails,
the page says so and leaves file inputs enabled.
