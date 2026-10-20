# LVGL assessment — demo h18 (playback screen)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADOPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h18` |
| **Scope** | Island demo `h18` (catalog JSON playback UI) |
| **Revision** | 0.1 |
| **Status** | Draft (static code review) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `h18` |
| **Related specs** | [`ui-update-taxonomy`](../../../v1-assessments/ui-update-taxonomy.md), inbox playback TBD |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Single-message player with meta, bar, clock, play, ROOMVOL |
| Entry triggers | Wi-Fi + JSON catalog load → `paint_player` |
| Exit triggers | Boot → next message → **Level 2** `paint_player` |
| Ephemeral UI state | Play/pause, scrub bar, volume notch |
| Domain state | `s_msg`, streaming task, codec volume |

## 2. Update level map

| User-visible change | Level | Notes |
|---|---|---|
| Position / clock / play label / read badge | **0** | `ui_refresh()` only |
| Volume slider label (dev) | **0** | Optional `ROOMVOL_SHOW_LEVEL` |
| Load different catalog message | **2** | `lv_obj_clean(scr)` in `paint_player` |

**Checklist:** Data during play does not rebuild tree — **pass**. Message identity change rebuilds — **acceptable**.

## 3. Widget and API choices

| Region | Implementation | Rating |
|---|---|---|
| Transport | `lv_bar` + labels, `ui_refresh` | **Use** |
| Full clean on new message | `paint_player` | **ADAPT** — v2 should Level 1 diff if only meta changes |
| Audio | Background task + `esp_codec_dev_write` | **Use** (not LVGL) |

## 4. Measurement table

| Column | Target | Measured |
|---|---|---|
| Widgets steady | ~15–25 | |
| Ledger B pixel | **0** (no sketch FB) | |
| Ledger B audio | Streaming buffer (task) | |
| Hotspot | Rebuild on every `s_next_msg` | v2: rebuild only when `seq` changes |

## 5. Grep evidence

| Query | Finding |
|---|---|
| `ui_refresh` | Level 0 path during playback |
| `lv_obj_clean` | Only in `paint_player` |
| UI-thread HTTP | Load path — review for v2 async envelope |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | **ADOPT** `v1_carousel_refresh_transport` / h18 `ui_refresh` pattern for carousel transport |
| P1 | Do not copy full player rebuild for inbox badge-only updates |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial static assessment |
