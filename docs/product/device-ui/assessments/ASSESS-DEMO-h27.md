# LVGL assessment — demo h27 (store-and-forward sketch note)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h27` |
| **Scope** | Island demo `h27` (timed draw, send, replay) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Related specs** | [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) |

## 1. User-visible scope

Home with Draw / New message; timed record on glass; boot sends; playback at recorded speed; boot during play stops and returns home.

## 2. Update level map

| Change | Level | Notes |
|---|---|---|
| Stroke while recording/playing | **0** | `invalidate_canvas` on `lv_image` only |
| Home ↔ record ↔ play | **2** | `lv_obj_clean(scr)` per stage |

## 3. Pixel RAM

| Buffer | Size | Bytes |
|---|---|---|
| Canvas FB | 320 × 240 RGB565 | **153 600** |
| Point buffer | up to 2048 × 8 B | **16 384** class |
| Blob cap | HDR + points | **~16 KiB** class |

v2 target per SK-004: **240×240** display FB (**115 200** B) + **120×120** logical storage (**28 800** B).

## 6. Hotspots

| ID | Severity | Description |
|---|---|---|
| H-01 | High | Full-screen invalidate every stroke (same class as v1_record) |
| H-02 | Medium | Level 2 clean between stages — OK for session changes, not data ticks |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | **ADAPT** timed point stream + replay clock for product sketch wire format |
| P0 | Shrink FB and map coordinates per SK-002–004 before ship |
| P1 | Do not copy h27 home button chrome — use h19 record rail + card canvas |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
