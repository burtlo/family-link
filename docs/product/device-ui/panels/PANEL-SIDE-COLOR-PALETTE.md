# Side color-palette panel

| Field | Value |
|---|---|
| ID | `PANEL-SIDE-COLOR-PALETTE` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — 80 × 240 rail and zig-zag swatch placement from owner-decided sketch layout |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `side` on [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md) |
| Experience | [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) |

## Purpose

Fills the right side slot with the six-color record palette. The panel owns
rail background, swatch placement, and which swatch shows the selected outline.
Each chip is [`COMP-COLOR-SWATCH`](../components/COMP-COLOR-SWATCH.md).

It does not own stroke capture. Selecting a color applies to **new** ink on
[`PANEL-FOCUSED-DRAWING`](PANEL-FOCUSED-DRAWING.md).

## Chrome, content, and composition

| Layer | Rule |
|---|---|
| Background | Paint **`fl-color-surface`** (`#101418`) on the full side `slot_chrome_bounds` (80 × 240; canvas `x=[240,320)`, `y=[0,240)`) |
| Children | Six `COMP-COLOR-SWATCH` instances, bottom to top, zig-zag |
| Insets | None. Placement uses the rail geometry below, not `fl-margin-screen` |
| Full-bleed | Swatches sit inside the 80 px rail; they MUST NOT cross `x=240` |
| Clip | The rail clips to `slot_chrome_bounds` |

```text
side slot_chrome_bounds (80 × 240; x=240…319)
(240,0)                            (320,0)
┌────────────────────────────────────────┐
│         ○ blue          (291, 28)      │
│    ○ red                (269, 64)      │
│         ○ green         (291,100)      │
│    ○ gold               (269,136)      │
│         ○ white *       (291,172)      │
│    ○ black              (269,208)      │
│  (* default selected)                  │
└────────────────────────────────────────┘
(240,240)                          (320,240)
```

Centers are **canvas-absolute**. Slot-relative `x` is `canvas_x − 240`.

## Geometry

Rail midline is canvas `x=280`. Even steps (from the bottom) offset **−11 px**;
odd steps offset **+11 px**. Vertical pitch is **36 px**. The bottom of the
lowest swatch is **10 px** above the LCD bottom.

| Constant | Value |
|---|---|
| Rail | 80 × 240 at `(240, 0)` |
| Swatch | 44 × 44 circle (radius 22) |
| Horizontal zig-zag | ±11 px from `x=280` |
| Vertical pitch | 36 px between centers |
| Bottom margin | 10 px (LCD bottom to bottom of lowest swatch) |
| Lowest center y | `240 − 10 − 22 = 208` |

Placement, `i` in `{0…5}` bottom to top:

```text
center_x = 280 + ((i % 2 == 0) ? −11 : +11)
center_y = 208 − (i × 36)
slot_x   = center_x − 240 − 22
slot_y   = center_y − 22
```

| Index | Role | Fill | Center (canvas) | Top-left in rail | Top-left on canvas |
|---|---|---|---|---|---|
| 0 | Background / “erase” | `#000000` | (269, 208) | (7, 186) | (247, 186) |
| 1 | Ink (default selected) | `#F8F8F8` | (291, 172) | (29, 150) | (269, 150) |
| 2 | Ink | `#E8C040` | (269, 136) | (7, 114) | (247, 114) |
| 3 | Ink | `#4CAF50` | (291, 100) | (29, 78) | (269, 78) |
| 4 | Ink | `#E53935` | (269, 64) | (7, 42) | (247, 42) |
| 5 | Ink | `#5AA0E8` | (291, 28) | (29, 6) | (269, 6) |

Default selection on entry: **index 1 (white)**.

## Visual states

| State | Presentation |
|---|---|
| Resting | Every swatch has the 2 px gray ring from `COMP-COLOR-SWATCH` |
| Selected | The selected swatch adds the 2 px gold outline with 1 px pad; others have no outline |
| Press | Selection changes on **press** (finger down) on a swatch circle, not on drag-across |

Changing selection invalidates only the previous and next swatch rectangles.

## Interaction and ownership

Hit testing is **circular**, radius 22 px around each center. A press whose
point is inside a circle selects that index. Rectangular widget bounds may be
larger; the circle test is the product rule.

| Touch | Result |
|---|---|
| Press inside a swatch circle | Select that color; gold outline moves; no ink |
| Press on the rail but outside every circle | Ignore; not ink, not a selection |
| Drag from canvas into the rail | Stroke ends; rail gaps never become ink |
| Drag across swatches | Do not change selection while moving; only a new **press** selects |

The 44 px diameter is below `fl-touch-min-height` (48 px). That size is locked
by `SK-007` for this palette.

Typical updates: **Level 0** (two swatch outlines). Must not rebuild the focused
canvas.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
- [`COMP-COLOR-SWATCH`](../components/COMP-COLOR-SWATCH.md)
- [`SCR-RECORD-SKETCH`](../screens/SCR-RECORD-SKETCH.md)
- [`THEME-DARK`](../themes/THEME-DARK.md) — rail fill `fl-color-surface`

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-22)* Stored points use RGB565 inside `flsk2` (`OPEN-SKP-01`). Placement is unchanged | — |

## Readiness

- **Owner review:** Rail size, zig-zag centers, ring/outline, and circular hit
  test are ready for correction.
- **Visual approval:** Pending catalog review on device.
- **Implementation:** Placement math is specified.
- **Verification:** `CAT-17`–`CAT-19` mount this panel with different selected indexes.

## Revision history

- **0.1 — 2026-10-21:** Initial side-slot palette from `SK-005`–`SK-007` and the
  lab-10 color rail.
