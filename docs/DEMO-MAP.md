# Demo map — what exists, what audio does, what to build next

One BOX-3 on USB. The other peer is the **parent page** (`/app/`) or a **Python twin**. Discrete demos stay one-job; this file is how they fit the product.

## Audio: four different jobs (do not collapse them)

| Job | Live? | Direction | How | Demo that proves it |
|---|---|---|---|---|
| Voicemail **from** the box | No | Child → parent inbox | Hold mute, record, `POST /v1/messages` | **h08** + server **03** |
| Voicemail **to** the box | No | Parent → child inbox | `POST` blob, box `GET` + play | **h09** + server **03** |
| Hangout **from** the box | Yes, 20 ms PCM | Child → peer while mute held | WS binary, server copies to peer | **h11** / **h12** + server **06** |
| Hangout **to** the box | Yes, 20 ms PCM | Peer → child speaker | Same WS, other peer holds floor | **h11** / **h12** + `twin_peer.py` / `demos/parent/live_ptt.py` |

There is **no** “raw TCP/UDP stream to the ESP32” and **no** WebRTC. Live audio is **WebSocket binary frames** (16 kHz s16le mono, 640 bytes / 20 ms) **only while that side holds the floor**.

```
box mic --(hold mute)--> WS binary --> server relay --> peer speaker
peer mic --(floor)------> WS binary --> server relay --> box speaker
```

Store-and-forward is a **file**, not a stream:

```
box mic --(hold, release)--> HTTP POST WAV --> disk --> GET blob --> box speaker
```

## Device to device — possible, but not peer-to-peer

**Yes, two devices can talk, but only through the server you run.** The boxes never learn each other’s IP. That is locked in [`SERVER-DEMOS.md`](SERVER-DEMOS.md): ESP32 has no useful WebRTC/ICE here; production is not even the same LAN (other-house Wi-Fi ↔ phone on cellular); group mixing is already a server job.

What “two devices” means today:

| Pair | Status |
|---|---|
| Python twin A ↔ twin B | **Done** — `make demo-relay` (server **06** client) |
| One BOX-3 ↔ Mac twin | **Written** — flash **h11**, run `demos/parent/live_ptt.py` (or `twin_peer.py`) |
| BOX-3 ↔ BOX-3 | **Same firmware + API.** Flash **h11** / **x01** on both, different `DEMO_DEVICE_ID` / tokens (`box-a` and `box-b`). Not tried on a second kit yet. |
| BOX-3 ↔ iPhone | **Page is in the tree** — combined serves `demos/parent/web/` at `/app/`. Mic needs HTTPS ([`TLS.md`](TLS.md)); text / photo / WAV file work on HTTP |

v1 **product** hangout is **you (phone) + one child box**, not kid-to-kid. Two boxes in `devices.example.yaml` are so the protocol can be exercised without a phone. Inboxes stay per child.

## Inventory (built in this tree)

### Host protocol (`demos/server/`, `make demos-server`)

Numbered **01–06** stay island proofs (`make demos-server`). Glue is **`make demo-combined`**: auth + heartbeat + messages (text / WAV / JPEG) + RGB565 `/preview` + playhead + one WS (inbox JSON + hangout binary) + static `/app`.

| Id | Proves |
|---|---|
| 01_auth | Bearer `GET /v1/me` |
| 02_heartbeat | Presence / stale / recover |
| 03_messages | Text + WAV store-and-forward, WS `inbox` alert |
| 04_cursor_archive | Playhead on server, archive after “reboot” |
| 05_hangout_signaling | Invite / ring / accept / floor / hangup (no audio) |
| 06_audio_relay | PCM copy-through + `twin_peer.py` for a real box |
| combined | One host for the box + parent page (`python -m demos.server.combined.server --host 0.0.0.0 --port 8080`) |

### Device firmware (`make flash DEMO=…`)

| Id | Proves |
|---|---|
| h01–h06 | BSP, count screen, PIN, mute PTT, loopback, Wi-Fi |
| h07–h10 | HTTP me, record-upload, download-play, playhead after reset |
| h11–h12 | Live PTT through server; h12 paints LIVE while PCM moves |
| h13–h15 | Photo preview, heartbeat + inbox WS, text after PIN |
| h16 | HTTPS GET /v1/me (skip-verify LAN). Host: `scripts/dev_https.py`. [`TLS.md`](TLS.md) |
| h17 | Cross-Wi-Fi heartbeat. Host: `make demo-cross-heartbeat` |
| x01 | Product shell: locked / PIN / inbox / record / hangout against combined |
| p01–p09 | Geometric face, blink, moods, SFX, pet loop, notice, talk-or-freeze |
| p10–p11 | Packed portrait + greeting (persona pipeline) |

### Parent likeness (`make demos-persona`)

Capture / cut / record / ideas / pack. No family media required.

### Parent Mac tools (`demos/parent/`)

CLI stand-ins (`live_ptt.py`, `send_voicemail.py`, `send_photo.py`) plus the **parent page** at `demos/parent/web/` (combined mounts it at `/app/`). Same tokens as a device (`box-b` / `change-me-b` talking to a kit flashed as `box-a`).

## Product features vs demos

Do **not** start by merging all `.c` files. Import the helper that passed. **x01** is the first product-shaped binary.

| Product mode | Proven by | Still missing |
|---|---|---|
| Locked idle + count | h02, p08, **x01** | Dim schedule / quiet hours as a household rule |
| PIN then content | h03, **h15**, **x01** | Polish: which look after unlock |
| Record out | h04, h05, h08, **x01** | Upload retry, “sent” face |
| Play inbound clip | h09, p09, **x01** | — |
| Live you↔box | h11, h12, parent `live_ptt.py`, **`/app`**, **x01** | iPhone mic needs HTTPS ([`TLS.md`](TLS.md)) |
| Face | p01–p07, p10 | Which look ships (geometry vs packed photo) |
| Your voice greeting | p11 + persona pack | Not UI chirps (p06 stays non-speech) |
| Photos you → child | combined `/preview` + **h13** + `/app` / `send_photo.py` | Device never decodes JPEG (server → 320×240 RGB565) |
| Photos child → you | hardware: USB cam on dock | UVC capture demo (deferred) |
| Heartbeat LED / “dad reachable” | server 02, **h14**, **x01** | — |
| New-mail without polling | server 03 / combined WS `inbox`, **h14**, **x01** | — |
| Power loss | h10 | NVS Wi-Fi+token only; never playhead |
| TLS | h16 skip-verify + `scripts/dev_https.py` | Production: SNTP + CA; iPhone needs a cert it trusts ([`TLS.md`](TLS.md)) |
| Two kids / mix | phase 3 | Server mixer; not on the ESP32 |
| One product app | **x01** | Household SSID, always-on dock, second kit |

## Suggested next (highest leverage first)

1. **Desk tryout** — combined on `0.0.0.0:8080`, parent `http://MAC:8080/app/`, flash **x01** (or **h11** + **h13** pieces).
2. **Cross-Wi-Fi heartbeat** — `make demo-cross-heartbeat`. Box on 2.4 GHz, Mac on another SSID. Isolated networks: `TUNNEL=1`.
3. **iPhone mic** — `python scripts/dev_https.py --extra-name LAN_IP`, open `https://…:8443/app/`. Flash **h16** to prove the box can speak HTTPS (set `DEMO_SERVER_PORT` 8443 for that flash only).
4. **Second kit** — two **x01** (or **h11**) binaries, `box-a` / `box-b`. Protocol already allows it.
5. **USB camera** — only if child→you photos are in v1.

## How to explore live audio this week

```
# terminal 1 — glue host (REST + hangout WS + parent /app)
python -m demos.server.combined.server --host 0.0.0.0 --port 8080

# terminal 2 (you, as box-b) — Mac page, or the CLI twin:
open http://localhost:8080/app/
# python demos/parent/live_ptt.py --base-url http://MAC_LAN_IP:8080

# then flash the box (USB-C on the box)
make flash DEMO=x01
# or make flash DEMO=h11
```

Start the Python side first so it can accept the box’s invite. Hold **mute** on the box to talk back. The CLI twin writes `data/parent/from-box.wav`.
