# Dark theme

ID: `THEME-DARK`  
Type: Theme  
Revision: 0.1  
Status: Draft — core surface and accent tokens owner-aligned  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas (RGB565)

## Purpose

`THEME-DARK` is the default v2 device color and accent vocabulary. Layouts,
panels, and components reference **tokens** here instead of raw hex literals.

Additional themes require a new `THEME-*` document and an explicit product
decision to support them.

## Sources and authority

| Source | Contribution |
|---|---|
| Product owner, 2026-10-10 | Default surface `#101418`; split panes may use two surface tokens; accent for selection highlights |
| Legacy v1 product web twin (as-built reference) | Surface, text, card peek, and gold accent values used to seed tokens |

## Core palette

| Token | Hex | Role |
|---|---|---|
| `fl-color-surface` | `#101418` | Default full-canvas and sign-in pane background |
| `fl-color-surface-upper` | `#101418` | Upper `slot_chrome_bounds` when a screen uses distinct pane colors |
| `fl-color-surface-lower` | `#101418` | Lower `slot_chrome_bounds` when a screen uses distinct pane colors |
| `fl-color-text-primary` | `#E8F0E8` | Primary labels and headings |
| `fl-color-text-secondary` | `#A8B0B8` | De-emphasized metadata |
| `fl-color-accent` | `#E8C040` | Selection outline (for example send-user pick); not the only focus cue |
| `fl-color-card-peek` | `#242C34` | Optional peek/side-card fill when a panel spec calls for it |

Sign-in roster (`SCR-SIGN-IN-ROSTER`) paints **`fl-color-surface`** on both
upper and lower panes (same color in both slots).

Message home and other screens **MAY** assign different values to
`fl-color-surface-upper` and `fl-color-surface-lower` when documented in the
screen or panel spec.

## Per-user message color

Each hangout user carries a **message color** (historically a vertical gradient
pair). Roster and message cards use that color as the card face background
behind the avatar and name.

| Token pattern | Meaning |
|---|---|
| `fl-color-user-message-top` | User-specific gradient top stop (from server profile) |
| `fl-color-user-message-bottom` | User-specific gradient bottom stop |

Components render the face as a top-to-bottom gradient between these stops. Light
gradients **MAY** require inverted text treatment; that rule belongs in
`COMP-USER-CARD` when contrast is measured on hardware.

## Shell defaults

[`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) paints
`fl-color-surface` on the full canvas before panel overrides. Panels **MAY**
override their slot with `fl-color-surface-upper` or `fl-color-surface-lower`.

## Dependents

- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) — typography pairs with this theme
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) — pane backgrounds
- [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) — card face and states
- [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Confirm RGB565 quantization for gradients and accent on hardware | Pixel-perfect match to hex |
| `UI-OPEN-002` | Additional themes (light, high-contrast) | Non-dark products |

## Revision history

- **0.1 — 2026-10-10:** Initial dark theme; surface `#101418`; dual pane tokens;
  accent and per-user message gradient pattern.
