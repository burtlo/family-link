# LVGL assessment — demo p10 (packed portrait)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADOPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-p10` |
| **Scope** | Personality demo `p10` (RGB565 persona image on LCD) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `p10` |
| **Related specs** | [`COMP-USER-CARD`](../components/COMP-USER-CARD.md), avatar pipeline |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Static portrait display |
| Entry triggers | Boot |
| Exit triggers | Immediate PASS |
| Ephemeral UI state | None |
| Domain state | Compiled `persona_idle` image descriptor |

## 2. Update level map

| Change | Level |
|---|---|
| Entire surface | **2** once (image + caption label) |
| Runtime updates | None |

## 3. Widget choices

| Region | Rating | Rationale |
|---|---|---|
| `lv_image` + `lv_image_dsc_t` in flash | **Use** | Preferred over geometry face when asset exists |
| Geometry face fallback | **Use** | `v1_ui_paint_user_portrait` path when no asset |
| Full-screen persona | **Neutral** | Card uses smaller portrait sizes |

## 4. Measurement

| Column | Target |
|---|---|
| Widgets | 2–3 |
| Ledger A | Minimal |
| Ledger B | Image bytes in **flash** (const descriptor), not malloc FB |
| Decode | None at runtime if pre-packed RGB565 |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Validate asset dimensions vs card portrait slots before roster ship |
| P1 | Flash p10 when testing persona overlay build, not for interaction |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
