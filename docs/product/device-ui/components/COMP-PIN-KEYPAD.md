# PIN keypad component

ID: `COMP-PIN-KEYPAD`  
Type: Component  
Revision: 0.5  
Status: Draft — v1 product shell geometry adapted to split lower slot  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

Numeric keypad for four-digit PIN entry on [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md).

## Layout (owner-decided)

| Rule | Value |
|---|---|
| Grid | **3 × 3** digits **1–9** only |
| Digit 0 | **Not present** |
| Clear row | **Not present** — BOOT / experience policy |

Digit order:

```text
1  2  3
4  5  6
7  8  9
```

## Geometry (v1 product shell, split-adapted)

Derived from the legacy v1 product PIN screen (three rows in the lower **⅔**
content band). v2 mounts the same math in the lower `slot_chrome_bounds` (160 px
tall), **without** v1 top/bottom ribbons.

| Constant | Value |
|---|---|
| Horizontal pad | **4 px** (`start_x`) |
| Key gaps | **4 px** (`gap_x`, `gap_y`) |
| Key width | `(320 − 2×pad − 2×gap_x) / 3` → **101 px** |
| Key height | `(band_h − 2×pad − 2×gap_y) / 3` with `band_h = 160` → **48 px** |
| Vertical origin | **10 px** below split (`y = 80` on canvas) — same offset as v1 `scroll_card_y + pad` mapped into the lower pane |

Corner radius: **8 px** (`fl-card-roster-radius` / v1 card radius).

Pressed fill uses a darker card tone from theme (v1 used a dedicated press hex on
RGB565; match v1 device appearance).

## Typography and touch

- Digit labels: **`fl-type-24`** on keys (v1 PIN title size on keypad labels);
  owner global weight **600**.
- Each key satisfies **`fl-touch-min-height`**.

## Revision history

- **0.5 — 2026-10-10:** v1 product keypad math (not h03 demo); vertical position
  mapped into split lower slot.
- **0.4 — 2026-10-10:** Prior h03-based draft superseded.
