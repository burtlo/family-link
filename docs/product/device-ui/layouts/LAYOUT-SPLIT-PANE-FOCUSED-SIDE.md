# Focused-side split-pane layout

ID: `LAYOUT-SPLIT-PANE-FOCUSED-SIDE`
Type: Layout (shell)
Revision: 0.1
Status: Draft — left/right geometry taken from owner-decided sketch record layout
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

`LAYOUT-SPLIT-PANE-FOCUSED-SIDE` is the **shell** for the record-sketch
composition: a left **focused** drawing surface and a right **side** color
palette. It defines:

- fixed left/right slot geometry on the full canvas;
- a **default parent background**;
- per-slot **`slot_chrome_bounds`** (these slots are edge-to-edge; there is no
  12 px content inset);
- mount points for **one swappable panel module per slot**.

It does not define the drawing strokes, palette colors, or capture lifecycle.
Those belong to [`PANEL-FOCUSED-DRAWING`](../panels/PANEL-FOCUSED-DRAWING.md),
[`PANEL-SIDE-COLOR-PALETTE`](../panels/PANEL-SIDE-COLOR-PALETTE.md), and
[`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md).

Do not use [`LAYOUT-SPLIT-PANE`](LAYOUT-SPLIT-PANE.md) (upper/lower ⅓ / ⅔) for
this screen. That shell's insets and split at `y=80` do not match the record
geometry.

## Composition model

```text
SCR-*
└─ LAYOUT-SPLIT-PANE-FOCUSED-SIDE
   ├─ focused (left)  → PANEL-*   (drawing surface)
   └─ side    (right) → PANEL-*   (color palette)
```

Screens declare which panel module mounts in each slot.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner, this request | Owner decision | Left focused panel + right side panel; capture placement from the lab-10 record canvas |
| `SK-004`, `EXP-SKP-003` | Experience, OWNER-DECIDED | 240 × 240 display paint area, left, full height |
| `SK-005`, `EXP-SKP-004` | Experience, SHOWCASE-AS-BUILT | 80 × 240 color rail on the right |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | 320 × 240 canvas; this layout **waives** `fl-margin-screen` |

## Shell background

| Item | Rule |
|---|---|
| Default | Shell paints **`fl-color-surface`** (`#101418` in [`THEME-DARK`](../themes/THEME-DARK.md)) across the full canvas |
| Focused override | The drawing panel paints the sketch canvas fill on its chrome bounds |
| Side override | The palette panel paints **`fl-color-surface`** on the 80 × 240 rail |

No visible divider is required at the split. The two fills meet at `x=240`.

## Canvas and slot chrome

The shell occupies the complete framebuffer. Slots are **edge-to-edge**
(full height; split at `x=240`). There is **no** `fl-margin-screen` inset.

```text
(0,0)                         (240,0)              (320,0)
┌───────────────────────────────┬──────────────────┐
│                               │                  │
│     focused slot_chrome       │  side slot_chrome│
│         240 × 240             │      80 × 240    │
│                               │                  │
└───────────────────────────────┴──────────────────┘
(0,240)                       (240,240)           (320,240)
```

| Region | `slot_chrome_bounds` | Size |
|---|---|---|
| `focused` | `x=[0,240)`, `y=[0,240)` | 240 × 240 |
| `side` | `x=[240,320)`, `y=[0,240)` | 80 × 240 |
| Split keyline | `x=240` | No required visible divider |

**`slot_content_bounds`** equals **`slot_chrome_bounds`** for both slots. This
layout exists specifically to waive the 12 px screen margin used by
`LAYOUT-SPLIT-PANE`. Panels MUST NOT re-apply `fl-margin-screen`.

## Slot contracts

### `focused` (left)

- Hosts exactly **one** drawing-class **`PANEL-*`** (the record canvas).
- Owns primary stroke touch inside `x=[0,240)`, `y=[0,240)`.
- A touch with `x >= 240` MUST NOT become ink, even if an input driver reports a
  wide point.
- Must clip painting to **`slot_chrome_bounds`**.

### `side` (right)

- Hosts exactly **one** palette-class **`PANEL-*`**.
- Owns color-selection touch. Hits that miss a swatch circle stay in the rail
  and MUST NOT become ink.
- Must clip to **`slot_chrome_bounds`**.

## Nesting and variants

This shell does not nest `LAYOUT-SPLIT-PANE`. Entry bloom, ring-wipe reveal, and
terminal outcome overlays are not part of the steady-state slot map.

## Visual and update invariants

- `x=240` and the 240 / 80 split remain stable while this shell is active.
- Changing the selected swatch MUST NOT rebuild the focused canvas subtree.
- Stroke invalidation stays inside the focused slot. Palette chrome invalidation
  stays inside the side slot.
- Follow [UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md).

## Accessibility and hardware constraints

- The focused slot is the drawing hit area (240 × 240).
- Circular swatches in the side slot are **44 px** diameter per `SK-007`. That
  is below `fl-touch-min-height` (48 px). This layout treats the 44 px circle as
  the hit target; do not enlarge the swatches without revising `SK-007`.
  [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md)
  does not apply to circular pickers.

## Traceability and consumers

| Decision | Shell behavior |
|---|---|
| `SK-004` | Focused slot 240 × 240 at `(0, 0)` |
| `SK-005` | Side slot 80 × 240 at `(240, 0)` |
| Margin waiver | Content bounds equal chrome bounds |

Consumers:

- [`SCR-RECORD-SKETCH`](../screens/SCR-RECORD-SKETCH.md)
- [`PANEL-FOCUSED-DRAWING`](../panels/PANEL-FOCUSED-DRAWING.md)
- [`PANEL-SIDE-COLOR-PALETTE`](../panels/PANEL-SIDE-COLOR-PALETTE.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-22)* No timer or recording indicator (`OPEN-SKP-05`, `EXP-SKP-008`) | — |

## Readiness

- **Owner review:** Left/right split and edge-to-edge slots are ready for correction.
- **Visual approval:** Pending on-device review of the catalog scenes.
- **Implementation:** Slot math is specified; panel modules are separate artifacts.
- **Verification:** Catalog scenes `CAT-17`–`CAT-19` mount this shell in `LAB-14`.

## Revision history

- **0.1 — 2026-10-21:** Initial left focused / right side split from `SK-004` /
  `SK-005` and the lab-10 record canvas.
