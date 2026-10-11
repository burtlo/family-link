# LVGL assessment — demo h13 (inbox RGB565 preview)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)  
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h13` |
| **Scope** | Island demo `h13` (HTTP image preview full screen) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Related specs** | [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |

## 1. User-visible scope

Poll inbox; download server-downscaled **320×240** RGB565; blit via `lv_image`.

## 2. Update level map

| Change | Level |
|---|---|
| New preview bytes | **1/2** — rebuild image descriptor / clean screen |
| Steady display | **0** |

## 3. Pixel RAM

| Buffer | Bytes |
|---|---|
| `PREVIEW_BYTES` | **153 600** malloc’d RGB |
| v2 card portrait | Should be **much smaller** than full LCD |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Server-side downscale path is **ADOPT**; full-screen blit is **not** for carousel cards |
| P1 | Pair with p10 for asset pipeline review |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
