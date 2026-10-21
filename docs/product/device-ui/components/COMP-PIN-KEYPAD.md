# PIN keypad component

ID: `COMP-PIN-KEYPAD`
Type: Component
Revision: 0.7
Status: Draft — 32 px digits; grid origin is the lower-slot top; no canvas overflow
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

Numeric keypad for four-digit PIN entry on [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md).

## Layout (owner-decided)

| Rule | Value |
|---|---|
| Grid | **3 × 3** digits **1–9** (LTR: 1–3 top row, 7–9 bottom) |
| Digit 0 | **Not present** |
| Clear row | **Not present** — BOOT / experience policy |

## Geometry (v1 product shell, split-adapted)

Lower `slot_chrome_bounds` (**160 px** tall), full **320 px** width, no v1 ribbons.
Tokens and formulas: [`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md).

| Constant | Value |
|---|---|
| `fl-grid-key-pad` / `start_x` | **4 px** |
| `fl-grid-key-gap` / `gap_x`, `gap_y` | **4 px** |
| Key width | `(320 − 2×pad − 2×gap_x) / 3` → **101 px** (`fl-button-min-width`) |
| Key height | `(band_h − 2×pad − 2×gap_y) / 3`, `band_h = 160` → **48 px** (`fl-button-min-height`) |
| Vertical origin | Lower-slot top (`y = 80`). The extra 10 px v1 offset is withdrawn because it placed the grid at y=90…241 |

The 4 px pad plus three 48 px rows and two 4 px gaps fills the 160 px slot
exactly (`4 + 48 + 4 + 48 + 4 + 48 + 4 = 160`). Keys occupy y=84…236 and
x=4…316. Nothing may extend past y=239 or x=319.

Corner radius: **8 px** (`fl-card-roster-radius`). Pressed fill: darker card tone from theme (match v1 device on RGB565).

## Typography and touch

- Digit labels: **`fl-type-32`**, weight **600**.
- Each key satisfies **`fl-touch-min-height`** (48 px). Do not shrink keys
  below **101 × 48 px**.

## Visual states

| State | Presentation |
|---|---|
| Ready | Full-contrast keys; taps accepted |
| Checking | Visually dim and disable keys; ignore taps (`DEC-059`). Exact dim amount is OPEN on `SCR-PIN-ENTRY` |
| Restored after wrong PIN | Return to Ready with the existing `wrong pin` header feedback |

## Revision history

- **0.7 — 2026-10-21:** `fl-type-32` digits; drop the 10 px origin overflow;
  dim-while-checking.
- **0.6 — 2026-10-20:** Cite button-minimums foundation; drop redundant digit ASCII block.
- **0.5 — 2026-10-10:** v1 product keypad math (not h03 demo); vertical position mapped into split lower slot.
- **0.4 — 2026-10-10:** Prior h03-based draft superseded.
