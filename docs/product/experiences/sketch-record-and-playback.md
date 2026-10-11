# Experience: sketch record and playback (card-aligned canvas)

| Field | Value |
|-------|--------|
| **Experience ID** | `EXP-SKETCH-RECORD-PLAYBACK` |
| **Revision** | 2 |
| **Status** | **OWNER-DECIDED** intent (2026-10-03 interview); **record-screen layout** aligned to **island demo h19** showcase (palette + canvas stub, no capture chrome on screen) |
| **Owner** | Lynn |
| **Surfaces** | BOX-3 record screen; carousel message card playback |
| **Classes** | Hardware-mediated, transactional (send), continuous (timed replay) |

## Source preservation

This specification was recovered from a **local Cursor agent transcript** on the owner’s machine (2026-10-03). It is written here so the decisions are not lost when that transcript is unavailable. Repository conversation key for the owner: `ab217574-edb8-49ed-ae41-d62514c4ad53`.

**Visual reference:** island demo **h19** record reveal — 240×240 black canvas, 80×240 color rail, six zig-zag swatches. That stub does **not** paint strokes, show a **recording timer**, or show a **recording indicator** on the record screen.

Supporting v1 evidence (as-built baseline, not approval of future behavior): [recording and send](../../v1-assessments/recording-send.md), [carousel and playback](../../v1-assessments/carousel-playback.md).

## Revision history

| Rev | Date | Change |
|-----|------|--------|
| 1 | 2026-10-10 | Initial spec transcribed from 2026-10-03 owner interview |
| 2 | 2026-10-10 | Record-screen geometry and color rail matched to h19 on-device showcase; timer and rec indicator removed from layout spec |

---

## 1. Scenario and intent

### Original owner input (summary)

Async messages combine **audio** with optional **drawing**. The owner initially wanted drawing over the **entire 320×240 screen** but changed direction so **recording and playback look like the same drawing**—framed like a **carousel card**, not a full-screen sketch squeezed into a small card on replay.

On **send**, the paintable area should appear as a **card on the record screen**, with **more drawing space than the inbox card** via an **upscale ratio**. On the right, the sender picks **stroke colors** from a short list. Strokes **store color**. There is **no eraser**; one palette entry matches the **background** (same effect as erase). Playback shows the sketch **inside the message card** with **chrome** so the drawing is framed.

### Intended qualities (observable)

- Recipient sees a sketch that matches what the sender drew—**same aspect ratio and framing**, not a remapped full-screen field.
- Sender has a **comfortable touch target** larger than the inbox card without changing the stored geometry.
- Color is **explicit per stroke**, not a single global ink.

---

## 2. Baseline (legacy v1 as-built)

Historical v1 behavior (for contrast only):

| Layer | As-built |
|--------|----------|
| Carousel card content | ~**132×134** px scroll card in the content band |
| Record | **Full-screen** 320×240 drawing; points in **screen** coordinates |
| Playback | Sketch replay into card-sized framebuffer with **coordinate rescaling** from full-screen space |
| Ink | **One** color per record and per card replay |
| Point payload | Time, phase, x, y — **no per-point color** in FLSK v1 |

That mismatch is why the owner asked for one **canonical logical canvas** shared by record, storage, and playback.

---

## 3. Locked decisions (intent ledger)

| ID | Decision | Status |
|----|----------|--------|
| SK-001 | **One logical coordinate system** for store, replay, and (via scale) record UI | OWNER-DECIDED |
| SK-002 | Logical canvas **120×120** — slightly inset inside the ~132 px card width; **1:1 replay** on the card (no per-message squeeze) | OWNER-DECIDED |
| SK-003 | Carousel card stays **~132×134** outer chrome; sketch **120×120** centered with optional **1 px** inner border so the drawing reads as framed | OWNER-DECIDED |
| SK-004 | Record screen: **240×240** **display** paint area (full LCD height, left side); maps to logical **120×120** at **2×** | OWNER-DECIDED |
| SK-005 | Record screen: **80×240** **color rail** on the right (`#101418` surface); **no** recording timer or recording indicator on this screen in the h19 showcase | SHOWCASE-AS-BUILT |
| SK-006 | **Multi-color** strokes; selected color applies to new touch ink; **no eraser tool** — background-matching color only | OWNER-DECIDED |
| SK-007 | Color rail: **44×44** circular swatches, **zig-zag** horizontal offset **±11 px** from rail center, **36 px** vertical pitch, **10 px** margin above bottom swatch; **2 px** gray border + **2 px** gold outline on selection (h19) | SHOWCASE-AS-BUILT |

### Palette (record rail — h19 showcase order, top to bottom in zig-zag stack)

Bottom swatch is index 0; stack grows upward. Default selection on entry: **white** (index 1).

| Order (bottom → top) | Role | Hex (24-bit, as in showcase) |
|----------------------|------|------------------------------|
| 1 | Background / “erase” | `#000000` |
| 2 | Ink | `#F8F8F8` |
| 3 | Ink | `#E8C040` (gold / product accent) |
| 4 | Ink | `#4CAF50` (green) |
| 5 | Ink | `#E53935` (red) |
| 6 | Ink | `#5AA0E8` (blue) |

Selection chrome: border `#B8BEC6`; selected outline `#E8C040`.

RGB565 packing for protocol work remains **OPEN** where it must match the global accent table.

---

## 4. Geometry reference

### Carousel playback (card)

```
┌──────────────── 132 ────────────────┐
│  card chrome (gradient, meta, dot)   │  ~134 tall
│    ┌──────── 120 ────────┐           │
│    │  sketch 120×120     │  framed   │
│    │  (1:1 logical)      │           │
│    └─────────────────────┘           │
└──────────────────────────────────────┘
```

### Record screen (320×240) — h19 showcase layout

Only the **canvas** and **color rail** are visible on the record screen. There is **no** elapsed-time label and **no** rec disk (or other recording indicator) on this surface.

```
 0                                                                                    319
 0 ┌──────────────────────────────────────── 240 ────────────────────────────────────────┬──────── 80 ────────┐
   │                                                                                     │  surface #101418   │
   │                                                                                     │                    │
   │                                                                                     │    ○ black         │
   │                                                                                     │      ○ white  *    │
   │                    PAINTABLE 240 × 240                                              │    ○ gold          │
   │                    black fill, 8 px corner radius                                   │      ○ green       │
   │                    (touch → logical 120×120 @ 2× when capture ships)               │    ○ red           │
   │                                                                                     │      ○ blue        │
   │                                                                                     │  (* default select)│
   │                                                                                     │  zig-zag ±11 px    │
239└─────────────────────────────────────────────────────────────────────────────────────┴────────────────────┘
     x=0 ──────────────────────────────── x=239 ─────────────────────────────────────── x=240 ─────────── x=319
```

| Region | Size | Position |
|--------|------|----------|
| LCD | 320 × 240 | — |
| Canvas (display) | **240 × 240** | origin **(0, 0)** — left strip, full height |
| Color rail | **80 × 240** | **(240, 0)** |
| Recording timer | — | **Not shown** on h19 record screen |
| Recording indicator | — | **Not shown** on h19 record screen |

**Coordinate mapping (record, when capture ships):** display (0…239, 0…239) → logical (0…119, 0…119) by dividing by 2; store **logical** coordinates and **per-stroke color** in the sketch blob.

**Entry (h19 only, not normative for product):** inbox list uses a red FAB bloom or physical PTT ring wipe to reveal this screen; that transition chrome is separate from the steady-state record layout above.

---

## 5. Color rail layout (swatches)

- **Control size:** **44×44** circular tap targets.
- **Rail width:** 80 px; swatches centered on rail midline with alternating **±11 px** horizontal offset (even steps left, odd steps right).
- **Vertical spacing:** **36 px** pitch between swatch centers (bottom-up placement); **10 px** from bottom edge of LCD to bottom of lowest swatch.
- **Selected state:** **2 px** border `#B8BEC6` on all swatches; selected swatch adds **2 px** outline `#E8C040` with **1 px** outline pad.
- **Dense / large tiers:** 40×40 and 54×54 chip sizes appear in other UI notes; **h19 showcase uses 44×44** zig-zag only.

---

## 6. Requirements

| ID | Requirement |
|----|-------------|
| EXP-SKP-001 | The system MUST store sketch points in a **single logical** width and height (**120×120** unless a later owner decision changes SK-002). |
| EXP-SKP-002 | Playback MUST render the sketch at **120×120** inside the message card without non-uniform scaling of stroke coordinates. |
| EXP-SKP-003 | The record screen MUST present a **240×240** paint surface and MUST map touches into logical space before append to the sketch buffer. |
| EXP-SKP-004 | The record screen MUST provide a **color rail** (80×240) with six swatches: black, white, gold, green, red, and blue, in the h19 order and geometry unless a later owner decision changes SK-007. |
| EXP-SKP-005 | Each stored point (or stroke segment policy TBD in protocol work) MUST carry **color** sufficient to replay accurately. |
| EXP-SKP-006 | The product MUST NOT offer a separate eraser; choosing the **background color** MUST paint with the canvas background color. |
| EXP-SKP-007 | Card chrome on record SHOULD match inbox card language (e.g. **8 px** corner radius on the canvas) so send and receive feel related. |
| EXP-SKP-008 | The steady-state record screen layout documented here MUST NOT require a on-screen recording timer or recording indicator unless a later owner decision adds them (see OPEN-SKP-05). |

---

## 7. Protocol and implementation notes (non-normative)

- Legacy **FLSK v1** points use an 8-byte record without color; this experience **requires a format revision** (e.g. FLSK2 point flags, or a new field) before multi-color sketches can ship.
- Audio timing remains the sketch clock when capture exists; color does not change capture start/stop rules.
- Island demo **h19** is a **UI stub**: palette selection updates chrome only; no stroke buffer or audio on the record surface.
- Web twin parity: color rail and 240×240 card canvas SHOULD mirror firmware when a twin exists.

---

## 8. Acceptance scenarios (draft)

| ID | Scenario | Pass |
|----|----------|------|
| ACC-SKP-01 | Sender draws a square in gold on the record card; recipient plays message | Square appears gold, same proportions, framed in card |
| ACC-SKP-02 | Sender switches white → red mid-stroke session | Replay shows two colors in correct time order |
| ACC-SKP-03 | Sender selects background color and draws | Strokes disappear visually; stored points do not corrupt replay |
| ACC-SKP-04 | Sender uses only audio, no touch | Message sends with no sketch attachment (unchanged audio path) |
| ACC-SKP-05 | Sender opens record screen (showcase) | Sees 240×240 canvas + six swatches only—no timer, no rec indicator |

---

## 9. Open items

| ID | Topic |
|----|--------|
| OPEN-SKP-01 | Final RGB565 hex for each rail color in wire/protocol form |
| OPEN-SKP-02 | Whether portrait/meta on the card is hidden during sketch replay (legacy overlay behavior) |
| OPEN-SKP-03 | Exact wire format for per-point color and backward compatibility with FLSK v1 |
| OPEN-SKP-04 | Owner approval checkpoint for full capture + send (this document records layout from h19 showcase plus 2026-10-03 canvas decisions) |
| OPEN-SKP-05 | Whether recording timer and/or recording indicator return on the record screen (removed from 2026-10-03 interview sketch in favor of h19 steady-state UI) |

---

## 10. Readiness

| Gate | Status |
|------|--------|
| Owner ordinary flow | **Decided** (2026-10-03) for canvas + multi-color intent |
| Record screen visuals | **Showcased** in island demo h19 (stub, no draw/send) |
| Exception / failure paths | **Partial** — inherit from [recording and send](../../v1-assessments/recording-send.md) unless extended |
| Product firmware (x02) | **Not started** under this spec |
| Protocol | **Blocked** on color-capable sketch format |
