# Button minimum sizing foundation

ID: `FOUNDATION-BUTTON-MINIMUMS`
Type: Foundation
Revision: 0.2
Status: Draft — keypad-derived minimum width and height; touch floor in DEVICE-CANVAS
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

This foundation defines **suggested minimum width and height** for primary
rectangular tap targets (buttons, keypad keys, and equivalent `lv_button`-class
controls) on the fixed 320 × 240 canvas.

It does not replace behavioral policy in experience specifications. It does not
define list-row geometry, carousel cards, or circular pickers — only **box-shaped
primary controls** whose hit area should read as a button.

Height **floors** and slider thickness remain in
[`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md) (`fl-touch-min-height`).
This document adds **width** guidance and documents how the **login PIN keypad**
sets the v2 reference minimum for both dimensions.

## Relationship to touch minimum height

| Token | Value | Authority |
|---|---|---|
| `fl-touch-min-height` | **48–56 px** | [`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md) — hard floor; MUST NOT go below **48 px** |
| `fl-button-min-height` | **48 px** | This document — **suggested** minimum height derived from login PIN grid |
| `fl-button-min-width` | **101 px** | This document — **suggested** minimum width derived from login PIN grid |

Implementations MAY use targets taller or wider when layout allows (prefer up to
**56 px** height per device canvas). They MUST NOT make primary controls **shorter**
than **48 px** or **narrower** than **101 px** unless this foundation is revised
and dependents are updated.

## Grid spacing tokens (login PIN reference)

The login PIN keypad ([`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) on
[`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)) uses a **3 × 3** digit grid in
the lower split slot (**160 px** tall; see
[`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)). Spacing matches the
historical v1 product shell PIN math.

| Token | Value | Role |
|---|---|---|
| `fl-grid-key-pad` | **4 px** | Inset from the band edge on all sides of the key grid |
| `fl-grid-key-gap` | **4 px** | Gap between adjacent keys (horizontal and vertical) |

Keys use the **full 320 px** lower-pane width (`x = 0…320`), not `fl-content-well`.

## Calculation (normative)

Variables: band width `W`, band height `H`, column count `cols`, row count `rows`,
pad `p` = `fl-grid-key-pad`, gap `g` = `fl-grid-key-gap`. Integer division
(floor), matching firmware:

```text
key_w = (W − 2×p − (cols − 1)×g) / cols
key_h = (H − 2×p − (rows − 1)×g) / rows
```

**Login PIN** (`W` = 320, `H` = 160, `cols` = `rows` = 3, `p` = `g` = 4):

```text
key_w = (320 − 8 − 8) / 3 → 101 px   →  fl-button-min-width
key_h = (160 − 8 − 8) / 3 → 48 px    →  fl-button-min-height
```

```text
3×3 grid, 4 px pad/gap on 320×160 lower slot:
|←101→|4|←101→|4|←101→|  ×3 rows (keys 48 px tall)
```

Digit keys on login SHOULD be **101 × 48 px** with `p = g = 4`. Other primary
buttons SHOULD be at least **101 × 48 px** unless a spec documents an
owner-approved exception.

## Applying the minimums outside the PIN grid

| Control type | Rule |
|---|---|
| **Keypad keys** | Grid formulas; login reference **101 × 48** |
| **Standalone primary buttons** | Width ≥ **101 px**, height ≥ **48 px**; MAY be wider (e.g. paired **140 px** actions) |
| **Full-width actions** | Width MAY span `fl-content-well` (**296 px**) or slot width; height ≥ **48 px** |
| **Non-rectangular targets** | MUST satisfy `fl-touch-min-height`; width minimums here do not apply |

Typography follows device canvas and component specs (PIN digits: **`fl-type-24`**
per [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md)).

## Out of scope

- Island demo PIN pads (~98 × 42, 3 × 4) — evidence only; v2 login uses product shell math.
- List row height or pitch (inbox, settings, menus).
- Spacing between unrelated controls — use `fl-space-8` / `fl-space-16` from device canvas.

## Traceability

| Source | Contribution |
|---|---|
| Product owner, 2026-10-10 | Login PIN on split lower slot; 3 × 3 digits; 4 px pad and gap |
| [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) | Component geometry implementing this math |
| [`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md) | `fl-touch-min-height` floor **48–56 px** |
| Historical v1 product PIN shell | Original `(320, band_h, pad, gap)` key sizing |

## Dependents

- [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md)
- [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)
- Future `COMP-*` primary buttons and settings actions

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-004` | Hardware check: **101 px** min width for short labels (“Play”) at **`fl-type-16`** / **`fl-type-24`** | Non-keypad primary buttons |

## Readiness

- **Owner review:** Ready for keypad-derived minimums and formulas.
- **Visual approval:** Pending desk check of PIN keys at **101 × 48** on BOX-class hardware.
- **Implementation:** Agents MAY use `fl-button-min-width` / `fl-button-min-height` in specs and UI code.

## Revision history

- **0.2 — 2026-10-20:** Tighter layout; compact grid sketch; merged worked example.
- **0.1 — 2026-10-20:** Initial foundation — login PIN grid **101 × 48**; grid formulas.
