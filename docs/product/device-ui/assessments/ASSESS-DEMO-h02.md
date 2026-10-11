# LVGL assessment — demo h02 (display count / dim)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)  
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h02` |
| **Scope** | Island demo `h02` (locked-idle “N new” + PWM dim) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `h02` |
| **Related specs** | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md), sleep/dim |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Static idle: device name + “2 new” |
| Entry triggers | Boot |
| Exit triggers | PASS; backlight dim after 4 s |
| Ephemeral UI state | None (fixed copy) |
| Domain state | None |

## 2. Update level map

| Change | Level |
|---|---|
| Initial labels | **2** once (`paint_idle`, no clean — screen empty) |
| Backlight dim | **0** (BSP, not LVGL) |

## 3. Widget choices

| Region | Rating |
|---|---|
| Montserrat 48 count | **Use** reference for hangout empty / locked-idle |
| No `lv_obj_clean` | **Use** — minimal tree |
| Hard-coded “2 new” | **Neutral** — h14 adds live badge |

## 4. Measurement

| Column | Target |
|---|---|
| Widgets | 2 labels |
| Ledger A | Minimal |
| Ledger B | **0** |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P1 | Flash for typography + dim smoke; behavior from h14 for live counts |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
