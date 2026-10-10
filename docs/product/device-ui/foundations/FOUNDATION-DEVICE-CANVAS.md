# Device canvas foundation

ID: `FOUNDATION-DEVICE-CANVAS`  
Type: Foundation  
Revision: 0.2  
Status: Draft — margin, touch, typography, and Montserrat decided; colors in THEME-DARK  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

This foundation defines the fixed canvas, content well, touch and slider
constraints, and the **approved v2 typography ladder** for all device UI
artifacts. Layouts, components, and screens MUST reference these tokens instead
of inventing local margins or ad hoc font sizes.

This document does not define behavioral copy, stage policy, or full color
palette. It does not authorize firmware implementation by itself.

## Canvas facts

| Fact | Value |
|---|---|
| Display size | 320 × 240 px, landscape |
| Origin | Upper-left; x increases right, y increases down |
| Color | RGB565 |
| System UI chrome | None inside the framebuffer |
| Bezel / physical controls | Outside the 320 × 240 canvas |

## Content well and margin

The default layout surface is inset from the display edge.

| Token | Value | Rule |
|---|---|---|
| `fl-margin-screen` | **12 px** | Uniform inset on all four sides of the 320 × 240 canvas |
| `fl-content-well` | **296 × 216 px** | Usable interior: x ∈ [12, 308), y ∈ [12, 228) |

```text
(0,0)                              (320,0)
┌────────────────────────────────────────┐
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│ 12 px margin (all sides)
│▒┌──────────────────────────────────┐▒│
│▒│                                  │▒│
│▒│     fl-content-well 296 × 216    │▒│
│▒│                                  │▒│
│▒└──────────────────────────────────┘▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
(0,240)                            (320,240)
```

**Rules:**

- Default screens place user-focused text and aligned controls inside
  `fl-content-well`.
- Full-bleed experiences (for example a drawing canvas) MUST use an explicit
  full-canvas layout and document why the margin is waived.
- On [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md), the shell defines
  per-slot **`slot_content_bounds`** using this same 12 px inset. The union of
  upper and lower content bounds matches `fl-content-well`. Panel modules paint
  backgrounds on full **`slot_chrome_bounds`** and place user-focused elements
  in content bounds per
  [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md). Panel modules
  MUST NOT re-apply `fl-margin-screen`.

## Touch and slider constraints

Inherited from the historical BOX brief and adopted for v2.

| Token / rule | Value | Application |
|---|---|---|
| `fl-touch-min-height` | **48–56 px** | Minimum height of any primary tap target (buttons, list rows, PIN keys, card hit areas). Implementations MUST NOT go below **48 px**. Prefer **56 px** when vertical space allows. |
| `fl-slider-track-min` | **18–24 px** | Minimum thickness of slider and scrubber tracks; use a fat knob for volume and timeline controls. |

Touch targets MUST remain fully inside their owning layout slot and MUST NOT
rely on hover or off-screen affordances.

## Typography ladder (v2)

### Owner decision

v2 uses **exactly three** native pixel sizes on the device:

| Token | Size | Role |
|---|---|---|
| `fl-type-16` | **16 px** | Body text, secondary labels, metadata, settings row labels, short status lines |
| `fl-type-24` | **24 px** | Primary headings, prominent names, section titles within a screen |
| `fl-type-32` | **32 px** | Hero emphasis: screen titles, large numeric keypad digits, short high-visibility status (for example connecting title) |

No other font sizes are permitted on v2 device UI unless this foundation is
revised and dependents are updated. Do not add **14**, **22**, **28**, or
intermediate sizes “for fit” without an owner-approved foundation change.

### Rationale (historical)

The v1 product image linked many Montserrat sizes (14, 16, 22, 24, 28, 32) with
overlapping roles. That ladder was partly driven by flash packaging, incremental
prompts, and implementation drift—not a deliberate three-tier visual system.
v2 intentionally **narrows** to **16 / 24 / 32** for reviewability and consistent
desk legibility.

### Typeface

| Item | Rule |
|---|---|
| Family | **Montserrat** — owner-approved for v2 (confirm flash/glyph budget at link time) |
| Weight | **500** default UI chrome; **600** for emphasized single-line titles and card names (matches v1 as-built roster) |
| Encoding | Use ASCII `...` not Unicode ellipsis on device (historical RGB565 / font glyph limitation) |

Color tokens live in [`THEME-DARK`](../themes/THEME-DARK.md), not in this document.

### Mapping guidance (non-normative examples)

Use tokens, not raw pixel literals, in component and screen specs:

- Sign-in heading “Sign in as ...”: `fl-type-24` unless owner specifies otherwise.
- Carousel sender metadata: `fl-type-16`.
- PIN pad digit labels: `fl-type-32`.
- Connecting main label: `fl-type-32` (replaces historical “28 or equivalent”).

Exact per-component choices belong in `COMP-*` and `SCR-*` documents.

## Spacing scale (initial)

Until a fuller spacing token set is approved, use these defaults:

| Token | Value | Use |
|---|---|---|
| `fl-margin-screen` | 12 px | Screen inset (see above) |
| `fl-space-8` | 8 px | Gap between related controls in a group |
| `fl-space-16` | 16 px | Separation between sections inside a slot |

Additional spacing tokens MAY be added in a foundation revision if a repeated
pattern appears in multiple components.

## Constraints and exceptions

- **RGB565:** Avoid 1 px hairline borders that disappear on hardware; prefer 2 px
  when a border must read clearly (see carousel unread treatment in historical
  specs).
- **No responsive breakpoints:** Single hardware target only.
- **v1 archive:** Historical pixel specs (20 px ribbons, 14 px carousel meta,
  etc.) are evidence only. They are superseded by this foundation for v2 unless
  explicitly re-adopted in a revised artifact.

## Traceability

| Source | Contribution |
|---|---|
| Product owner, 2026-10-10 | 12 px margin; 48–56 px touch; slider track minimum; typography **16 / 24 / 32** only |
| Historical BOX brief (v1 archive) | Original margin, touch, and slider guidance |
| Historical v1 LVGL font inventory | Rejected as v2 policy — too many near-duplicate sizes |

## Dependents

Layouts and screens SHOULD cite this foundation when specifying alignment,
padding, type size, or touch size:

- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Confirm Montserrat flash budget and required glyph subset on device | Firmware font linking |
| `UI-OPEN-002` | *(Superseded 2026-10-10)* Default colors — see [`THEME-DARK`](../themes/THEME-DARK.md) | — |
| `UI-OPEN-003` | *(Superseded 2026-10-10)* Split-pane insets — see `LAYOUT-SPLIT-PANE` 0.2 | — |

## Readiness

- **Owner review:** Ready for margin, touch, slider, typography, and Montserrat.
- **Visual approval:** Pending hardware check of Montserrat weights and theme on RGB565.
- **Implementation:** Agents MAY use these tokens in specs and new UI code; font
  family and full palette remain OPEN.

## Revision history

- **0.2 — 2026-10-10:** Montserrat approved; weights 500/600; colors delegated to `THEME-DARK`.
- **0.1 — 2026-10-10:** Initial foundation: 12 px margin / 296×216 well,
  touch and slider rules, typography ladder 16 / 24 / 32 (replacing v1
  multi-size Montserrat sprawl).
