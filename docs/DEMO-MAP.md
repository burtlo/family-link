# Demo map — what exists, what audio does, what to build next

One BOX-3 on USB is enough for parent-page tryouts. Two kits (Mazi ↔ Arlo) use **h20–h22**, **h26**, and **h27**; see [`TWO-BOX.md`](TWO-BOX.md). Discrete demos stay one-job; this file is how they fit the product.

## Audio: different jobs (do not collapse them)

| Job | Live? | Direction | How | Demo that proves it |
|---|---|---|---|---|
| Voicemail **from** the box | No | Child → parent inbox | Hold **red circle**, record, `POST /v1/messages` | **h08** + server **03** (tested; trim leading click) |
| Voicemail **from** the box (toggle + idle stop) | No | Child → parent inbox | Tap **red circle** on/off; auto-stop after 30 s quiet | **h23** + server **03** |
| Voicemail **to** the box | No | Parent → child inbox | `POST` blob, box `GET` + play | **h09** + server **03** |
| Hangout **from** the box | Yes, 20 ms PCM | Child → peer while mute held | WS binary, server copies to peer | **h11** / **h12** + server **06** |
| Hangout **to** the box | Yes, 20 ms PCM | Peer → child speaker | Same WS, other peer holds floor | **h11** / **h12** + `twin_peer.py` / `demos/parent/live_ptt.py` |
| Friend **talk** (two boxes) | Yes, 20 ms PCM | Both ways while unmuted | WS copy, **no floor** | **h21** + `h21_talk` (Mazi ↔ Arlo) |
| Diary **from** the box | No (1 s files) | Child → server journal | Unmute, POST WAV chunks | **h22** + `h22_diary` |
| Chipmunk memo (local) | No | Box only | Hold **red circle**, play back pitched up | **h25** |

There is **no** “raw TCP/UDP stream to the ESP32” and **no** WebRTC. Live product hangout is **WebSocket binary frames** (16 kHz s16le mono, 640 bytes / 20 ms) **only while that side holds the floor**. **h21** is the exception: both sides may send at once while unmuted.

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
| BOX-3 ↔ BOX-3 | **Written** — flash **h20** / **h21** / **h22** / **h26** / **h27** (Mazi=`box-a`, Arlo=`box-b`) or **h11** / **x01**. See [`TWO-BOX.md`](TWO-BOX.md). |
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
| h17 | Button panel: live down/up, analog mic mute, chirps (circle press+release). Tested 2026-08-23 |
| h18 | Playback screen: GET catalog message (sender/time/url/length/position/read), play/pause, stream WAV, ROOMVOL slider, Boot cycles clips. Host: `h18_playback`. Web twin: `/box/` (same catalog, 320×240 LCD + bezel keys) |
| h19 | Scrollable message list + slide transition to detail and back. Client-side; same record shape as h18 |
| h20 | Mute latch as open/away. Heartbeat `{available}`; response is the friend’s state (Mazi ↔ Arlo). Host: `h20_presence` |
| h21 | Live talk while unmuted: full-duplex PCM copy-through, mute relay on the friend card, h18 ROOMVOL slider. Host: `h21_talk`. **Desk success 2026-08-24** (two kits) |
| h22 | Diary: record while unmuted, POST 1 s WAV chunks, server stamps UTC. Host: `h22_diary` |
| h23 | Toggle record on red circle; 30 s without speech auto-stops and uploads (h08 server) |
| h24 | Heartbeat + device event log batch on each beat; host writes `data/h24_device_log/` |
| h25 | Hold red circle, record a short memo, play it back as a chipmunk (pitch 5/3). No Wi-Fi |
| h26 | Shared drawing: touch on one glass, coordinates through the server, ink on the other (Mazi ↔ Arlo). Host: `h26_draw`. **High impact** (desk 2026-08-28) |
| h27 | Drawing note: record a timed sketch, POST to the friend, replay at the same speed. Host: `h27_sketch`. **High impact** (desk 2026-08-28) |
| h28 | Short LCD clip: 5 s film at 12 fps (title / bouncing ball / end), RGB565 blit, loops. No Wi-Fi. Not a product path. |
| x01 | Earlier glue shell: locked / PIN / inbox / record / hangout against combined |
| x02 | v1 product shell: hangout roster, PIN, carousel, record, WS inbox. Firmware: **`firmware/v1/`** modules + `demos/x02_product_shell.c` (1-line flash shim). Host: `make v1-server`. Timing: `make v1-timing`; parity: `make check-v1-parity` |
| p01–p09 | Geometric face, blink, moods, SFX, pet loop, notice, talk-or-freeze |
| p10–p11 | Packed portrait + greeting (persona pipeline) |

### Parent likeness (`make demos-persona`)

Capture / cut / record / ideas / pack. No family media required.

### Parent Mac tools (`demos/parent/`)

CLI stand-ins (`live_ptt.py`, `send_voicemail.py`, `send_photo.py`) plus the **parent page** at `demos/parent/web/` (combined mounts it at `/app/`). Same tokens as a device (`box-b` / `change-me-b` talking to a kit flashed as `box-a`).

## Product features vs demos

Do **not** start by merging all `.c` files. Import the helper that passed. **x02** is the v1 product binary (x01 is the earlier combined-host glue).

| Product mode | Proven by | Still missing |
|---|---|---|
| Locked idle + count | h02, p08, **x01** | Dim schedule / quiet hours as a household rule |
| PIN then content | h03, **h15**, **x01** | Polish: which look after unlock |
| Record out | h04, h05, h08, **x01** | **Minutes-long diary** (chunked upload during hold, server stitch); upload retry + outbox; **trim leading hardware click**; raise **h09** inbound blob cap for long parent clips |
| Play inbound clip | h09, p09, **x01** | Playback **screen** (h18); inbox **list + detail** (h19) |
| Live you↔box | h11, h12, parent `live_ptt.py`, **`/app`**, **x01** | iPhone mic needs HTTPS ([`TLS.md`](TLS.md)) |
| Face | p01–p07, p10 | Which look ships (geometry vs packed photo) |
| Your voice greeting | p11 + persona pack | Not UI chirps (p06 stays non-speech) |
| Photos you → child | combined `/preview` + **h13** + `/app` / `send_photo.py` | Device never decodes JPEG (server → 320×240 RGB565) |
| Photos child → you | hardware: USB cam on dock | UVC capture demo (deferred) |
| Heartbeat LED / “dad reachable” | server 02, **h14**, **x01** | — |
| New-mail without polling | server 03 / combined WS `inbox`, **h14**, **x01** | — |
| Power loss | h10 | NVS Wi-Fi+token only; never playhead |
| TLS | h16 skip-verify + `scripts/dev_https.py` | Production: SNTP + CA; iPhone needs a cert it trusts ([`TLS.md`](TLS.md)) |
| Two kids / mix | phase 3; **h20–h22** are a two-box friend-line experiment | Server mixer for product group hangout; not on the ESP32 |
| Live shared drawing | **h26** | **High impact** (desk 2026-08-28). Import into friend line / later parent glass |
| Drawing note | **h27** | **High impact** (desk 2026-08-28). Store-and-forward sketch like a voicemail; parent page playback still missing |
| One product app | **x02** (x01 is prior glue) | Remote TLS + Tailscale before kids’ boxes ship |

## Suggested next (highest leverage first)

1. **Desk tryout** — `make v1-server`, admin `http://MAC:8080/app/v1.html`, `make check-v1-parity`, flash **x02** (`firmware/v1/` + shim).
2. **iPhone mic** — `python scripts/dev_https.py --extra-name LAN_IP`, open `https://…:8443/app/`. Flash **h16** to prove the box can speak HTTPS (set `DEMO_SERVER_PORT` 8443 for that flash only).
3. **Second kit** — Mazi/Arlo open line: [`TWO-BOX.md`](TWO-BOX.md) + **h20** then **h21** then **h22**. Product hangout remains two **x01** (or **h11**) binaries, `box-a` / `box-b`.
4. **USB camera** — only if child→you photos are in v1.

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
