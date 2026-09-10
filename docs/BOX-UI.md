# Box UI — endpoint screen design brief

Child- and adult-facing screens on the **BOX-3 kiosk**. Paste [Stitch prompt](#stitch-prompt) into [Google Stitch](https://stitch.withgoogle.com/) (or any UI generator). Hardware facts: [`HARDWARE.md`](HARDWARE.md). Product contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md). Requirements: [`REQUIREMENTS.md`](REQUIREMENTS.md).

Status: **approved** (2026-09-04). Carousel refresh **shipped** on box + web twin (2026-09-05) — see [`plans/carousel-ui-refresh.md`](plans/carousel-ui-refresh.md).

---

## What this product is

A **desk answering machine** for a small hangout (family group). **Users** (Lynn, Mazi, Arlo, …) sign in on **endpoints** (BOX-3 kiosks). Any user can use any endpoint. Messages are async **audio** in v1; live voice and drawing come later.

| Role | Device | What they do |
|---|---|---|
| Each person | Sign in on a **BOX-3 endpoint** (or web for Lynn admin) | Browse carousel inbox, play voice notes, send 1:1 or broadcast |
| Lynn | BOX-3 + **web `/app`** on the home server | Same mailbox experience on the box; admin for PIN reset, welcome audio, sends |

Three endpoints in v1. Server runs on Lynn’s Windows box (home LAN). Remote endpoints (kids’ house) connect over TLS when deployed.

```
endpoint  --HTTPS / WebSocket-->  server  <--  web /app (Lynn admin)
```

**Media on the box (v1 merge):** async **audio** only. Photos, drawing notes, and live hangout are **later** — carousel and protocol reserve room for more `kind` values.

**Not this product:** video, cellular, e-ink, canned phrases, wake word, phone UI crammed onto 320×240.

Sample copy in mockups may use **Lynn**, **Mazi**, **Arlo**. Do not put a child’s photograph on a lock or sign-in tile without asking.

---

## The physical object

The UI lives on a real plastic appliance, not in a browser chrome. Design the **pixels inside the LCD**. Also show the **bezel and controls** in at least one device mock so Stitch / reviewers remember fingers hit hardware as much as glass.

### Enclosure

**ESP32-S3-BOX-3** (same main unit as BOX-3B). Sits upright in the **BOX-3-DOCK** on a desk, usually next to a Nintendo Switch dock and a TV.

| Item | Spec |
|---|---|
| Body | **61 × 66 × 16.6 mm** (Espressif). Pocket-small. About a thick credit-card stack standing on edge. |
| Power | USB wall power. Always on. Desk appliance, not a handheld. |
| Orientation | Landscape when docked: **wider than tall**. Screen faces the child. |

```
          mute latch + red mute LED + dual mic holes
                         │
        ┌────────────────┴────────────────┐
        │                                 │
   USB-C│     2.4″ capacitive LCD         │ speaker
   Boot │        320 × 240 px             │ (on back)
  Reset │                                 │
        │                                 │
        └─────────────( ● )───────────────┘
                      red circle
                         │
                    gold fingers → dock
```

- **Front:** LCD, then a printed **red circle** on the plastic under the glass.
- **Top edge:** **mute** latch and a **red LED**. Two microphone holes (peel the screen protector or the mics are muffled).
- **Left side** (facing the screen): **USB-C**, **Boot**, **Reset**.
- **Back:** 8 Ω / 1 W speaker grille.
- **Bottom:** gold fingers into the dock. Dock USB-C is **power only**. Flash through USB-C **on the box**.

### The screen (this is the canvas)

| Item | Spec |
|---|---|
| Size | **2.4 inch** diagonal |
| Resolution | **320 × 240** pixels, **landscape** (320 wide, 240 tall) |
| Panel | ILI9341, SPI |
| Color | **RGB565** (65,536 colors). No 24-bit gradients, no hairline 1 px chrome that vanishes. |
| Active area | about **48.96 × 36.72 mm** |
| Pixel pitch | about **0.153 mm** (~**166 PPI**) |
| Uncompressed frame | 320 × 240 × 2 = **153.6 KB** |

**This is not a phone.** There is no status bar, no iOS safe area, no navigation bar, no notch. Every pixel is ours. A full-screen photo is exactly 320×240. A paragraph of text does not fit; short lines do.

### Touch (the glass)

The LCD is a **capacitive GT911** overlay. Fingers work. No stylus. Proven by the PIN pad demo (**h03**): taps register at desk distance.

| Rule | Why |
|---|---|
| Minimum tap **48–56 px** tall (~8 mm) | Child fingers, glancing from a game |
| Sliders **at least 18–24 px** track, fat knob | Volume and timeline are the main glass jobs |
| 12 px outer margin | Content sits in a 296 × 216 usable well |
| No hover, no right-click, no keyboard | Capacitive taps and holds only |
| ASCII labels | Default Montserrat on the box is missing glyphs (ellipsis paints as a box). Write `...` not `…`. |

The red circle is **not** part of the 320×240 framebuffer. It is a separate capacitive zone on the bezel. Do not draw a fake red button on the LCD to “be” the circle. The physical circle is already there.

### Web twin (layout without flashing)

Flashing the kit is slow for UI iteration. Two browser twins share the v1 server:

**v1 product (carousel + shoulder settings):**

```
python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
open http://localhost:8080/box/
```

**h18 playback** (older single-message screen; clip catalog):

```
python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080
open http://localhost:8080/box/
```

Keys on the v1 twin: **B** / **N** / **→** = shoulder. **C** / Space = red circle (short tap). **←** / **→** = older / newer. Mouse on the LCD is capacitive touch. Scale 1× / 2× / 3× is viewing only.

### Physical controls

Four things a hand can hit. Only three are programmable.

| Control | Where | Kind | Product job (v1) |
|---|---|---|---|
| **Mute** | Top edge | **Latch** | Hardware mic gate. Down = mics dead — block record, show `unmute first`. |
| **Boot** | Left side (GPIO0) | **Momentary** | **Shoulder button.** Carousel → settings. Settings → carousel. Picker / record → cancel. PIN → clear or roster. |
| **Reset** | Left side | Momentary | Reboot. **Not a UI control.** |
| **Red circle** | Bezel under LCD | Capacitive | **Short tap:** wake from sleep; carousel → recipient picker; recording → stop + upload. Ignored while playing/scrubbing. |

**Mute vs red circle is hardware.** Recording uses the circle with mute latch **up** (LED off).

### Audio

- **Speaker:** desk-volume. **ROOMVOL** slider on **shoulder settings** (shoulder from carousel): mute, then **78, 80, … 100** by twos.
- **Mic:** live during **recording mode** only (after recipient pick). No wake word.
- **Leading click:** trim first **~150 ms** after record start.
- **Record chirps:** two-tone step up at start, step down at stop (distinct from h17 button chirps).

---

## How the system feels (modes)

One endpoint, many users. Sign in → carousel → sign out or idle lock.

```
boot / wifi ok --server down-->  CONNECTING  --server up-->  signed out (user tiles)
asleep  --wake (touch / short circle)-->  signed out (user tiles)
signed out  --tap user-->  PIN pad  --ok-->  CAROUSEL
carousel  --shoulder-->  SETTINGS (volume, color, face, sign out)
settings  --shoulder-->  carousel
carousel  --1 min idle-->  PIN lock (same user portrait)
carousel  --sign out (settings)-->  signed out
carousel  --tap circle-->  recipient pick  --tap-->  RECORDING  --tap circle-->  carousel
carousel  --5 min idle-->  asleep (ambient sleep, low backlight)
```

| Mode | Screen job |
|---|---|
| **Asleep** | After 5 min idle (dim at 2 min): dark screen + slow bottom accent glow (~4–6% backlight). Signed-in unread shows a count badge pulse. Not the same as `no wifi`. Wake: touch or short circle. |
| **Connecting** | Signed out + server unreachable. Centered mailbox icon, `connecting`, animated dots. Keeps retrying — **not** an error screen. See [Connecting](#connecting-waiting-for-server). |
| **Signed out** | All hangout users as profile tiles. Tap → PIN. Only after server is reachable. |
| **PIN lock** | Same user’s portrait + PIN pad (1 min idle from carousel). 5 fails → 60s cooldown. |
| **Carousel** | Center message card, peek strips, play, scrub. Shoulder → settings. Top/bottom ribbons. |
| **Settings** | Scroll: volume → color swatches → face grid → sign out. Shoulder → carousel. |
| **Recipient pick** | Roster-style rows (face + name); Everyone has asterisk icon. Shoulder or 10s timeout cancels. |
| **Recording** | Listening UI; tap circle stops; 5s silence or 3min cap; shoulder cancels. |

**Read rule:** centering a card does **not** mark read. **Play** marks read (server sync).

**Server owns:** `last_viewed_seq`, per-message `read`, `position_ms` — **per user**, not per endpoint.

---

## What to reuse from desk demos

| Demo | Reuse in product |
|---|---|
| **h18** | Timeline, play/pause, ROOMVOL, sender/time/unread chrome |
| **h19** | Card slide animation (carousel peeks, not list-as-home) |
| **h08** | Record + POST path (adapt to tap-start, silence-stop, trim) |
| **h17** | Mute GPIO gate, chirp inspiration for record start/stop |
| **h03** | PIN pad layout |

**Retired for v1 product:** Boot = next message; hold-to-release record; one-user-per-box; Dad copy; presence strip; fullscreen detail.

---

## Carousel home (signed in)

The **primary screen**. Not a scrollable phone inbox.

### Layout

```
+----------------------------------------+
| ·· reserved              3/7         |  top ribbon 20px
|  [peek] [ face | (>) ] [peek]         |  card + 56px peeks
|         [========track========]        |
|           shoulder = back            |  bottom ribbon 20px
+----------------------------------------+
```

Center **message card** (~200×184): two panes (~100×162) + teal progress track.

**Shoulder settings** (scrollable, between ribbons):

```
| Lynn                                   |
| vol  ··················  90            |
| color  (10 swatches)                   |
| face   (geometry + 12 creatures)       |
|            [ sign out ]                |
|           shoulder = back              |
```

### Carousel rules

- **Center card** = current message (`last_viewed_seq` on server).
- **Left peek** = older; **right peek** = newer. Tap edge → animate to center.
- **First Message** (`seq=1`, system welcome): nothing to the left when centered.
- Switching cards **pauses** playback and **saves `position_ms`**; return restores position.
- **Never autoplay** on card change or after PIN.
- **Unread vs read** distinct chrome; read only after Play.

### Record (outbound)

1. **Tap circle** → recipient screen (touch only).
2. Tap a recipient or **Everyone** → recording starts + start chirp.
3. **Tap circle** → stop + upload + stop chirp. Also: **5s silence**, **3min** cap.
4. **Shoulder** during picker or record → cancel discard.
5. **Shoulder** on carousel → settings; shoulder again → back.
5. Mute latched → block; near-silence 0.5s → abort with `unmute first`.
6. **PIN required** before carousel and before record.

Hint copy: `unmute first` (toast when needed). First session: `tap circle to send` (once).

### Recording overlay

Replace play + timeline with **listening** overlay on carousel (settings unchanged underneath if opened later).

---

## Connecting (waiting for server)

Shown when the endpoint is **signed out** and **`GET /v1/hangout` fails** (server asleep, LAN/Tailscale blip, first boot). This is **not** a failure screen — the box is still trying. Do **not** show sign-in tiles, dev commands, or a separate “connection error” panel.

**Distinct from:**

| Situation | Screen |
|---|---|
| Wi-Fi join failed | **No Wi-Fi** — `this box needs the home network` / `ask Lynn to check the network` |
| Signed in, server dropped mid-session | Carousel **offline** ribbon (`offline` + `saved messages still play`); not the connecting screen |
| Empty inbox after sign-in | Carousel First Message / normal home |

### Layout (320×240)

Vertically centered stack on `#101418`:

1. **Mailbox icon** — gold `#E8C040`, ~1.5× desk size, **screen-centered** (not top-left).
2. **Label** — `connecting` (lowercase, **no** `...`; dots carry animation). `#F0F4F0`, Montserrat 28 or equivalent. Full-width centered text — not aligned to the icon’s left edge.
3. **Progress dots** — three **gold circles** (not text periods), horizontally centered as a row. Fixed positions so phases never jump.

Gap: ~22 px icon→label, ~14 px label→dots.

### Dot animation

- Tied to the **5 s retry window**: one dot → two → three, ~1.7 s per step.
- Timer **resets to one dot** after each probe attempt completes (so a slow HTTP probe does not skip the single-dot phase).
- Animation runs continuously while connecting; no fast-then-stall cadence.

### Retry / transition

- Probe server every **5 s** (`GET /v1/hangout`, **2.5 s** HTTP timeout per probe).
- **Sign-in roster** appears only when the server returns **200** with users.
- Hangout roster may be **cached in NVS** for a fast paint after reconnect, but **do not show user tiles while offline**.
- **No timeout** to an error screen — connecting stays until the server answers or Wi-Fi fails.
- **No ambient sleep** on this screen (same rule as Wi-Fi error).

### Copy rules (end users)

- Never show hostnames, ports, `make …`, or `secrets.h`.
- Only escalation path on other screens: **ask Lynn** (PIN lockout, Wi-Fi). Connecting screen has **no** “ask Lynn” line — calm wait only.

**Firmware reference:** `firmware/demos/x02_product_shell.c` (`ST_CONNECTING`, `paint_connecting`).

---

## Connection confidence (signed-out)

The box must never look **ready to sign in** when the server cannot answer. A child should always be able to tell whether the device is **waiting**, **checking**, or **ready** — never stuck or silently broken.

### Principle

| User question | Screen truth |
|---|---|
| Can I pick someone? | Only on **sign-in roster** after a successful hangout probe |
| Did my last tap register? | PIN dots update **immediately**; 4th digit shows **checking...** before any network work |
| Is the server back? | **Connecting** with animated dots until hangout probe succeeds |

WebSocket disconnect is a **fast hint**; **`GET /v1/hangout` every 5 s** (2.5 s timeout) is the **source of truth** while signed out.

### Signed-out heartbeat

While **nobody is signed in** (roster, PIN pad, or connecting — not Wi-Fi error):

- Probe `GET /v1/hangout` every **5 s** even when the roster or PIN pad is visible.
- On probe failure → **connecting** screen immediately (clear partial PIN).
- On probe success → roster (from connecting) or stay on roster/PIN if already there.
- **Do not** show user tiles or a live PIN pad when the last probe failed — even if a cached roster exists in NVS.

### PIN verify (async)

- Digits 1–3: update dots on tap (unchanged).
- Digit 4: show **four dots** and status **`checking...`**; run `POST /v1/session/login` on a **worker task** with the **2.5 s** probe timeout — never block the touch handler.
- Ignore further keypad taps while **checking...**.
- Wrong PIN → clear entry, **`wrong pin`** (unchanged lockout rules).
- Network / server failure → **connecting** screen (not a frozen pad or mystery missing dot).

### Signed-in (unchanged)

Server loss after sign-in → carousel **offline** ribbon; block send and server-backed ops; **not** the connecting screen.

**Firmware reference:** `signed_out_pre_auth`, `signed_out_hangout_probe`, `enter_connecting_from_signin`, `login_task_fn` in `firmware/demos/x02_product_shell.c`.

---

## Other screens

| Screen | Contents |
|---|---|
| **Connecting** | Mailbox + `connecting` + dot row — see above |
| **Signed out** | Profile tiles for every hangout user (server must be up) |
| **PIN pad** | 3×3 (1–9); dots; Boot clears partial entry or back to roster; 5 tries → cooldown |
| **Recipient pick** | Four targets; Boot / timeout cancel |
| **Settings** | Sign out + volume; Boot back to carousel |
| **No Wi-Fi** | Wi-Fi icon + `no Wi-Fi` / home network copy — distinct from connecting and empty inbox |
| **Asleep** | Dark navy + bottom accent breathe; ~25 s hint pulse above bezel circle; wake on touch or short circle |

**Later (not v1 merge):** live hangout overlay, drawing playback, photo card, archive list (h19 rows).

Web `/app` is a different canvas. See [`v1-product-spec.md`](plans/v1-product-spec.md).

---

## Color, assets, and what not to fake

- **RGB565.** Flat fills and two-stop gradients only.
- **No Material You** phone kit, no iOS tab bar, no hamburger, no settings cog forest.
- **No keyboard.** Child outbound is voice. Parent sends text *to* the box.
- **No video player, no camera viewfinder** on v1 home (USB camera is a later dock path).
- **No wake-word orb**, no “listening…” while idle.
- Placeholder **geometry face** (slot 0) or **12 built-in fun avatars** (creatures, RGB565 in flash). User picks avatar + accent on shoulder settings; synced per `user_id` on server.
- Do not use licensed characters or a child’s photograph as the avatar without asking.
- Photos from the parent arrive as **320×240 RGB565**. The box does not decode JPEG.

---

## Stitch prompt

Copy from here down. Canvas is **exactly 320×240 landscape**.

```
Design UI for a family mailbox kiosk on a desk appliance, not a phone app.

DEVICE
- ESP32-S3-BOX-3 in a dock. Always plugged in. 320 x 240 landscape RGB565 touch LCD.
- Red circle on bezel below LCD (not drawn on LCD). Mute latch on top (hardware mic gate).
- Boot on left side. Reset reboots — not a UI key.

PRODUCT (v1)
- Hangout with users Lynn, Mazi, Arlo. Sign in on any endpoint with PIN.
- Async audio messages only. Carousel inbox with peek cards left/right.
- Long-press red circle to send (recipient pick: Lynn / Mazi / Arlo / Everyone).
- Play/Pause on glass. Timeline always visible. Volume slider always at bottom.
- Sign out top-left. No video, no wake word, no phone chrome.

SCREENS
1) CONNECTING — centered gold mailbox, "connecting", three gold dots (1 then 2 then 3)
2) SIGNED OUT — profile tiles for each user
3) PIN — 3x4 pad
4) CAROUSEL — center card, peek edges, timeline, play, long-press hint
4b) SETTINGS — sign out, volume slider, Boot = back
5) CAROUSEL RECORDING — listening overlay on carousel
6) RECIPIENT PICK — four targets
7) NO WI-FI — separate from connecting; ask Lynn to check network

STYLE: dark bg #101418, cards #2A3038, unread gold #E8C040, calm desk appliance.
Also one DEVICE CHROME mock with bezel and red circle.
```

---

## Decisions this brief locks

| Topic | Decision |
|---|---|
| Canvas | 320×240 landscape touch LCD |
| Home | **Carousel** with peek cards; not list; not Boot-as-next |
| Sign-in | Multi-user per endpoint; sign-out roster |
| Record | Tap circle → picker → record; tap circle stop; 5s silence; 3min cap |
| Play | Glass Play/Pause + timeline scrub |
| Shoulder | Carousel → settings; settings → carousel; cancel picker / recording |
| Volume | On shoulder settings screen |
| Identity | Per-user `avatar_slot` + `accent_hex` on server; 10 colors + 13 faces |
| PIN | Required for carousel and record; 1 min idle relock |
| Locked | PIN lock shows user portrait; signed-out shows all users |
| Connecting | Signed out + server down: mailbox + `connecting` + dots; retry every 5 s; no error timeout |
| Connection confidence | Signed-out roster/PIN re-probe every 5 s; offline → connecting; PIN verify async with `checking...` |
| No Wi-Fi | Separate screen; not connecting; not empty inbox |
| v1 media | Audio only |

Still open: product name, quiet hours, retention. See [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Full contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

---

## Appendix: Acceptance scripts

Use these scripts after any firmware or web-twin change that touches PIN, connectivity, or carousel. Run on **device** (with serial monitor) and on the **web twin** before claiming a fix complete.

**Device workflow:** follow [`.cursor/skills/device-test-after-flash/SKILL.md`](../.cursor/skills/device-test-after-flash/SKILL.md) — flash with monitor, paste login/state log excerpts into session notes.

**Web twin (no flash):**

```bash
python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
# open http://localhost:8080/box/
```

Run the same scripts below in the browser twin where behavior is implemented (connecting UX may lag firmware — note divergence before flash).

### PIN script

1. From **sign-in roster**, tap a user → PIN pad.
2. Enter **4 digits** (one tap each).
3. **Expect:** status **`checking...`** on the 4th digit (within one tap — async worker must not block the touch handler).
4. **Within 3 s**, exactly one of:
   - **Carousel** (correct PIN, server up),
   - **`wrong pin`** (incorrect PIN),
   - **Connecting** screen (network / server failure — not a frozen pad).

**Never acceptable:** infinite **`checking...`** with no transition. Network failure must land on **connecting**, not a stuck pad (see [PIN verify (async)](#pin-verify-async) — line 313: *Network / server failure → connecting screen*).

### Connecting script

1. Start with server down or unreachable (stop `make v1-server`, or block LAN).
2. Boot or trigger signed-out probe failure → **connecting** screen.
3. **Expect:** three gold dots animate **1 → 2 → 3** over the **5 s retry window** (~1.7 s per step); animation **resets to one dot** after each probe completes.
4. **Expect:** dots keep moving while waiting — no long frozen stall, no fast-then-stall burst.
5. Restore server → roster appears after successful `GET /v1/hangout`.

See [Dot animation](#dot-animation) and [Retry / transition](#retry--transition).

### Carousel script

1. Sign in → **carousel** with at least two messages.
2. **Snap:** tap a **non-center** peek card → card **animates** to center (not an instant jump).
3. **Play focus:** with the centered card, tap **Play** → let playback **finish** (do not navigate away).
4. **Expect:** the **same card remains centered** after playback ends (no jump to first message).

See [Carousel rules](#carousel-rules) — center card, peek tap, position restore.

---

## References

- Hardware: [`HARDWARE.md`](HARDWARE.md)
- Requirements: [`REQUIREMENTS.md`](REQUIREMENTS.md)
- Playback / buttons / list demos: [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) (h17, h18, h19)
- Presence / talk: [`TWO-BOX.md`](TWO-BOX.md), [`plans/mazi-arlo-open-line.md`](plans/mazi-arlo-open-line.md)
- Personality / face: [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md)
- Product spec: [`plans/v1-product-spec.md`](plans/v1-product-spec.md)
