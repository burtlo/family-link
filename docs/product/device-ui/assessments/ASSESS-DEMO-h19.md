# LVGL assessment — demo h19 (message list transitions)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** [`DEMO-PATTERN-LEDGER`](DEMO-PATTERN-LEDGER.md)

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h19` |
| **Scope** | Island demo `h19` (inbox list ↔ detail ↔ record-entry motion) |
| **Revision** | 0.1 |
| **Status** | Draft (static code review; Measured column empty) |
| **Author** | Firmware agent |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `h19` island demo |
| **Related specs** | [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md), [`EXP-LOGIN`](../../experiences/endpoint-login.md) (stage motion only) |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Prototype inbox list, message detail, record-entry stub (not production record) |
| Entry triggers | Tap list row → detail; FAB / physical key → record stub |
| Exit triggers | Back reverses chosen transition; record stub dismiss |
| Ephemeral UI state | Vertical list scroll; active transition anim (~400 ms); record bloom / ring wipe |
| Domain state kept outside widgets | Selected row id, transition enum per row, anim-in-flight flags |

## 2. Update level map

| User-visible change | Level (0 / 1 / 2) | Why not lower? | Preserved IDs / state |
|---|---|---|---|
| List scroll | **0** | Scroll container only | Row widgets, clip host |
| List → detail (cover-down default) | **2** | Stage-like surface swap with motion | Persistent `s_root_scr`, clip/wrap hosts |
| Transport-style label tweaks | **0** | N/A in demo | — |
| Record stub reveal | **2** | New panel geometry (stub) | Record host allocated once |

**Checklist**

- [x] Data arrival alone does not use Level 2 (demo has no live inbox).
- [x] Level 2 uses transform anims, not `lv_obj_delete` of root.
- [x] Same snapshot → idempotent paint for list rebuild paths reviewed.
- [ ] Measured widget peak during anim overlap — **not run on device**.

## 3. Widget and API choices

| UI region | Implementation option | Rating | Rationale |
|---|---|---|---|
| Stage root | Persistent `s_root_scr` + clip children | **Use** | Matches v2 stage manager direction |
| List | Vertical scroll + row buttons | **Use** | Standard inbox list pattern |
| Detail | Wrapped subtree + transform anim | **Use** | Cover-down/up without screen delete |
| Record capture | Styled `lv_obj` panel + palette swatches | **Neutral** | **Not** RGB565 ink — stub only |
| Full `lv_obj_clean(scr)` per navigation | — | **Avoid** | Demo avoids for main flow; good |

## 4. Standard measurement table

| Column | Target (design) | Measured (device) |
|---|---|---|
| Widgets est. (steady) | ~40–80 (list rows × ~3–4 objs + chrome) | |
| Widgets peak (transition) | Old + new subtrees during 400 ms anim | |
| Ledger A — LVGL pool | Within 64 KiB pool at ≤12 rows | |
| Ledger B — pixel RAM | **0** (record stub has no FB) | |
| Ledger B — audio / other | None | |
| Invalidate strategy | Transform + subtree invalidation during anim | |
| Paint lifecycle | Build list once; anim moves wrappers | |
| Anim layer cost | 1–2 concurrent `lv_anim`, 400 ms quint ease | |
| Scroll / snap behavior | Vertical list only | |
| Hotspot notes | Many transition enums — lab surface, not all for v2 | |
| Frame / build time p95 | | |
| Degradation at boundary | | |

### Pixel RAM worksheet

| Buffer | W × H | bpp | Bytes | Caps | Lifetime |
|---|---|---|---|---|---|
| Record stub | — | — | **0** | — | N/A (vector UI only) |

## 5. Grep / code review evidence

| Cookbook query | Finding | Action |
|---|---|---|
| `lv_obj_clean` | Not used on `s_root_scr` for list/detail | **ADOPT** pattern |
| `heap_caps_malloc` / SPIRAM | No sketch FB in h19 | Implement ink using h27 / product record module evidence |
| `lv_anim` | Cover-down/up, record expand — `TRANS_MS` 400 | Align with [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) |
| Blocking I/O in UI path | None in demo | — |

## 6. Hotspots and risks

| ID | Severity | Description | Mitigation |
|---|---|---|---|
| H-01 | High | Record panel looks like real capture but has no canvas FB | Spec SK-002–004 before ship; cite record module assessments |
| H-02 | Medium | Many transition types — easy to over-ship | V2 default cover-down only per motion foundation |

## 7. Comparison to legacy v1 (evidence only)

| Topic | Legacy v1 behavior | v2 assessment choice |
|---|---|---|
| Stage motion | h19 cover-down 400 ms | **ADOPT** as default roster ↔ PIN |
| Record UI | Stub panel 240×240 + rail | Replace with bounded FB per experience spec |
| Product shell | x02 `lv_obj_clean` per stage | **ADAPT** — keep router, reduce blind repaints |

## 8. Recommendations

| Priority | Recommendation | Blocks |
|---|---|---|
| P0 | Promote cover-down from h19 into stage manager API | SCR stage transitions |
| P1 | Do not copy record stub as ink implementation | EXP sketch record |

## 9. Sign-off

| Role | Name | Date | Notes |
|---|---|---|---|
| Implementer | | | |
| Reviewer | | | |
| Product owner | | | |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial static assessment from island demo review |
