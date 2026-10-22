# Record canvas component

ID: `COMP-RECORD-CANVAS`
Type: Component
Revision: 0.1
Status: Draft — 240 × 240 black drawing surface from owner-decided sketch layout
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

The paintable record surface on [`SCR-RECORD-SKETCH`](../screens/SCR-RECORD-SKETCH.md).
Behavior (when capture starts, what is stored, how playback frames the sketch)
stays in [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md).

## Anatomy

```text
┌────────────── 240 ──────────────┐
│                                 │
│     black fill #000000          │  240 tall
│     8 px corner radius          │
│     clip to rounded rect        │
│                                 │
└─────────────────────────────────┘
```

| Region | Content | Rule |
|---|---|---|
| Face | Solid fill | `#000000` (`fl-color-sketch-canvas`) |
| Corners | 8 px radius | Clip ink to the rounded rectangle (`EXP-SKP-007`) |
| Ink | Selected palette color | One logical pixel is a **2 × 2** display block |

## Geometry

Coordinates are **canvas-absolute** when this component fills the focused slot
at `(0, 0)`.

| Constant | Value |
|---|---|
| Display size | **240 × 240** |
| Origin | **(0, 0)** — left strip, full LCD height |
| Corner radius | **8 px** |
| Logical sketch | **120 × 120** |
| Display-to-logical | divide by **2** (`SK-004`, `EXP-SKP-001`) |

Touches in display `x=[0,239]`, `y=[0,239]` map to logical
`x=[0,119]`, `y=[0,119]`. Points with display `x >= 240` are outside this
component.

## Visual states

| State | Presentation |
|---|---|
| Empty | Black fill, no strokes |
| Active | Black fill plus ink in the selected color. Background-color strokes paint `#000000` (erase appearance; `EXP-SKP-006`) |
| Clipped corner | Ink that would sit outside the 8 px radius is not visible |

## Interaction surface

The hit area is the **240 × 240 rectangle**. Press, move, release, and leave on
that rectangle are drawing input. This component does not select colors.

Catalog review may paint ink immediately so the surface can be tried. Product
record still maps touches into the 120 × 120 logical canvas before storage.

## Tokens and assets

| Token / value | Use |
|---|---|
| `fl-color-sketch-canvas` (`#000000`) | Empty fill and background-color ink |
| `fl-card-roster-radius` (**8 px**) | Corner radius, matching card language |

No typography. No icon.

## Accessibility and hardware constraints

RGB565 quantization applies. Exact desktop-monitor hex match is not required.
The 240 × 240 hit area meets `fl-touch-min-height`.

## Traceability

| Source | Contribution |
|---|---|
| `SK-004`, `EXP-SKP-003` | 240 × 240 display canvas, 2× to 120 × 120 |
| `EXP-SKP-007` | 8 px corner radius |
| Lab-10 record canvas | Black fill, clip-corner, 2 × 2 display pixels per logical pixel |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Stroke preview weight beyond the 2 × 2 logical-pixel block | Fine ink appearance |

## Readiness

- **Owner review:** Size, fill, radius, and touch boundary are ready for correction.
- **Visual approval:** Pending catalog review.
- **Implementation:** Geometry is specified.
- **Verification:** Mounted by `PANEL-FOCUSED-DRAWING` in `CAT-17`–`CAT-19`.

## Revision history

- **0.1 — 2026-10-21:** Initial record canvas from `SK-004` and the lab-10
  drawing surface.
