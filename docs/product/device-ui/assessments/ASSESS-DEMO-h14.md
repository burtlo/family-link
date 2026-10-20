# LVGL assessment — demo h14 (heartbeat inbox badge)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h14` |
| **Scope** | Island demo `h14` (WS badge, locked-idle, no message body) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `h14` |
| **Related specs** | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md), sleep / locked-idle policy |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Locked-idle: large count + peer line |
| Entry triggers | Wi-Fi + WS hello/heartbeat |
| Exit triggers | PASS when hello + heartbeat OK |
| Ephemeral UI state | Badge count, peer online color |
| Domain state | `s_badge`, `s_peer_online` (no message body) |

## 2. Update level map

| Change | Level | Notes |
|---|---|---|
| Badge count / peer text / color | **0** | `refresh_ui()` — label text only |
| First paint | **2** | `paint_idle` → `lv_obj_clean` once |
| Re-paint idle | **2** if called again | Prefer keep widgets like h14 `refresh_ui` |

**Checklist:** WS events use **Level 0** after initial paint — **ADOPT** for poller-driven chrome.

## 3. Widget choices

| Region | Rating |
|---|---|
| Large count font (48 class) | **Use** for locked-idle |
| No message body on glass | **Use** (privacy) |
| `lv_obj_clean` on every idle entry | **Avoid** if poller fires often — h14 only cleans once at start |

## 4. Measurement

| Column | Target |
|---|---|
| Widgets | 2 labels |
| Ledger A | ~1 KiB class |
| Ledger B | **0** |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Model for **background inbox badge** updates without carousel rebuild |
| P1 | Pair with h02 typography for static “N new” copy review |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
