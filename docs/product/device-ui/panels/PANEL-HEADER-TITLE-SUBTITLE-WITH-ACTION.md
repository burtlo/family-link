# Header title + subtitle with action panel

| Field | Value |
|---|---|
| ID | `PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION` |
| Type | Panel module |
| Revision | 0.3 |
| Status | Draft — placement measured; subtitle wrap and title length measured; action name still estimated |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | None yet. This panel owns presentation. Copy and the action’s effect come from the mounting screen or experience. |

## Purpose

Upper-slot header with a one-line title, a two-line subtitle, and one trailing
action button (for example **Back**). Use this when the upper third must name
the screen, carry a short supporting line, and offer one header action.

This is not [`PANEL-HEADER-TITLE-SUBTITLE`](PANEL-HEADER-TITLE-SUBTITLE.md).
That panel has no trailing control and uses the full 296 px content width for
title and subtitle.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner, 2026-10-21 | Owner measurement | Title, subtitle, and action origins; action 101 × 56; 12 px inset on the action’s top, right, and bottom; subtitle wrap lengths |
| [`PANEL-HEADER-TITLE-SUBTITLE`](PANEL-HEADER-TITLE-SUBTITLE.md) | Measured sibling | Title and subtitle type, colors, and vertical placement |
| [`PANEL-BODY-FOUR-BUTTON`](PANEL-BODY-FOUR-BUTTON.md) | Measured sibling | 15-character name in a 136 × 56 `fl-type-16` button, used to estimate the 101 px action |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | `fl-margin-screen` **12 px**; `fl-type-24` / `fl-type-16`; `fl-touch-min-height` prefers 56 px |
| [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md) | Foundation | Action width is `fl-button-min-width` **101 px** |

Title and action names in the lab are length fixtures, not product copy. The
subtitle fixture below is the owner-measured wrap.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` (or `fl-color-surface-upper`) on the full upper `slot_chrome_bounds` (320 × 80; y=[0, 80)) |
| Content | Title, first subtitle line, and the action sit in upper `slot_content_bounds` (296 × 56; x=[12, 308), y=[12, 68)) |
| Full-bleed | The wrapped second subtitle line MAY use the 12 px band between y=68 and the split at y=80. It MUST remain inside `slot_chrome_bounds` |
| Insets | Do not re-apply `fl-margin-screen`. The 12 px action gutters are the shell inset |
| Full-bleed list | Second subtitle line only |

## Anatomy

Coordinates are relative to the upper slot origin, which is the canvas origin
`(0, 0)`.

```text
upper slot_chrome_bounds (320 × 80)
(0,0)                              (320,0)
┌────────────────────────────────────────┐
│  Title              ┌────101×56────┐  │  title (12, 12); action (207, 12)
│  Subtitle line one  │    Action    │  │  subtitle (12, 41), width 183
│  Subtitle line two  └──────────────┘  │  12 px top / right / bottom on action
└────────────────────────────────────────┘
(0,80)                             (320,80)
```

## Geometry and typography

| Element | Token | Color | Origin | Box | Lines |
|---|---|---|---|---|---|
| Title | `fl-type-24`, weight **600** | `fl-color-text-primary` | x=12, y=12 | width **183**; height one type line | One |
| Subtitle | `fl-type-16`, weight **600** | `fl-color-text-secondary` | x=12, y=41 | width **183**; wraps to two lines | Two |
| Action | `fl-type-16`, weight **600** | `fl-color-text-primary` | x=207, y=12 | **101 × 56**; label centered | One |

| Spacing | Value | How it is derived |
|---|---|---|
| Left inset (title / subtitle) | 12 px | `fl-margin-screen` / `slot_content_bounds` |
| Title top inset | 12 px | Same shell inset |
| Title origin → subtitle origin | 29 px | y=41 − y=12 |
| Gap under a 24 px title box | 5 px | y=41 − (12 + 24) |
| Text column width | 183 px | Action x − left inset − 12 px gap = 207 − 12 − 12 |
| Gap, text column → action | 12 px | Same shell inset |
| Action size | 101 × 56 px | `fl-button-min-width` × (80 − 12 − 12) |
| Action right inset | 12 px | 320 − 12 − 101 = 207 origin |
| Action top / bottom inset | 12 px / 12 px | 80 − 12 − 56 = 12 |

Check:

```text
12 + 183 + 12 + 101 + 12 = 320
12 + 56 + 12 = 80
```

Do not vertically center the title. Keep the subtitle at y=41 so two lines
keep descenders inside the 80 px chrome. Do not move the action off the
12 px top, right, or bottom inset.

## Content limits

Counts include every visible character, including spaces and punctuation.

| Role | Max | Lines | Source |
|---|---|---|---|
| Title | **15** (measured) | 1 | `Profile Setting` in the 183 px `fl-type-24` column |
| Subtitle line 1 | **23** (measured) | 1 of 2 | Owner-amended wrap on the 183 px column |
| Subtitle line 2 | **22** (measured) | 2 of 2 | Owner-amended wrap on the 183 px column |
| Subtitle total | **46** in the source string (23 + wrapping space + 22) | 2 | Same fixture |
| Action name | **11** (estimated) | 1 | Scaled from 15 chars in a 136 px `fl-type-16` button |

Subtitle measurement fixture (not product copy):

```text
Subtitle wraps twice g. Forty-seven character.
|---------------------| |--------------------|
         23                      22
```

Line 1 is `Subtitle wraps twice g.` (23). Line 2 is `Forty-seven character.`
(22). The space after `g.` is the wrap point and is not drawn on line 2.

Rules:

- Copy authors MUST keep subtitle line 1 to 23 characters and line 2 to 22
  characters at `fl-type-16` in the 183 px column.
- Title does not wrap. Stay within **15** characters at `fl-type-24` in the
  183 px column. Action names do not wrap; the 11-character action limit is
  still an estimate.
- Extra subtitle characters are not visible; ellipsize the end with ASCII
  `...` if a caller supplies a longer string.
- Mixed-case Montserrat was the measurement basis. All-caps or many wide
  glyphs (W, M) MAY overflow sooner.

## Visual states

| State | Presentation |
|---|---|
| Default | Title, two-line subtitle, and one visible action as above |
| Action hidden | Do not use this panel. Use `PANEL-HEADER-TITLE-SUBTITLE` |
| Long copy | Apply the line limits and ellipsis rules. Do not add a third subtitle line or a second action |
| Update | Typical **Level 0**: replace strings without rebuilding the lower slot |

Pressed, disabled, and focused action fills are not measured.

## Interaction and ownership

The action is the only touch target in this panel. Its hit area is the
101 × 56 rectangle. The mounting screen owns the label, when the control is
available, and what the tap does (for example return from Settings).

Title and subtitle are display-only.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`PANEL-HEADER-TITLE-SUBTITLE`](PANEL-HEADER-TITLE-SUBTITLE.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md)
- [`THEME-DARK`](../themes/THEME-DARK.md)

Pairs naturally with a settings or detail body in the lower slot. No `SCR-*`
mounts this panel yet.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Confirm the estimated action (11) limit on device. Title is measured at **15**. Subtitle is measured: 23 then 22. | Copy that must fill the action |
| `UI-OPEN-002` | Action fill, corner radius, and pressed/disabled appearance | Visual approval of chrome, not the grid math |

## Readiness

- **Owner review:** Placement, action size, and subtitle wrap lengths are ready for correction.
- **Visual approval:** Action insets and subtitle wrap lengths were accepted on device. Title length is measured at 15 on the LVGL Montserrat Medium 24 stand-in. Action name length is still an estimate. Button chrome is not approved.
- **Implementation:** Agents MAY use this geometry in new UI. No consuming screen is specified.
- **Verification:** No formal evidence pack is claimed.

## Revision history

- **0.3 — 2026-10-21:** Title max is **15** on the 183 px `fl-type-24` column.
  The previous 16-character fixture (`Profile Settings`) overflowed by about
  3 px and wrapped. Column width stays 183 px; copy shortened to
  `Profile Setting`.
- **0.2 — 2026-10-21:** Subtitle wrap measured on device: 23 characters, then
  22. Total source string 46.
- **0.1 — 2026-10-21:** Initial title + subtitle + action header from
  owner-measured 12 px action insets and 101 × 56 control; character limits
  scaled from earlier full-width and four-button measurements.
