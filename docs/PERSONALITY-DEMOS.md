# Personality demos — plan

Small BOX-3 apps that ask whether this 320×240 desk kit can feel like a **presence** — a face that idles, notices you, and reacts — without becoming a wake-word toy or a virtual-pet sim.

Protocol is [`SERVER-DEMOS.md`](SERVER-DEMOS.md). Hardware jobs (codecs, PTT, Wi-Fi) are [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md). This track is the **HMI character**: avatar on the LCD, motion, transitions, button reactions, and short acknowledgement sounds.

It can run on the USB kit in parallel with host scripts. It does **not** wait on a server. It must not wait on a finished illustration either: demos use a geometric stand-in face so we are testing the engine, not commissioning art.

## What this is

A communicator with a face. Idle it looks alive. Hold the mute key and it *listens*. A new message (simulated at first) makes it perk up and chirp. Locked, it may look sleepy and show a **count badge** — never the message body, never autoplay of a voicemail.

## What this is not

- Not a Tamagotchi. No hunger, no care meters, no “feed me.”
- Not a wake-word character. It does not listen until a button is held. No ESP-SR. No “Hey …”
- Not TTS or a talking mascot. Acknowledgements are **short PCM chirps**, not spoken lines.
- Not the product LVGL app. One expressive job per flash, same as the h-series.
- Not a reason to skip h05/h12. A pretty face that glitches I2S is a fail.

Quiet hours, how lively a glowing LCD pet is in someone else’s house, and the actual character look are still open ([`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)). Demos use a mute-able SFX path and a dimmable idle so those decisions stay cheap to change.

## What we are examining

| Risk | Why a demo |
|---|---|
| 320×240 SPI is slow | Full RGB565 frame is 153 KB. A 30 fps full-screen face may starve later audio/Wi-Fi |
| “Alive” vs slideshow | Blink/breathe must read as one creature from desk distance |
| Button-to-paint latency | If the face reacts 300 ms late, it does not feel like a pet |
| SFX + animation + later I2S | Chirps share the ES8311 with voicemail and PTT; pops and underruns are the question |
| Locked idle rules | Face + badge is allowed; leaking text/audio of a message is not |
| LCD glow | An always-moving face is brighter and more attention-seeking than a dim count |

Wokwi is fine for **p01–p05** (pixels and touch). **p06–p09 need the kit** (speaker, and p09 needs real playback).

## Constraints every personality demo obeys

Same board rules as device demos: ESP-BSP `esp-box-3`, USB-C on the box, no wake word, mic only while held, UART `-- PASS p0n`.

Additionally:

- Placeholder art only: simple shapes or a tiny sprite sheet (eyes, mouth, optional blush). No licensed characters, no child’s photo as the avatar.
- Locked and “has mail” states may show **N**, not words from the inbox.
- SFX files are a few hundred milliseconds, stored in flash. They are not recordings of family members.
- Print **FPS** (or ms/frame) on UART for any demo that animates. We need a number before we layer protocol audio.

## Layout

Same IDF project as the h-series, different mains.

```
firmware/
  common/                 # already: BSP init
  persona/                # state enum, tween helper, sfx trigger (grows from these demos)
  assets/
    face/                 # placeholder frames or atlas (committed)
    sfx/                  # short wav chirps (committed; not family audio)
  demos/
    p01_static_face.c
    p02_idle_life.c
    p03_moods.c
    p04_transitions.c
    p05_press_react.c
    p06_sfx.c
    p07_pet_loop.c
    p08_notice.c
    p09_talk_or_freeze.c
```

Select with `-D FAMILY_DEMO=p01`. `persona/` is what the product UI should call later, not a second animation stack.

## Discrete demos

### p01 — Static face

**App:** `p01_static_face.c`  
**Needs:** h01 (screen works).

- Fill 320×240 with a readable face: two eyes, a mouth, optional color block for “body.”
- Desk-distance test: it should parse as a character, not a settings screen.

**Pass:** `-- PASS p01`. Human: you can tell what it is from the chair.

**Feasibility:** the LCD can carry an avatar at all. If it looks like a postage stamp of noise, fix palette/contrast before animation.

**Reuse later:** framebuffer or LVGL image widget, asset pipeline (PNG → C array).

### p02 — Idle life (blink / breathe)

**App:** `p02_idle_life.c`

- Same face. Blink on a timer (every few seconds). Optional slow “breathe” (a few pixels of scale or lid).
- UART prints average FPS and max ms/frame over 10 s.

**Pass:** `-- PASS p02` if blinks look like one creature, not a flipbook, and we have a printed FPS. **Target to record, not to invent:** even ~8–12 fps idle may be enough for a pet; chasing 30 fps is how h12 dies later.

**Feasibility:** sustainable idle animation on SPI. If we can only afford a blink every 2 s and a static frame in between, that is still a personality — write that number down.

**Reuse later:** idle animator, frame budget.

### p03 — Moods (state faces)

**App:** `p03_moods.c`

Hard cuts between named states, cycled by the mute key or on-screen buttons:

| State | Looks like | Product meaning |
|---|---|---|
| `idle` | Neutral, occasional blink | Locked or unlocked, nothing new |
| `listen` | Lean in / open mouth | Mute **held** (mic would be live in product) |
| `mail` | Perk + badge **N** | Unread count; no body |
| `locked` | Sleepy lids; badge if N>0 | PIN not entered |
| `error` | Distinct frown | Wi-Fi/auth fail (simulated) |

**Pass:** `-- PASS p03` after each state has been shown. Human: distinct from across the desk. Badge is a number, not a preview.

**Reuse later:** `persona_state` enum used by the product state machine.

### p04 — Transitions

**App:** `p04_transitions.c`  
**Needs:** p03.

- Same states, but 200–400 ms tweens (crossfade, lids, small move). No 2-second cinematic.
- UART: duration of each tween.

**Pass:** `-- PASS p04` if idle→listen→idle feels like one creature turning its attention, not a slide deck.

**Feasibility:** tweening on 320×240 without dropping to 1 fps. If tweens hitch, product uses **hard cuts** (p03) plus SFX only.

**Reuse later:** tween helper in `persona/`.

### p05 — Press reactions

**App:** `p05_press_react.c`  
**Needs:** h04 (mute is hold-to-talk).

- Mute **down** → `listen` (immediate). **Up** → settle back to idle.
- Red-circle tap → a short “poke” (surprise frames, then idle). Touch is a poke, not a microphone.
- UART: milliseconds from GPIO/touch ISR (or poll) to first painted reaction frame.

**Pass:** `-- PASS p05` if mute down is obvious without looking at the serial log. Record the latency. If it is routinely over ~150 ms, the pet will feel drunk — fix the render path before adding SFX.

**Feasibility:** reactivity. This is the difference between a picture and a companion.

**Reuse later:** input → persona event (`PTT_DOWN`, `PTT_UP`, `POKE`).

### p06 — Acknowledgement sounds

**App:** `p06_sfx.c`  
**Needs:** h05 path for speaker (ES8311); **kit, not Wokwi.**

Map events to **short** chirps from flash (different pitches/lengths, not speech):

| Event | Sound role |
|---|---|
| Mute down | “armed / listening” |
| Mute up | “released” |
| Poke | “hey” |
| New mail (button-simulated) | “notice” |
| Unlock (simulated PIN ok) | “ok” |
| Error | “nope” |

Mic stays off. A compile flag or long-press **mutes SFX** (quiet-hours hook).

**Pass:** `-- PASS p06` after each chirp plays without a click/pop. Human: audible at desk, not a room alarm. **Fail** if a chirp is a spoken word.

**Reuse later:** `sfx_play(event)` on the same I2S port voicemail will use. Mixing a chirp *over* a clip is out of scope; chirps play when the clip player is idle, or they duck/skip.

### p07 — Pet loop (the actual personality demo)

**App:** `p07_pet_loop.c`  
**Needs:** p05 + p06.

One binary: idle life + press reactions + chirps, no network.

Sit with it for a minute. Hold PTT, poke the red circle, let it blink.

**Pass:** `-- PASS p07` as a **human** call: it feels reactive, not a menu. UART still prints FPS. If animation and chirps fight (stutter, delayed sound), fail and simplify (hard cuts + SFX, or blink-only idle).

**Reuse later:** the default locked-idle experience, minus real mail.

### p08 — Notice (mail without a server)

**App:** `p08_notice.c`

- Idle. After a few seconds (or a debug button) fire a fake `inbox` event: perk transition, notice chirp, badge **1** (then **2**).
- Face stays `mail` / locked-with-badge. Do **not** play a voicemail. Do **not** draw message text.
- Optional: tap PIN digits (from h03) to “unlock” — face becomes idle-happy, badge clears. Still no message body in this demo.

**Pass:** `-- PASS p08` if a walk-by can tell “something arrived” without learning what it was.

**Feasibility:** locked-idle product rule with a pet face. This is how “2 new” survives as a character instead of a seven-segment count.

**Reuse later:** WS `inbox` alert from the server becomes `persona_event(MAIL)` plus badge from `/v1/me`.

### p09 — Talk or freeze (face during playback)

**App:** `p09_talk_or_freeze.c`  
**Needs:** h09 or h05 (real clip on the speaker).

While a fixture WAV plays:

- **A:** two- or three-frame mouth flap on a timer (not lip-sync).
- **B** (if A glitches): freeze the face, play audio, then a small “done” reaction + chirp.

**Pass:** `-- PASS p09` if the **clip stays intelligible**. Printed glitch count (underruns) must not jump when the mouth moves. If A fails, **B is the product decision**, not a later surprise.

**Feasibility:** same family as device h12. Personality does not get to steal the I2S budget from the message.

**Reuse later:** hangout UI either animates at idle FPS or paints once.

## Suggested order vs other tracks

```
h01 screen ──► p01 face ──► p02 idle ──► p03 moods ──► p04 tweens
h04 PTT    ──► p05 react ─────────────────────────────┘
h05 speaker ─► p06 sfx ──► p07 pet loop ──► p08 notice
h09 play   ──► p09 talk-or-freeze          (before animated hangout)
```

Do not block host demos or h06–h11 on a finished pet. Do not ship an animated hangout until p09 (and h12) have a written outcome.

## How this maps to the product app

The product screen is a **persona state** plus a **mode**:

- Locked idle: `locked` or `mail` face, badge, dim backlight, optional rare blink. No body, no autoplay clip.
- PIN: pad overlay; face can watch.
- Unlocked inbox: still short text / photo / play controls — face can shrink, not vanish if FPS allows.
- Recording: `listen` for as long as mute is held; mic gated by the same GPIO.
- Hangout: `listen` when we have the floor; otherwise a calm “live” face. Follow p09/h12.

SFX fire on edges (press, new, error), not on every LVGL frame.

Art can be replaced in `assets/face/` without changing `persona_state`. Parent photo/voice packing is a **separate** track: [`PERSONA-ASSETS.md`](PERSONA-ASSETS.md) (p10/p11). Two children can share the engine and differ only by palette/sprites — later, and not in p01–p09.

## Decisions this plan makes

- Personality is **local animation + chirps**, not a cloud character and not a pet sim.
- Placeholder geometry first; character design is not a demo blocker.
- Locked idle may be a face + **count**, never a preview.
- Acknowledgements are non-speech SFX, mute-able.
- If animation fights playback, **freeze the face** (p09 B). That is an allowed product outcome, not a failure to “make it cute.”
