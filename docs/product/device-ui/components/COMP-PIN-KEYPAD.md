# PIN keypad component

ID: `COMP-PIN-KEYPAD`
Type: Component
Revision: 0.6
Status: Draft — v1 product shell geometry adapted to split lower slot
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
| Vertical origin | **10 px** below split (`y = 80`) — v1 `scroll_card_y + pad` in lower pane |

Corner radius: **8 px** (`fl-card-roster-radius`). Pressed fill: darker card tone from theme (match v1 device on RGB565).

## Typography and touch

- Digit labels: **`fl-type-24`**, weight **600**.
- Each key satisfies **`fl-touch-min-height`**.

## Revision history

- **0.6 — 2026-10-20:** Cite button-minimums foundation; drop redundant digit ASCII block.
- **0.5 — 2026-10-10:** v1 product keypad math (not h03 demo); vertical position mapped into split lower slot.
- **0.4 — 2026-10-10:** Prior h03-based draft superseded.
