# LVGL assessment — product shell x02 (stage router)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** `x02` + `v1_connect`, `v1_auth`, `v1_carousel`, `v1_record` — **ADAPT** / mixed
**Carousel deep-dive:** [`ASSESS-MODULE-v1-carousel`](ASSESS-MODULE-v1-carousel.md)

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-SHELL-x02` |
| **Scope** | Product build `x02` orchestration and module paints |
| **Revision** | 0.1 |
| **Status** | Draft (static module review) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `x02` shell + `v1_*` modules |
| **Related specs** | [`EXP-LOGIN`](../../experiences/endpoint-login.md), [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md), all `SCR-*` login/carousel |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Connecting → roster → PIN → carousel/settings → pick → record → send |
| Entry triggers | State machine `v1_state` |
| Exit triggers | Shoulder/boot per stage |
| Ephemeral UI state | Carousel scroll/snap; PIN entry; record timer |
| Domain state | Session, inbox, workers off UI thread (intended) |

## 2. Update level map (by module)

| Module | Good patterns | Problem patterns |
|---|---|---|
| `v1_connect` | Connecting dots **Level 0**; roster snap | Roster paint = **Level 2** clean (stage OK) |
| `v1_auth` | Split header + keypad band | **Level 2** each PIN paint |
| `v1_carousel` | `refresh_transport` **Level 0**; snap anim | **Level 2** `paint_carousel`; scroll portrait rebuild — **AVOID** |
| `v1_record` | Pick snap similar to roster | Record **320×240×2** FB; send phase full paints |
| `x02` `ui_task` | 100 ms loop; transport refresh while playing | Full `paint()` on every `repaint` flag |

## 3. Measurement (shell-level targets for v2)

| Column | v1 as-built (evidence) | v2 target |
|---|---|---|
| Ledger A peak | Carousel full paint ~110–130 widgets | Level 1 inbox; ≤ neighbor cards live |
| Ledger B record | **153 600** B full-screen RGB565 | **115 200** display / **28 800** logical per SK-004 |
| Ledger B sketch cards | ~**35 376** B × allocated slots | **28 800** B @ 120×120; cap concurrent |
| Playback buffer | ~**320 KiB** class PSRAM | Bounded + streaming per resource envelope |

## 4. Grep evidence (module themes)

| Query | Finding |
|---|---|
| `paint(` + `lv_obj_clean` | Every non-carousel stage nukes screen |
| `v1_carousel_is_playing` | Drives transport refresh without full paint |
| `carousel_refresh_sender_header` + scroll | Portrait **clean** on scroll — anti-pattern |
| `RECORD_FB_BYTES` | 320×240×2 |

## 6. Hotspots

| ID | Severity | Description |
|---|---|---|
| H-01 | Critical | Carousel inbox reload → full paint |
| H-02 | High | Record full-frame invalidate on stroke |
| H-03 | High | Handle invalidation required on stage exit (documented in shell) |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Keep **x02** stage table; replace blind `repaint` → full paint with level-aware updates |
| P0 | **ADOPT** modules: connect dots, auth layout, h18-style transport refresh |
| P0 | **AVOID** carousel scroll portrait rebuild; eager max cards |
| P1 | Flash **x02** after each v2 panel lands — not before |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial shell assessment from ledger DEEP rows |
