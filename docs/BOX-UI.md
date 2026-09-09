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
asleep  --wake (touch / short circle)-->  signed out (user tiles)
signed out  --tap user-->  PIN pad  --ok-->  CAROUSEL
carousel  --shoulder-->  SETTINGS (volume, color, face, sign out)
settings  --shoulder-->  carousel
carousel  --1 min idle-->  PIN lock (same user portrait)
carousel  --sign out (settings)-->  signed out
carousel  --tap circle-->  recipient pick  --tap-->  RECORDING  --tap circle-->  carousel
carousel  --5 min idle-->  asleep (backlight off)
```

| Mode | Screen job |
|---|---|
| **Asleep** | Backlight off after 5 min (dim at 2 min). Session unchanged. |
| **Signed out** | All hangout users as profile tiles. Tap → PIN. |
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

## Other screens

| Screen | Contents |
|---|---|
| **Signed out** | Profile tiles for every hangout user |
| **PIN pad** | 3×3 (1–9); dots; Boot clears partial entry or back to roster; 5 tries → cooldown |
| **Recipient pick** | Four targets; Boot / timeout cancel |
| **Settings** | Sign out + volume; Boot back to carousel |
| **Wi-Fi / error** | `no wifi` — distinct from empty inbox |
| **Asleep** | Backlight off; wake on touch or short circle |

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
1) SIGNED OUT — profile tiles for each user
2) PIN — 3x4 pad
3) CAROUSEL — center card, peek edges, timeline, play, long-press hint
3b) SETTINGS — sign out, volume slider, Boot = back
4) CAROUSEL RECORDING — listening overlay on carousel
5) RECIPIENT PICK — four targets

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
| v1 media | Audio only |

Still open: product name, quiet hours, retention. See [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Full contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

---

## References

- Hardware: [`HARDWARE.md`](HARDWARE.md)
- Requirements: [`REQUIREMENTS.md`](REQUIREMENTS.md)
- Playback / buttons / list demos: [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) (h17, h18, h19)
- Presence / talk: [`TWO-BOX.md`](TWO-BOX.md), [`plans/mazi-arlo-open-line.md`](plans/mazi-arlo-open-line.md)
- Personality / face: [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md)
- Product spec: [`plans/v1-product-spec.md`](plans/v1-product-spec.md)
