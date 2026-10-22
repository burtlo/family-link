# Dark theme

ID: `THEME-DARK`
Type: Theme
Revision: 0.4
Status: Draft — first intentional color scheme (no second theme in v2)
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas (RGB565)

## Purpose

`THEME-DARK` is the **only** v2 color scheme under active design. Tokens name
**where** each color is used. A second theme is **backlog**, not in scope.

Layouts and components reference tokens here instead of raw hex literals unless
matching legacy v1 device quantization.

## Token placement map

| Token | Hex (reference) | Used on |
|---|---|---|
| `fl-color-surface` | `#101418` | Full canvas default; sign-in upper/lower chrome; empty hangout; cooldown screen |
| `fl-color-surface-upper` | `#101418` | Upper `slot_chrome_bounds` when a screen uses two pane colors |
| `fl-color-surface-lower` | `#242C34` | Lower `slot_chrome_bounds` for the two-pane treatment |
| `fl-color-text-primary` | `#E8F0E8` | Headings, keypad digits, primary labels on surface |
| `fl-color-text-secondary` | `#A8B0B8` | De-emphasized chrome, empty PIN slots (`_`) |
| `fl-color-accent` | `#E8C040` | Filled PIN `*` marks; send-user pick outline; selected color-swatch outline |
| `fl-color-sketch-canvas` | `#000000` | Record drawing surface fill and background-color ink |
| `fl-color-swatch-ring` | `#B8BEC6` | Ring on every record-palette swatch |
| `fl-color-card-peek` | `#242C34` | Optional non-focused peek fills when specified |
| `fl-color-text-on-card` | *(per catalog entry)* | User name on [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |

Card faces use solid fills from
**[`FOUNDATION-CARD-BACKGROUNDS`](foundations/FOUNDATION-CARD-BACKGROUNDS.md)**
with their paired `fl-color-text-on-card`, not free-form per-user hex.

## RGB565

Match **legacy v1 device** appearance for gradient and accent quantization.
Exact hex match on desk monitors is not required.

## Shell defaults

[`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) paints `fl-color-surface`
on the full canvas before panel overrides.
[`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
does the same; the focused panel then paints `fl-color-sketch-canvas`.

## Dependents

- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`COMP-USER-CARD`](../components/COMP-USER-CARD.md)
- [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md)
- [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)
- [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md)
- [`SCR-RECORD-SKETCH`](../screens/SCR-RECORD-SKETCH.md)

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-10)* RGB565 — follow v1 device |
| `UI-OPEN-002` | Second theme | **Backlog** — not designing now |

## Revision history

- **0.4 — 2026-10-21:** Sketch canvas `#000000` and swatch ring `#B8BEC6`.
- **0.3 — 2026-10-21:** Two-pane lower surface `#242C34` (same pair as the
  profile-settings lab fixture).
- **0.2 — 2026-10-10:** Placement map; catalog card backgrounds; single-scheme intent.
- **0.1 — 2026-10-10:** Initial dark tokens.
