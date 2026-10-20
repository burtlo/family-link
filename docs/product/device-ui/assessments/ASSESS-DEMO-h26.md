# LVGL assessment — demo h26 (real-time shared drawing)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h26` |
| **Scope** | Island demo `h26` (WS collaborative ink, two kits) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Related specs** | [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) (async mailbox is **not** live collab) |

## 1. User-visible scope

Full-screen black canvas; local + remote strokes (friend color); boot clears both sides; banner/status labels.

## 2. Update level map

| Change | Level |
|---|---|
| Each stroke (local or WS) | **0** — raster + `invalidate_canvas` |
| Boot clear / reconnect UI | **2** — may `lv_obj_clean` screen |

## 3. Architecture

| Topic | Finding |
|---|---|
| Ledger B | **153 600** B persistent FB + queues (not LVGL) |
| LVGL tree | Single `lv_image` + few labels — **low Ledger A** |
| Product fit | **Out of scope** for v2 async sketch — reference for stroke raster only |

## 6. Hotspots

| ID | Severity | Description |
|---|---|---|
| H-01 | High | Same full-frame invalidate pattern as h27 |
| H-02 | Medium | WS + UI queues — do not run heavy work on `ui_task` |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P1 | Borrow **ink raster** approach, not networking model |
| P2 | Flash only if exploring live collab experiments |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
