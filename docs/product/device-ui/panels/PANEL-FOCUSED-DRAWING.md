# Focused drawing panel

| Field | Value |
|---|---|
| ID | `PANEL-FOCUSED-DRAWING` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — 240 × 240 black canvas placement from owner-decided sketch layout |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `focused` on [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md) |
| Experience | [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) |

## Purpose

Fills the left focused slot with the record drawing surface. The panel provides
the slot-level clip and contains
[`COMP-RECORD-CANVAS`](../components/COMP-RECORD-CANVAS.md).

It does not own capture start/stop, audio, send, or which color is selected.
Selected ink comes from [`PANEL-SIDE-COLOR-PALETTE`](PANEL-SIDE-COLOR-PALETTE.md).

## Chrome, content, and composition

| Layer | Rule |
|---|---|
| Background | The canvas fill **is** the panel background. Paint it on the full focused `slot_chrome_bounds` (240 × 240; canvas `x=[0,240)`, `y=[0,240)`) |
| Child | Mount one `COMP-RECORD-CANVAS` at slot origin `(0, 0)` |
| Insets | None. This shell waives `fl-margin-screen` |
| Full-bleed | The canvas uses the entire focused slot, including the 8 px rounded corners |
| Clip | Painting MUST stay inside `slot_chrome_bounds`. Rounded corners clip the visible fill |

```text
focused slot_chrome_bounds (240 × 240; x=0…239)
(0,0)                              (240,0)
┌────────────────────────────────────────┐
│                                        │
│         COMP-RECORD-CANVAS             │
│         240 × 240                      │
│         black fill, 8 px radius        │
│                                        │
└────────────────────────────────────────┘
(0,240)                            (240,240)
```

## Interaction and ownership

The panel owns the drawing hit area: the 240 × 240 rectangle. Touch down, move,
up, and leave on this surface are stroke input. Touches with `x >= 240` are not
this panel's ink path.

Catalog review (`LAB-14`) paints the selected color onto the canvas so the
surface can be tried on device. Product capture and the 2× logical mapping stay
in the experience and in the record-canvas lab.

Typical updates: **Level 1** inside the focused slot (stroke pixels). Must not
rebuild the side palette when only ink changes.

## Visual states

| State | Presentation |
|---|---|
| Empty | Black fill, no strokes |
| Drawing | Strokes in the currently selected ink (or background color) |
| Unavailable | Not part of this panel. Capture failures are screen/experience outcomes |

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
- [`COMP-RECORD-CANVAS`](../components/COMP-RECORD-CANVAS.md)
- [`SCR-RECORD-SKETCH`](../screens/SCR-RECORD-SKETCH.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Whether an unavailable/error treatment ever paints over this canvas | Failure presentation on the record screen |

## Readiness

- **Owner review:** Size, origin, black fill, and touch boundary are ready for correction.
- **Visual approval:** Pending catalog review on device.
- **Implementation:** Geometry is specified.
- **Verification:** `CAT-17`–`CAT-19` mount this panel.

## Revision history

- **0.1 — 2026-10-21:** Initial focused-slot drawing panel from `SK-004` and the
  lab-10 canvas.
