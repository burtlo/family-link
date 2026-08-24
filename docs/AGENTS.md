# Notes for future agents

Read this before writing code. Product intent lives in [`REQUIREMENTS.md`](REQUIREMENTS.md). Hardware facts in [`HARDWARE.md`](HARDWARE.md). Unresolved decisions in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Demo plans: [`SERVER-DEMOS.md`](SERVER-DEMOS.md) (host), [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) (BOX-3 feasibility), [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md) (face, motion, chirps), [`PERSONA-ASSETS.md`](PERSONA-ASSETS.md) (parent likeness capture/cut/record/pack), [`DEMO-MAP.md`](DEMO-MAP.md) (what exists, audio paths, what to build next).

## What this project is

A **desk answering machine + walkie-talkie** so two children can reach a parent without borrowing the other parent’s phone (Marco Polo and Nintendo Switch voice both fail that way).

- Parent stays on **iPhone / Mac / PC**.
- Each child eventually gets their own **ESP32-S3-BOX-3** on **2.4 GHz Wi-Fi**.
- Media: **text, small photos, async audio**, and a **live half-duplex hangout**.
- **No video. No cellular. No e-ink. No canned phrases. No wake word.**

Folder name `family-link` is a placeholder. Names: [`NAMES.md`](NAMES.md). Do not brand around split-household language or Marco Polo.

## What this project is not

| Tree | Do not reuse |
|---|---|
| `/Users/lynnfrank/eink-family-messenger` | Arduino Waveshare firmware, five buttons, matching boxes, 5 s poll |
| `/Users/lynnfrank/src/eink-device-landscape` | T-Deck / HiBreak / Gmail / US-LTE shopping |

Reuse at most: “server you own” and “device identity.”

## Hardware on the desk (as of 2026-08-20)

A BOX-3 (or 3B — same main unit) is **plugged into this Mac**. Native USB serial is typically:

```
/dev/cu.usbmodem113401
```

(USB modem numbers move when you replug. `ls /dev/cu.usbmodem*`.)

**Flash through USB-C on the box**, not the dock’s USB-C (dock USB-C is **power only**). Unplug any USB camera on the dock USB-A while flashing. If download fails: hold **Boot**, tap **Reset**, release Boot.

Peel the **screen protector** or the mics are muffled (Espressif).

Stock firmware is a **wake-word assistant** (“Hi E.S.P.”). Replace it before the box goes to the other house. First bring-up flash should be Espressif BSP example `display_audio_photo` (`espressif/esp-box-3`), then ours.

Mute (top) in stock firmware toggles wake-word. **Remap to hold-to-talk.** Red circle under the screen is extra touch.

SSID and password are **known** and may be baked in. Never commit them. NVS / `sdkconfig` local overlay / secret file in `.gitignore`.

## Architecture (do not invert)

```
BOX-3  --HTTPS/WSS-->  server you run  <--  parent web app (iPhone Safari / Mac)
```

v1 tryout is **one box + parent phone**. A second box is only for a second child.

Hangouts are **half-duplex PTT**. Full-duplex speakerphone next to a Switch/TV will echo. Mixing for “everyone joins” is **on the server**, not on the ESP32.

Child → parent photos need a **UVC/MJPEG USB 1.1** camera on dock USB-A. Parent → child photos do not (phone camera roll, server downscales to ~320×240).

Nintendo Switch Online cannot run on the box. Voice sits **beside** Minecraft.

## Suggested build order

Most island/protocol/personality demos **and the first product glue** are in the tree. See [`DEMO-MAP.md`](DEMO-MAP.md).

- Combined host: `python -m demos.server.combined.server --host 0.0.0.0 --port 8080` (parent page at `/app/`).
- Product box: `make flash DEMO=x01`. Piece demos remain **h01–h19**.
- iPhone mic still needs HTTPS: [`TLS.md`](TLS.md) / `scripts/dev_https.py`.
- Still later: other-house SSID, always-on dock, second kit, group mixing (phase 3). Do not rewrite I2S.

iPhone **getUserMedia requires HTTPS** (localhost is OK on the Mac). Plan TLS even for a home server.

## Constraints that are already decided

- Wi-Fi 2.4 GHz only. No 5 GHz, no SIM.
- Mic live **only while a button is held**.
- PIN gates **playback** of stored inbound content. Recording out does not need a PIN if the box is that child’s.
- Locked idle may show a **count**, never the body, never audio.
- Parent client is a phone page, not a second box.
- USB wall power; desk appliance.

## If you are stuck

Open questions are listed in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Do not invent canned phrases, e-ink, or a matching parent gadget to “make progress.”
