# LVGL assessment — demo h28 (short CPU-painted clip)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)  
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h28` |
| **Scope** | Island demo `h28` (~5 s film @ 12 FPS, loop) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |

## 1. User-visible scope

Offline animated clip (title → scene → end card); loops; PASS after first full playthrough.

## 2. Update level map

| Change | Level |
|---|---|
| Each frame (~83 ms) | **0** — CPU paint FB + invalidate `lv_image` |
| Setup | **2** once |

## 3. Measurement targets

| Column | Target |
|---|---|
| Ledger B FB | **153 600** B |
| Sustained invalidate | **12 FPS** full frame — SPI stress test |
| Ledger A | Minimal (one image widget) |
| Product | **Not video** — glass budget probe only |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P2 | Flash to validate FPS + `s_max_us` logging on BOX-3 |
| P0 | Do not treat as inbox or sketch feature |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
