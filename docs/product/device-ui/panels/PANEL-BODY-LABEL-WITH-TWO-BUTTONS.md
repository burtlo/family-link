# Label with two buttons body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-LABEL-WITH-TWO-BUTTONS` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — placement and label length measured; button name length estimated |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | None yet. This panel owns the label-and-two-button stack. Copy and actions come from the mounting screen or experience. |

## Purpose

A one-line guidance label over two full-width stacked buttons in the lower
two-thirds of the split-pane shell. Use it when the lower slot offers two
primary actions and a short supporting line.

This is not [`PANEL-BODY-FOUR-BUTTON`](PANEL-BODY-FOUR-BUTTON.md). That panel
is a 2×2 grid with a 16 px gutter and no label.

The panel owns geometry, type size, and visible character limits. It does not
own what the buttons do.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner, 2026-10-21 | Owner measurement | 12 px left, right, and bottom inset; 12 px gap between buttons; label origin and 38-character one-line limit |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | `fl-margin-screen` **12 px**; `fl-type-16`; `fl-touch-min-height` 48–56 |
| [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md) | Foundation | Button height is `fl-button-min-height` **48 px**; width exceeds `fl-button-min-width` 101 |
| [`PANEL-BODY-FOUR-BUTTON`](PANEL-BODY-FOUR-BUTTON.md) | Measured sibling | 15-character name in a 136 px `fl-type-16` button, used to estimate these 296 px names |

The strings below are length fixtures, not product copy.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint the panel fill on the full lower `slot_chrome_bounds` (320 × 160; canvas y=[80, 240)). Inherit the shell or use a distinct lower surface |
| Content | Place the label and both buttons on the 12 px shell inset. Do not re-apply `fl-margin-screen` |
| Full-bleed list | None. All content stays in lower `slot_content_bounds` (296 × 136; canvas y=[92, 228)) |
| Clip | Content MUST stay inside `slot_chrome_bounds` and MUST NOT cross the split at y=80 |

## Anatomy

Coordinates in the tables are **slot-relative** unless marked canvas. The
lower slot origin is canvas `(0, 80)`.

```text
lower slot_chrome_bounds (320 × 160)
(0,80)                             (320,80)
┌────────────────────────────────────────┐
│  Label one line, width 296             │  (12, 12)
│  ┌────────────296×48─────────────┐     │  (12, 40)
│  └───────────────────────────────┘     │
│  12                                    │
│  ┌────────────296×48─────────────┐     │  (12, 100)
│  └───────────────────────────────┘     │
│  12                                    │
└────────────────────────────────────────┘
(0,240)                            (320,240)
```

## Geometry

Uniform **12 px** inset (`fl-margin-screen`) on the left, right, and bottom.
The same 12 px is the gap between the two buttons.

```text
12 + 296 + 12 = 320
12 + 16 + 12 + 48 + 12 + 48 + 12 = 160
```

The 16 px in that height sum is one `fl-type-16` line box under the 12 px
top inset.

| Element | Slot (x, y) | Canvas (x, y) | Size |
|---|---|---|---|
| Label | (12, 12) | (12, 92) | width **296**; one type line |
| Top button | (12, 40) | (12, 120) | **296 × 48** |
| Bottom button | (12, 100) | (12, 180) | **296 × 48** |

| Spacing | Value | How it is derived |
|---|---|---|
| Left inset | 12 px | `fl-margin-screen` / `slot_content_bounds` |
| Right inset | 12 px | Same |
| Top inset (label) | 12 px | Same |
| Bottom inset | 12 px | 160 − 12 − 48 = 100 origin for the lower button |
| Label → top button | 12 px under a 16 px line | y=40 − (12 + 16) |
| Gap between buttons | 12 px | y=100 − (40 + 48) |
| Button size | 296 × 48 px | 320 − 12 − 12 wide; `fl-button-min-height` tall |

Do not add a third button or a second label line. Two label lines would
collide with the top button at y=40.

## Typography and names

| Element | Token | Color | Alignment | Lines |
|---|---|---|---|---|
| Label | `fl-type-16`, weight **600** | `fl-color-text-primary` | Left | One |
| Button name | `fl-type-16`, weight **600** | `fl-color-text-primary` | Centered in the button | One |

| Role | Max | Source | Fixture (not product copy) |
|---|---|---|---|
| Label | **38** (measured) | Owner added one character to the 37-character estimate on the 296 px column | `Preview is local until Save. extra ggg` |
| Button name | **32** (estimated) | 15 chars in 136 px scaled to 296 px: 15 × 296 / 136 ≈ 32.6, floored. Same result from 11 × 296 / 101 | `Profile accent and avatar pad gg` / `Sketch stroke color max line 32g` |

Rules:

- Copy authors MUST keep the label to 38 characters and one line.
- Button names SHOULD stay at or under 32 characters until that limit is
  measured on this width. Names do not wrap.
- Extra characters are not visible; ellipsize the end with ASCII `...` if a
  caller supplies a longer string.
- Mixed-case Montserrat was the measurement basis. All-caps or many wide
  glyphs (W, M) MAY overflow sooner.

## Visual states

| State | Presentation |
|---|---|
| Default | One label and two stacked buttons as above |
| One action | Not part of this panel. A single-button body needs its own ID |
| Four actions | Use `PANEL-BODY-FOUR-BUTTON` |
| Long copy | Apply the limits and ellipsis rules. Do not wrap the label or names |
| Update | Typical **Level 0**: replace strings without rebuilding the upper slot |

Pressed, disabled, and focused fills are not measured.

## Interaction and ownership

Each button is a primary tap target (48 px meets `fl-button-min-height`).
Hit areas are the 296 × 48 rectangles. The mounting screen owns which action
each button runs and any modal that follows a tap.

The label is display-only. This panel does not scroll.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md)
- [`THEME-DARK`](../themes/THEME-DARK.md)

Pairs naturally with
[`PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION`](PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md)
in the upper slot. No `SCR-*` mounts this panel yet.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Confirm the estimated 32-character button names on device | Copy that must fill the 296 px buttons |
| `UI-OPEN-002` | Button fill, corner radius, and pressed/disabled appearance | Visual approval of chrome, not the stack math |

## Readiness

- **Owner review:** Stack math, 12 px gutters, and the 38-character label are ready for correction.
- **Visual approval:** Gutters and label length were accepted on device. Button name length is estimated. Button chrome is not approved.
- **Implementation:** Agents MAY use this geometry in new UI. No consuming screen is specified.
- **Verification:** No formal evidence pack is claimed.

## Revision history

- **0.1 — 2026-10-21:** Initial label + two-button lower-slot stack from
  owner-measured 12 px gutters and 38-character label; button names scaled
  from the four-button and header-action measurements.
