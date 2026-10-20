# LVGL assessment — demo p13 (UI showcase / LVGL lab)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADOPT** (lab reference)

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-p13` |
| **Scope** | Island demo `p13` (multi-page LVGL lab) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `p13` |
| **Related specs** | [`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md), [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Menu + paginated showcase (scroll modes, snap, gradients, modals) |
| Entry triggers | Boot → menu; boot/circle next page |
| Exit triggers | PASS after one page advance |
| Ephemeral UI state | Horizontal snap pages, scrollbar modes |
| Domain state | Page index |

## 2. Update level map

| Change | Level | Notes |
|---|---|---|
| Page flip | **2** | Rebuild page subtree (lab acceptable) |
| Scroll within page | **0** | Container scroll |
| Snap carousel scroll | **0** | Position + opacity patterns |

## 3. Widget / buffer choices

| Region | Rating | Notes |
|---|---|---|
| H snap gradient cards | **Use** | Pre-baked RGB565 + tuned scroll — carousel perf reference |
| `lv_canvas` row buffers | **Neutral** | TRI_ROW 296×88 ×4 rows ≈ **52 KiB** each max if all allocated — lab only |
| Full-screen clean per menu pick | **Avoid** in product | OK in lab |

### Pixel RAM worksheet (representative)

| Buffer | W × H | Bytes (RGB565) | Notes |
|---|---|---|---|
| Triangle row canvas | 296 × 88 | ~52 KiB | One row; up to 4 rows in demo |
| Snap card canvas | per page constants | ≪ full screen | See demo `SNAP_CARD_*` pages |

## 6. Hotspots

| ID | Severity | Description |
|---|---|---|
| H-01 | Medium | Many pages — easy to copy wrong pattern into product |
| H-02 | Low | Canvas pages increase Ledger B if reused blindly |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Extract **snap math** and chrome/content split for roster carousel |
| P1 | Do not ship p13 as a product screen — reference only |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
