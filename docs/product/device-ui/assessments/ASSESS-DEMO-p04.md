# LVGL assessment — demo p04 (mood face tweens)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADOPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-p04` |
| **Scope** | Personality demo `p04` (five mood states, property tweens) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `p04` |
| **Related specs** | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Offline mood face lab |
| Entry triggers | Circle cycles mood |
| Exit triggers | PASS after five states |
| Ephemeral UI state | ~300 ms parallel tweens on head x/y and mouth height |
| Domain state | Mood index, caption string |

## 2. Update level map

| Change | Level | Notes |
|---|---|---|
| Mood tween | **0** | Same `s_face` subtree; `lv_anim` on properties |
| Caption text | **0** | Label set after tween |
| Initial build | **2** | Once at boot |

## 3. Widget choices

| Region | Rating | Rationale |
|---|---|---|
| Persistent face subtree | **Use** | No scene replacement for emotion |
| Parallel `lv_anim` (x, y, h) | **Use** | Cheap vs full-screen transition |
| Hard cut between moods | **Avoid** | p04 contrasts with p01 — p04 is the v2 motion model for chrome |

## 4. Measurement

| Column | Target |
|---|---|
| Widgets steady | ~6–10 |
| Ledger B | **0** |
| Anim | 3× ~300 ms per mood step |
| Layer cost | Small objects only |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Use for ribbon/mascot **Level 0** motion; not for roster↔PIN (use cover-down) |
| P1 | Keep tween duration ≤400 ms family per motion foundation |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
