# Parent watch (Amazfit 2) — optional sidecar

Notes on using an **Amazfit Active 2** or **Amazfit Balance 2** as a second **parent outbound mic** for Family Link. The child endpoint stays the desk **BOX-3**; the watch is not a replacement for it.

Collected 2026-08-24 from product conversation, Zepp OS docs, and community examples. Status: **exploratory** — no watch code in this repo yet.

## Problem it might solve

You already carry a phone for the parent web app. A watch adds a **hands-free async voice note** path: record on the wrist while walking or driving, clip lands in a child’s inbox on the box — same store-and-forward story as [`REQUIREMENTS.md`](REQUIREMENTS.md), without unlocking Safari and holding record.

The watch’s **built-in Voice Memos** feature does **not** plug into Family Link today. It syncs through **Zepp’s app and cloud** (transcription, workout notes). See [Stock Voice Memos vs custom app](#stock-voice-memos-vs-custom-app) below.

## Hardware (Amazfit “2” line)

“Amazfit 2” in conversation usually means **Active 2** or **Balance 2**. Both run **Zepp OS 5.0** and support third-party **mini programs** at **API level 4.2**.

| Model | Shape | Screen | Zepp OS | API level | Mini programs |
|---|---|---|---|---|---|
| **Amazfit Active 2** | Square or round | 390×450 (sq) / 466×466 (rnd) | 5.0 | 4.2 | Yes |
| **Amazfit Balance 2** | Round | (see manual) | 5.0 | 4.2 | Yes |

Official device matrix: [Device Basic Information](https://docs.zepp.com/docs/reference/related-resources/device-list/) (Zepp OS docs).

**Audio on the watch (for custom apps)**

- Recording API: [`@zos/media`](https://docs.zepp.com/docs/reference/device-app-api/newAPI/media/) — [`Recorder`](https://docs.zepp.com/docs/reference/device-app-api/newAPI/media/Recorder/)
- Format: **Opus** to a file in the mini app’s `data://` sandbox (e.g. `data://record.opus`)
- Playback: same module can play OPUS/MP3 from assets or `data://`

**Stock Voice Memos (Balance 2 manual)**

- With watch connected to phone: Zepp app → Device → Amazfit Balance 2 → **Voice Memos** → select recording → transfer to phone → play in app
- Manual: [Amazfit Balance 2 user manual (Voice Memos)](https://www.manualslib.com/manual/4141560/Amazfit-Balance-2.html?page=45)
- Third-party review of Amazfit’s workout voice notes (T-Rex 3 / Active 2 class devices): [Amazfit Voice Memos Notes](https://the5krunner.com/2026/02/09/amazfit-voice-memos-notes/)

**What the watch is not**

- Not a Wi‑Fi client you control — network uploads go through the **paired phone**
- Not rootable in any practical sense on current Zepp OS hardware (see [Root / “hack the OS”](#root--hack-the-os))
- Not a child endpoint at the other house (wrong owner, wrong always-on desk role)

## Fit with Family Link architecture

Do **not** invert the product shape from [`AGENTS.md`](AGENTS.md):

```
BOX-3  ──HTTPS/WSS──►  server you run  ◄──  parent phone (Safari / PWA)
```

The watch becomes a **third client** on the parent side:

```
┌─────────────┐   Bluetooth    ┌──────────────┐   HTTPS      ┌──────────────┐
│  Amazfit 2  │ ─────────────► │ Zepp app     │ ───────────► │ Family Link  │
│  mini app   │  Side Service  │ (iPhone)     │  POST audio  │ server       │
└─────────────┘                └──────────────┘              └──────┬───────┘
     │ record OPUS locally                                          │
     │ chunk to phone                                               ▼
     └──────────────────────────────────────────────────────► child BOX-3 inbox
```

**Best use case:** parent → child **async audio** (`POST /v1/messages`, `kind=audio`). Same route the combined host already exposes (`demos/server/combined/server.py`).

**Poor fit (v1):** live half-duplex hangout from the wrist — too many hops (watch → phone → server → box), latency, and half-duplex discipline.

The device registry already anticipates a parent device: [`devices.example.yaml`](../devices.example.yaml) notes *“A real parent-a device can be added later without changing routes.”* Add something like `parent-dad` with `role: parent` and `peer: box-a` (or a picker in the mini app when two children exist).

## Stock Voice Memos vs custom app

| Approach | Verdict |
|---|---|
| **Intercept built-in Voice Memos** | **No.** Closed Zepp pipeline (watch → Zepp app → cloud/transcription). No third-party export hook. |
| **Custom Zepp OS mini app** | **Yes.** Record with `@zos/media`, upload via **Side Service** on the phone. Official, sideloadable. |
| **Child uses watch instead of box** | **Wrong role.** Box is the child’s desk appliance at the other house. |
| **“2 new from Mazi” on wrist** | Possible later (Side Service polling / notifications). Not v1. |

## Sideloading — official path (no OS hack)

You do **not** need to root or jailbreak the watch. Zepp provides a normal developer workflow.

### Prerequisites

- **Phone:** [Zepp app](https://docs.zepp.com/docs/guides/quick-start/preview/) paired with the watch
- **Mac (or PC):** Node.js LTS — [Environment preparation](https://docs.zepp.com/docs/guides/quick-start/environment/)
- **CLI:** `npm i @zeppos/zeus-cli -g` — [Zeus CLI](https://docs.zepp.com/docs/guides/tools/cli/)
- **Account:** Free Zepp developer account; `zeus login` (required for `zeus preview` on a real device)
- **Console (optional):** [console.zepp.com](https://console.zepp.com/) — only needed for App Store submission, not personal sideload

### Enable Developer Mode on the phone

1. Zepp app → **Profile** → **Settings** → **About**
2. Tap the **Zepp logo 7 times** until Developer Mode appears
3. On the bound device page → **Developer Mode** → **Scan**, logs, Bridge, device API level

Docs: [Zepp App Developer Mode](https://docs.zepp.com/docs/guides/tools/zepp-app/)

### Install your app on the watch

```sh
zeus create family-link-watch    # interactive template
cd family-link-watch
zeus dev                         # simulator — UI/layout
zeus login                       # once
zeus preview                     # QR in terminal → Scan in Developer Mode
```

Docs: [Preview on a Watch](https://docs.zepp.com/docs/guides/quick-start/preview/)

| Command | Purpose |
|---|---|
| `zeus dev` | Simulator; fast UI iteration |
| `zeus preview` | Real watch via QR scan |
| `zeus build` | `.zab` package in `dist/` (store submit or archive) |
| `zeus bridge` | Developer Bridge — live connection, logs ([CLI docs](https://docs.zepp.com/docs/guides/tools/cli/)) |

**Persistence:** The preview **QR code** expires (session token; CLI shows expiry — [release notes](https://docs.zepp.com/docs/guides/tools/cli/release-note/)). The **installed mini program stays on the watch** until you remove it or reinstall. Re-scan after code changes.

**Distribution:** Personal use = repeat `zeus preview`. Public install = submit `.zab` through the developer console (review; paid apps may use KiezelPay — overkill for a family-only tool).

### Difficulty (honest)

| Milestone | Effort |
|---|---|
| Hello world on wrist (Developer Mode + `zeus preview`) | ~30 minutes if Node is already set up |
| Record button → OPUS file on watch | Moderate — test on hardware; simulator is weak for mic |
| Side Service → Family Link server | Moderate — watch has no direct HTTPS; phone relays. Reference: [lefred/bip6-ideas](https://github.com/lefred/bip6-ideas) (Bip 6, same pattern: chunked watch → phone → `POST`) |
| End-to-end inbox on BOX-3 | Small server gap if watch sends **Opus** and box expects **WAV** — see [Server / codec](#server--codec-gaps) |

Platform intro: [Introduction to Zepp OS](https://docs.zepp.com/docs/intro/)

## Custom mini app shape (proposed)

Zepp mini programs split across three runtimes ([Side Service intro](https://docs.zepp.com/docs/guides/framework/side-service/intro/)):

| Part | Runs on | Job |
|---|---|---|
| **Device app** | Watch | UI: pick child, hold to record, release to send |
| **Side Service** | Zepp app on iPhone | Reassemble audio from watch over BLE; `fetch` POST to Family Link |
| **Settings app** (optional) | Phone | Server URL, parent bearer token, default child |

**Watch recording (official API)**

```js
import { create, id, codec } from '@zos/media'

const recorder = create(id.RECORDER)
recorder.setFormat(codec.OPUS, { target_file: 'data://clip.opus' })
recorder.start()
// … hold …
recorder.stop()
```

**Phone upload:** Side Service registers with [`AppSideService`](https://docs.zepp.com/docs/guides/framework/side-service/register/). Use the messaging bridge (watch ↔ phone); Side Service calls your server with [`fetch`](https://docs.zepp.com/docs/guides/framework/side-service/intro/). Chunk large files — `TransferFile` is unreliable on some devices; base64/json chunks from the watch are the known workaround ([bip6-ideas](https://github.com/lefred/bip6-ideas)).

**Background:** Continuous background on watch is limited ([App Service](https://docs.zepp.com/docs/guides/framework/device/app-service/) — permissions, 600 ms single-shot limits, user prompts). For “tap, record, send,” a foreground mini app is enough.

## Server / codec gaps

| Topic | Today | Watch path |
|---|---|---|
| Auth | Bearer token per device ([`devices.example.yaml`](../devices.example.yaml)) | Add `parent-*` token in `devices.local.yaml` |
| Upload route | `POST /v1/messages` — `kind=audio`, `blob` file | Same; Side Service sends multipart from phone |
| Audio format | Demos mostly **WAV** on disk | Watch records **Opus** |
| Long clips | [`STORAGE.md`](STORAGE.md), **h22** chunking | Chunk on watch → phone → server if memos run minutes |

Open product decision: Opus on the wire vs transcode to WAV on server (ffmpeg) vs Opus decode on ESP32 — already flagged in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) under audio codec format.

## Root / “hack the OS”

**Not applicable for Active 2 / Balance 2.**

Older Amazfit models (Pace, Stratos) ran Android-ish stacks with bootloader unlock, `adb`, and community ROMs. Modern **Zepp OS** watches:

- No public root or custom firmware
- No practical way to replace the OS or hook system Voice Memos
- Hardware “port” under the nameplate on some models is **sensor/buzzer**, not a debug cable ([XDA discussion — T-Rex 2 / Zepp OS](https://xdaforums.com/t/zepp-os-and-how-to-access-it.4464117/))

For Family Link, treat **sideloaded mini programs** as the only supported extension surface.

## What you still cannot do

- Redirect **stock Voice Memos** into your server
- Upload **without the phone** in the loop (no watch Wi‑Fi client you own)
- Guarantee delivery if **Zepp app is killed** on the phone — Side Service runs inside Zepp
- **Live PTT hangout** from the wrist without heavy engineering
- Use the watch as the **child** endpoint at the other house

## Suggested build order (when/if)

1. Confirm sideload path: Developer Mode → `zeus create` → `zeus preview` → app icon on watch.
2. Add `parent-*` to `devices.local.yaml` with token and `peer` pointing at one box.
3. Mini app: record 10–30 s OPUS → Side Service → `POST` to combined server on the LAN.
4. Verify clip in child inbox / **h18** playback path on the box.
5. Polish: child picker (Mazi / Arlo), longer clips, haptic “sent”, server Opus handling.

Repo home for future code (not created yet): e.g. `watch/family-link-watch/` at repo root, outside firmware and Python demos.

## Reference links

### Zepp OS — getting started

- [Introduction to Zepp OS](https://docs.zepp.com/docs/intro/)
- [Environment preparation](https://docs.zepp.com/docs/guides/quick-start/environment/)
- [Preview on a Watch](https://docs.zepp.com/docs/guides/quick-start/preview/)
- [Zeus CLI](https://docs.zepp.com/docs/guides/tools/cli/)
- [Zeus CLI release notes](https://docs.zepp.com/docs/guides/tools/cli/release-note/)
- [Zepp App Developer Mode](https://docs.zepp.com/docs/guides/tools/zepp-app/)
- [Developer console](https://console.zepp.com/)

### Zepp OS — APIs used for Family Link

- [Device list / API levels](https://docs.zepp.com/docs/reference/related-resources/device-list/)
- [Media module (`@zos/media`)](https://docs.zepp.com/docs/reference/device-app-api/newAPI/media/)
- [Recorder](https://docs.zepp.com/docs/reference/device-app-api/newAPI/media/Recorder/)
- [Side Service introduction](https://docs.zepp.com/docs/guides/framework/side-service/intro/)
- [Register Side Service](https://docs.zepp.com/docs/guides/framework/side-service/register/)
- [App Service (background limits)](https://docs.zepp.com/docs/guides/framework/device/app-service/)

### Community / hardware notes

- [lefred/bip6-ideas](https://github.com/lefred/bip6-ideas) — watch record → Side Service → HTTP POST (reference implementation)
- [Amazfit Balance 2 manual — Voice Memos](https://www.manualslib.com/manual/4141560/Amazfit-Balance-2.html?page=45)
- [Amazfit Voice Memos Notes (the5krunner)](https://the5krunner.com/2026/02/09/amazfit-voice-memos-notes/)

### Family Link (this repo)

- [`REQUIREMENTS.md`](REQUIREMENTS.md) — async audio, parent on phone
- [`AGENTS.md`](AGENTS.md) — architecture; do not invert box ↔ server ↔ parent
- [`STORAGE.md`](STORAGE.md) — clip length, chunking, outbox
- [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) — Opus vs WAV, parent client shape
- [`devices.example.yaml`](../devices.example.yaml) — device registry; parent slot TBD
