# User card component

ID: `COMP-USER-CARD`  
Type: Component  
Revision: 0.1  
Status: Draft — geometry and states owner-aligned  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

Visual contract for one hangout user card in horizontal carousels (sign-in roster,
message inbox, send-user picker). Behavior (tap, focus index, data binding) stays
in experience and screen specs.

## Anatomy

```text
┌──────────────────── 132 px ────────────────────┐
│  gradient face (user message color)            │
│           ┌──────────┐                         │
│           │ avatar   │  66 px circle            │
│           └──────────┘                         │
│              display name (fl-type-16)           │
└────────────────────────────────────────────────┘
        corner radius 8 px
```

| Region | Content | Token / rule |
|---|---|---|
| Face | Solid vertical gradient | `fl-color-user-message-top` → `fl-color-user-message-bottom` ([`THEME-DARK`](../themes/THEME-DARK.md)) |
| Avatar | User portrait or placeholder | **66 px** diameter — `fl-avatar-roster` (matches v1 sign-in roster as-built) |
| Name | Single line, centered, ellipsized | `fl-type-16`, weight **600**; color `fl-color-text-primary` unless light gradient requires inversion |

No secondary metadata on the sign-in roster card.

## Geometry (roster / carousel)

Values derive from v1 as-built roster carousel and are adapted to the v2 lower
split slot (160 px chrome height).

| Token | Value | Notes |
|---|---|---|
| `fl-card-roster-width` | **132 px** | Fixed card width |
| `fl-card-roster-height` | **133 px** | v1 card height; **vertically centered** in lower `slot_chrome_bounds` |
| `fl-card-roster-gap` | **12 px** | Horizontal gap between cards |
| `fl-card-roster-radius` | **8 px** | Corner radius |

Touch target: the full card hit region is at least **`fl-touch-min-height`**
([`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)).

## Visual states

### Default (not focused)

Reference behavior from v1 as-built roster carousel:

- **Opacity:** ~58% of focused card (`fl-card-unfocused-opacity` ≈ 0.58).
- **Scale:** 0.9 relative to focused (`fl-card-unfocused-scale` ≈ 0.9).

### Focused (carousel center)

- **Opacity:** 1.0.
- **Scale:** 1.0.
- Focus **must not** rely on color alone; scale and opacity carry the primary cue.

### Picked (send-user selection)

When used on the send-user screen, a card that is **selected for send** shows a
**2 px** outline in `fl-color-accent` ([`THEME-DARK`](../themes/THEME-DARK.md)).
This is separate from carousel focus (v1 used the same card chrome with a gold
border for `picker-card.picked`).

### Pressed

**OPEN** — may reuse brief opacity dip; must not conflict with focus animation.

## Roster size

Layout and snap math are identical for **4** or **8** users (product minimum eight
supported; typical hangouts ≤ 4). No distinct density mode.

## Dependencies

- [`THEME-DARK`](../themes/THEME-DARK.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Light-gradient text contrast on hardware | Name color inversion |
| `UI-OPEN-002` | Pressed-state treatment | Touch feedback polish |

## Revision history

- **0.1 — 2026-10-10:** v1-derived geometry; avatar 66 px; focus scale/opacity;
  accent border for pick mode.
