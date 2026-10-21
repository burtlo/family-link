# Header title + subtitle panel

| Field | Value |
|---|---|
| ID | `PANEL-HEADER-TITLE-SUBTITLE` |
| Type | Panel module |
| Revision | 0.2 |
| Status | Draft — placement, type sizes, and visible character limits measured; no visual approval claimed |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | None yet. This panel owns presentation only. Copy comes from the mounting screen or experience. |

## Purpose

Two-line header for the upper split-pane slot: a single-line title with a
wrapping subtitle beneath it. Use this when the upper third must name the
screen and carry a short supporting line (status, guidance, or context).

This is not [`PANEL-HEADER-TITLE-LEFT`](PANEL-HEADER-TITLE-LEFT.md). That panel
is a single vertically centered heading with no subtitle.

This panel is display-only. It does not own a trailing action, navigation,
or experience copy. Title + subtitle + one header action is
[`PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION`](PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md).

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner, 2026-10-21 | Owner measurement | Title and subtitle origins, type sizes, and maximum visible character lengths on the 320 × 240 canvas |
| [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) | Draft shell | Upper `slot_chrome_bounds` 320 × 80; content inset 12 px |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | `fl-type-24`, `fl-type-16`, Montserrat **600**, `fl-margin-screen` 12 px |
| [`THEME-DARK`](../themes/THEME-DARK.md) | Draft theme | Title `fl-color-text-primary`; subtitle `fl-color-text-secondary` |

The measurement strings below are length fixtures, not product copy.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` (or `fl-color-surface-upper` when the screen uses two pane colors) on the full upper `slot_chrome_bounds` (320 × 80; y=[0, 80)) |
| Content | Place the title and the first subtitle line inside upper `slot_content_bounds` (296 × 56; x=[12, 308), y=[12, 68)) |
| Full-bleed | The wrapped second subtitle line MAY use the 12 px band between y=68 and the split at y=80. It MUST remain inside `slot_chrome_bounds` and MUST NOT cross into the lower slot |
| Insets | Do not re-apply `fl-margin-screen`. Origins already sit on the shell inset |
| Full-bleed list | Second subtitle line only. Title, first subtitle line, and all other chrome stay in content bounds |

## Anatomy

Coordinates are relative to the upper slot origin, which is the canvas origin
`(0, 0)`.

```text
upper slot_chrome_bounds (320 × 80)
(0,0)                              (320,0)
┌────────────────────────────────────────┐
│  Title one line                        │  (12, 12)  fl-type-24
│  Subtitle line one                     │  (12, 41)  fl-type-16, width 296
│  Subtitle line two (may enter y=68–80) │
└────────────────────────────────────────┘
(0,80)                             (320,80)
```

## Geometry and typography

| Element | Token | Color | Origin | Box | Lines |
|---|---|---|---|---|---|
| Title | `fl-type-24`, weight **600** | `fl-color-text-primary` | x=12, y=12 | width ≤ 296; height one type line | One |
| Subtitle | `fl-type-16`, weight **600** | `fl-color-text-secondary` | x=12, y=41 | width **296**; wraps to two lines | Two |

| Spacing | Value | How it is derived |
|---|---|---|
| Left inset | 12 px | Shell `slot_content_bounds` / `fl-margin-screen` |
| Title top inset | 12 px | Same shell inset |
| Title origin → subtitle origin | 29 px | y=41 − y=12 |
| Gap under a 24 px title box | 5 px | y=41 − (12 + 24) |
| Subtitle width | 296 px | Upper content width |

Do not vertically center the pair. The title stays at the content-band top;
the subtitle starts at y=41 so a two-line block keeps descenders (for example
**g**) inside the 80 px chrome.

## Content limits

These maxima are the longest strings that remained fully visible at the
specified sizes during owner measurement. They count every visible character,
including spaces and punctuation.

| Role | Max visible characters | Lines | Measurement fixture (not product copy) |
|---|---|---|---|
| Title | **26** | 1 | `Profile Settings for Lover` |
| Subtitle | **77** | 2 | `Card details. Settings previews. That should wrap now quit I tiny lil' aggro.` |

Rules:

- Copy authors MUST stay within these lengths for the text to remain fully
  visible at `fl-type-24` / `fl-type-16`.
- Title does not wrap. Extra title characters are not visible.
- Subtitle wraps to at most two lines. Extra subtitle characters are not
  visible; ellipsize the end with ASCII `...` if a caller supplies a longer
  string.
- These counts were measured with mixed-case Montserrat. All-caps or many
  wide glyphs (W, M) MAY overflow sooner; shorten the string rather than
  changing type size.

## Visual states

| State | Presentation |
|---|---|
| Default | Title and subtitle as supplied, placed as above |
| Title-only | Allowed. Keep the title at (12, 12). Do not move it to the vertical center of the slot (that is `PANEL-HEADER-TITLE-LEFT`) |
| Long copy | Apply the character limits and ellipsis rules above. Do not add a third subtitle line or a second title line |
| Update | Typical **Level 0**: replace title and/or subtitle strings without rebuilding the lower slot |

This panel has no empty, focused, disabled, or error chrome of its own. A
waiting or failure message is still a subtitle string supplied by the screen.

## Interaction and ownership

Display-only. No touch targets. A trailing control such as Back is
[`PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION`](PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md).

The mounting screen owns when the panel appears, the strings it shows, and
any sibling lower-slot module.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`THEME-DARK`](../themes/THEME-DARK.md)

No `SCR-*` mounts this panel yet.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-21)* A trailing header action is a separate panel: [`PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION`](PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md) | — |

## Readiness

- **Owner review:** Placement, type sizes, and character limits are ready for correction.
- **Visual approval:** Not claimed. Geometry is an on-device measurement.
- **Implementation:** Agents MAY use this geometry in new UI. No consuming screen is specified.
- **Verification:** No formal evidence pack is claimed.

## Revision history

- **0.2 — 2026-10-21:** Trailing-action question resolved as a separate panel ID.
- **0.1 — 2026-10-21:** Initial title + subtitle header from owner-measured
  upper-slot placement and maximum visible string lengths.
