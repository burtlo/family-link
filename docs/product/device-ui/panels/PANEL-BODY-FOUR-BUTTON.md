# Four-button body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-FOUR-BUTTON` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — 2×2 grid measured and owner-accepted on device; no formal visual approval claimed |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | None yet. This panel owns the four-button grid. Labels and actions come from the mounting screen or experience. |

## Purpose

A 2×2 button grid that fills the lower two-thirds of the split-pane shell.
Use it when the lower slot offers four equal primary actions and no
collection or carousel.

This is not [`PANEL-BODY-PIN-GRID`](PANEL-BODY-PIN-GRID.md). That panel is a
3×3 digit keypad with 4 px gaps. This panel is four 136 × 56 buttons with a
uniform **16 px** gutter.

The panel owns geometry, type size, and the visible name length. It does not
own what the buttons do.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner, 2026-10-21 | Owner measurement and acceptance | 16 px gutter, 136 × 56 buttons, cell origins, `fl-type-16` labels, 15-character name limit |
| [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) | Draft shell | Lower `slot_chrome_bounds` 320 × 160 |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | `fl-type-16`, Montserrat **600**, `fl-touch-min-height` 48–56 |
| [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md) | Foundation | Buttons exceed `fl-button-min-width` 101 and `fl-button-min-height` 48 |

The measurement name below is a length fixture, not product copy.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint the panel fill on the full lower `slot_chrome_bounds` (320 × 160; canvas y=[80, 240)). Inherit the shell or use a distinct lower surface |
| Grid | Place all four buttons in the chrome rectangle using the 16 px gutter. Do not re-apply `fl-margin-screen` (12 px); the 16 px gutter is this panel's grid pad |
| Full-bleed list | The four buttons use the full 320 × 160 slot. They do not sit on `slot_content_bounds` (which would be a 12 px inset) |
| Clip | Buttons MUST stay inside `slot_chrome_bounds` and MUST NOT cross the split at y=80 |

## Anatomy

Coordinates in the tables are **slot-relative** unless marked canvas. The
lower slot origin is canvas `(0, 80)`.

```text
lower slot_chrome_bounds (320 × 160)
(0,80)                             (320,80)
┌────────────────────────────────────────┐
│ 16                                     │
│   ┌──136×56──┐ 16 ┌──136×56──┐         │  slot y=16  (canvas y=96)
│   │          │    │          │         │
│   └──────────┘    └──────────┘         │
│         16                             │
│   ┌──136×56──┐ 16 ┌──136×56──┐         │  slot y=88  (canvas y=168)
│   │          │    │          │         │
│   └──────────┘    └──────────┘         │
│ 16                                     │
└────────────────────────────────────────┘
(0,240)                            (320,240)
```

## Geometry

Uniform gutter **16 px** on every outside edge and between adjacent buttons.

```text
width:  16 + 136 + 16 + 136 + 16 = 320
height: 16 +  56 + 16 +  56 + 16 = 160
column stride: 152
row stride: 72
```

| Cell | Slot (x, y) | Canvas (x, y) | Size |
|---|---|---|---|
| Top-left | (16, 16) | (16, 96) | 136 × 56 |
| Top-right | (168, 16) | (168, 96) | 136 × 56 |
| Bottom-left | (16, 88) | (16, 168) | 136 × 56 |
| Bottom-right | (168, 88) | (168, 168) | 136 × 56 |

Placement formula, `col` and `row` in `{0, 1}`:

```text
x = 16 + col × 152
y = 16 + row × 72
```

| Spacing | Value |
|---|---|
| Left inset | 16 px |
| Right inset | 16 px |
| Top inset | 16 px |
| Bottom inset | 16 px |
| Column gap | 16 px |
| Row gap | 16 px |
| Button size | 136 × 56 px |

Do not restore a hint label above the grid. The four buttons are the only
content in this panel.

## Typography and names

| Property | Value |
|---|---|
| Token | `fl-type-16`, weight **600** |
| Color | `fl-color-text-primary` |
| Alignment | Centered in the button, horizontally and vertically |
| Lines | One. Names do not wrap |

| Role | Max visible characters | Measurement fixture (not product copy) |
|---|---|---|
| Button name | **15** | `Settings Polars` |

Rules:

- Copy authors MUST keep each name at or under 15 characters for the label
  to remain fully visible at `fl-type-16` in a 136 × 56 button.
- Extra characters are not visible; ellipsize the end with ASCII `...` if a
  caller supplies a longer string.
- These counts were measured with mixed-case Montserrat. All-caps or many
  wide glyphs (W, M) MAY overflow sooner; shorten the name rather than
  changing type size or button width.

## Visual states

| State | Presentation |
|---|---|
| Default | Four buttons in the grid above, each with a one-line name |
| Fewer actions | Not part of this panel. A two-button or three-button body needs its own ID |
| Long name | Apply the 15-character limit and ellipsis rule. Do not wrap or shrink type |
| Update | Typical **Level 0**: replace names without rebuilding the upper slot |

Pressed, disabled, and focused fills are not measured.

## Interaction and ownership

Each cell is a primary tap target (56 px height meets `fl-touch-min-height`).
Hit areas are the 136 × 56 rectangles. The mounting screen owns which action
each cell runs, when a cell is available, and any modal that follows a tap.

This panel does not scroll.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md)
- [`THEME-DARK`](../themes/THEME-DARK.md)

Pairs naturally with [`PANEL-HEADER-TITLE-SUBTITLE`](PANEL-HEADER-TITLE-SUBTITLE.md)
in the upper slot. No `SCR-*` mounts this panel yet.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Button fill, corner radius, and pressed/disabled appearance | Visual approval of chrome, not the grid math |
| `UI-OPEN-002` | Whether an empty or unavailable cell hides, dims, or stays labeled | Screens with fewer than four live actions |

## Readiness

- **Owner review:** Grid math, type size, and the 15-character name limit are ready for correction.
- **Visual approval:** Grid spacing was accepted on device. Button chrome (fill, radius, press) is not approved.
- **Implementation:** Agents MAY use this geometry in new UI. No consuming screen is specified.
- **Verification:** No formal evidence pack is claimed.

## Revision history

- **0.1 — 2026-10-21:** Initial four-button lower-slot grid from owner-measured
  16 px gutter, 136 × 56 cells, `fl-type-16` names, and 15-character limit.
