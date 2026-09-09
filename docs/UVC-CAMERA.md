# Child → parent photos and video (USB camera)

The BOX-3 has **no onboard camera**. Anything the child sends as a picture (or, later, a short moving clip) needs a **UVC/MJPEG** camera on the **dock USB-A** port.

v1 product is still **snapshots, not a camera roll, not a video call** ([`REQUIREMENTS.md`](REQUIREMENTS.md)). Hangout and voicemail do not wait on this port. This file is how the dock camera actually works, what “streaming” can mean on this SoC, and what to refuse.

Until a camera is on the desk:

- **You → child** photos are the path that matters: parent page or `demos/parent/send_photo.py`, then firmware **h13** / product shell. The box never decodes JPEG; the server downscales to **320×240 RGB565** (`153 600` bytes).
- Simulating a child snap from this Mac: POST an image **as `box-a`** (token `change-me-a`) so it lands in **box-b**’s inbox. That proves the protocol, not the dock.

Do not block hangout or voicemail on UVC.

## What the dock USB-A actually is

| Fact | Value |
|---|---|
| Port | BOX-3-**DOCK** USB-A (host). Dock USB-C is **5 V in only**. |
| Speed | **USB 1.1 full-speed** (12 Mbps theoretical). ESP32-S3 USB-OTG is not high-speed. |
| Camera class | **UVC** that can emit **MJPEG**. YUY2-only or H.264-only cams will not work with Espressif’s `usb_stream` host. |
| Isochronous cap | Interface `wMaxPacketSize` **≤ 512**, stream **< 4 Mbps** (~500 KB/s). Typical: **480×320 @ 15 fps**. |
| Bulk cap | Stream **< 8.8 Mbps** (~1100 KB/s). Typical: **800×480 @ 15 fps**. |
| Marketing “720p” | The **sensor** may be 720p. A sustained 720p30 MJPEG pipe is **not** what full-speed USB can host. 720p is a lucky still, or a low-fps bulk mode, if the cam even exposes a FS descriptor. |
| Pins | USB D+ / D− are **GPIO20 / GPIO19**. Those are also Pmod1 IO2 / IO6 — leave them alone ([`SPEAKER.md`](SPEAKER.md)). |
| Power / flash | Camera on dock USB-A. Keep the dock USB-C plugged in for 5 V. **Flash through the box USB-C with the camera unplugged.** |

Espressif’s BOX-3 example is [`usb_camera_lcd_display`](https://github.com/espressif/esp-box/tree/master/examples/usb_camera_lcd_display) (`espressif/usb_stream`). Desk reports: many consumer webcams are USB 2.0 high-speed only and never enumerate; a few (including a Logitech C930e in one thread) expose a full-speed MJPEG alt-setting and do. Buy a cam advertised for ESP32-S3 UVC, or test before shipping a kit to the other house.

The LCD is **320×240 SPI**. A 720p frame is for the **parent phone**, not for the child’s screen. Local preview must be JPEG-decoded and scaled down (Espressif uses `tjpgd`). The send path should keep the **JPEG bytes** and let the server / phone display them — same split as h13 in reverse.

## Privacy (same rule as the mic)

The box sits in someone else’s house. A USB camera that is live whenever the dock has power is worse than a button-gated mic.

- Capture **only while a control is held** (red circle, mute, or a later Pmod arcade button). No always-on viewfinder to the network.
- Locked idle may show a count, never a live picture.
- Do not enable a “baby monitor” mode as a shortcut to streaming.

## Ranked ways to add a camera

Listed **in the order you would build them**, from simplest to heaviest. Each section title is the name to use in conversation — there are no separate codes like “C1.”

Firmware copies the same discipline as audio: one new job per demo, import the helper that passed, do not rewrite I2S to “make room.” USB host is a **new** stack (`usb_stream` + USB host). It shares the CPU, PSRAM, and 2.4 GHz radio with hangout. Prove it **off** the live call first.

| Build order | Approach | One-line summary |
|---|---|---|
| 1 | **One still (POST)** | Single JPEG upload; no viewfinder |
| 2 | **Viewfinder, then still** | LCD preview, then same POST as #1 |
| 3 | **Short clip (store-and-forward)** | Hold to record MJPEG; POST a file |
| 4 | **Live hold-to-show (WebSocket)** | JPEG frames while button held |
| — | **Skip on this kit** | H.264, WebRTC, always-on, etc. |

### One still (POST) — start here

```
UVC MJPEG frame  -->  HTTP POST /v1/messages  kind=image  -->  server disk
                                                              parent phone displays JPEG
```

- Grab **one** JPEG from `usb_stream` (hold to arm, release to send — or tap to freeze).
- No JPEG decoder on the sending box. No LCD viewfinder required.
- Combined host already stores `kind=image` and builds a 320×240 RGB565 `/preview` for the **other** box.
- Size: a 640×480 indoor JPEG is typically **30–80 KB**; a 320×240 still **8–20 KB**. PSRAM is not the limit. Wi-Fi upload of one still is trivial on LAN; other-house uplink is still fine.

**Good for:** the requirement “child → you small photo.” Matches this file’s original plan.

**Firmware:** USB host init, one frame callback, existing `POST` multipart from **h08**. New island/protocol demo (not in the h-series yet). `sdkconfig` factory is **1.5 MB** today — USB host + `usb_stream` may force a custom table with a **2–3 MB** app slot ([`STORAGE.md`](STORAGE.md), [`HARDWARE.md`](HARDWARE.md)).

### Viewfinder, then still

Espressif’s LCD example: MJPEG → `tjpgd` → RGB565 → ILI9341. Child sees what they are about to send. Red circle freezes; **One still (POST)** sends the **JPEG**, not the RGB565.

- Needs a JPEG decoder in the binary (h13 deliberately has none).
- USB + SPI LCD at once is the same class of risk as **h12** (LCD + I2S + Wi-Fi). If the viewfinder glitches the hangout, the product viewfinder is **off during a call**.
- Do not blit 720p RGB565 (that would be ~1.8 MB/frame). Decode to **320×240** for the glass.

**Good for:** Minecraft-desk “did it point at my build.” Extra to **One still (POST)**, not a replacement.

### Short clip (store-and-forward)

Hold: append MJPEG frames into **PSRAM**. Release: `POST` a blob (`kind=video` — **not in the protocol today**). Server keeps the file; playhead stays on the server.

| Clip | Rough size in PSRAM |
|---|---|
| 10 s · 320×240 · ~10 fps · ~12 KB/frame | **~1.2 MB** |
| 10 s · 640×480 · ~10 fps · ~28 KB/frame | **~2.8 MB** |
| 30 s · 320×240 · 10 fps | **~3.6 MB** |

16 MB PSRAM can stage that. SPI flash does not need to hold the take. Parent playback: browsers are bad at raw MJPEG; the host should transcode (ffmpeg → mp4) or send a short JPEG sequence. That is host work, not ESP work.

**Good for:** “look at this for ten seconds” without a live session. Heavier than **One still (POST)** (new kind, parent player, bigger POSTs on a 2.4 GHz uplink). Do not start this before a still upload is boring.

### Live hold-to-show (WebSocket)

Same shape as hangout audio, different payload:

```
box UVC --(hold)--> WS binary JPEG frames --> server copy --> parent <img> (or MSE)
```

No WebRTC. No peer-to-peer. Boxes still never learn each other’s IP.

| Mode | Approx bitrate | USB FS | Other-house uplink |
|---|---|---|---|
| 320×240 @ 5–8 fps | **0.5–1 Mbps** | Easy (isoc or bulk) | Often OK |
| 640×480 @ 10 fps | **~2.3 Mbps** | OK on bulk; tight on isoc | Needs a decent upload |
| 800×480 @ 15 fps | **~5 Mbps** | Bulk only | Many home uplinks will stutter |
| 1280×720 @ 15 fps | **~8–9 Mbps** | At the bulk ceiling | Do not plan on this |

Half-duplex still applies: **button down = camera live, button up = dead.** Do not run **Live hold-to-show** in the same second as PCM hangout + a busy LCD; **h12** already spends that budget on audio. A later “show me” session can be video-only, or audio-only, not a free Marco Polo clone.

LAN tryout can look great and still fail at their house. Measure uplink before treating **Live hold-to-show** as a product feature.

### Skip on this kit

| Idea | Why not |
|---|---|
| Always-on live video to the phone | Forbidden privacy shape; also USB + Wi-Fi heat for nothing |
| H.264 encode on the S3 | No hardware encoder; software 720p is fantasy |
| WebRTC / QUIC media | Already rejected for audio; worse for JPEG-shaped USB frames |
| USB 2.0 high-speed 720p30 | ESP32-S3 OTG is **full-speed**. That class of cam wants **ESP32-P4** (HS + codecs) — a different board |
| DVP/SPI camera on Pmod | Possible silicon-wise; steals the PTT/audio pins; ignores the dock USB-A the kit was bought for |
| USB webcam as a headset | USB-audio host is not in this tree; the port is the **camera** port ([`SPEAKER.md`](SPEAKER.md) — skip USB headset / USB mic on dock) |
| USB disk and camera at once | One USB-A. A full-speed hub **shares** 12 Mbps — snapshot-then-copy can work; live record-to-stick while streaming will starve |
| Parent → child live video on the LCD | 320×240 SPI + no JPEG decoder today; “not video” is a v1 non-goal. Stills already work via `/preview` |
| Second MCU (ESP32-CAM sidecar) | Another radio, another failure mode, another thing to flash before it goes to their desk |

## How this sits next to audio

| Job | Audio today | Camera analogue |
|---|---|---|
| Async clip | **h08** WAV POST | **One still (POST)** (then **Short clip** if you need video) |
| Inbound media | **h09** / **h13** / **h18** from server | Parent already receives JPEGs; child LCD stays RGB565 preview |
| Live | **h11** / **h21** PCM on `/v1/ws` | **Live hold-to-show** JPEG frames on the same WS **or** a dedicated session |
| Gate | Hold mute / red circle / latch | Same controls. Camera off when the button is up |

USB host and I2S0 can coexist (different peripherals). The fight is **CPU + PSRAM + Wi-Fi**. Keep **One still (POST)** off the hangout path until a demo says otherwise.

## Firmware status

| Path | Today |
|---|---|
| Parent → child still | **h13**, **x01**, combined `/preview` |
| Child → parent still | **Not in this tree.** Protocol can be simulated from the Mac. |
| LCD short clip (baked) | **h28** — 5 s RGB565 film at 12 fps. No decoder, no download. Glass budget only. |
| `usb_stream` / BOX-3 `usb_camera_lcd_display` | Espressif example only. Not flashed as an h-demo yet. |
| JPEG decoder on the box | **None** (deliberate). Needed only for **Viewfinder, then still**. |
| `kind=video` | **None.** |
| Live JPEG on `/v1/ws` | **None.** Hangout binary frames are 16 kHz PCM. |

Suggested order if child→you photos enter the tryout: buy a known-FS MJPEG cam → flash Espressif LCD example on the dock (proof the cam enumerates) → **One still (POST)** against combined → **Viewfinder, then still** if the still is always aimed at the ceiling → stop. **Short clip** and **Live hold-to-show** are a later phase, after hangout at their house is boring.

## Storage (video-shaped)

The box is **not** the archive. Playhead and blobs live on the server you run. PSRAM holds a take for the length of a POST; then it is gone.

If you later **claim the unused flash** (~12 MiB wear-leveled FAT after a 2–3 MB app), that cache holds on the order of **~90 s** of 320×240 MJPEG, **~40 s** of 640×480 MJPEG, or **~35–40** ten-second PCM voicemails — not a weekend of video. Removable media buying specs: [`STORAGE.md`](STORAGE.md).

## Related

- Kit facts and expansion: [`HARDWARE.md`](HARDWARE.md)
- Audio I/O (do not steal USB pins): [`SPEAKER.md`](SPEAKER.md)
- Photo receive demo: [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) h13
- “Child → you photos in v1?” still open: [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)
