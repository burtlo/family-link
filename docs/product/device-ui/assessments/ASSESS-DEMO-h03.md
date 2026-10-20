# LVGL assessment — demo h03 (touch PIN pad)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** [`DEMO-PATTERN-LEDGER`](DEMO-PATTERN-LEDGER.md) — **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h03` |
| **Scope** | Island demo `h03` (GT911 PIN entry, no network) |
| **Revision** | 0.1 |
| **Status** | Draft (static code review) |
| **Date** | 2026-10-11 |
| **Firmware / demo ref** | Legacy v1 POC archive — `h03` island demo |
| **Related specs** | [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md), [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md) |

## 1. User-visible scope

| Question | Answer |
|---|---|
| Session stage(s) | Standalone PIN pad lab (not split-pane product shell) |
| Entry triggers | Boot into pad |
| Exit triggers | Correct PIN → `unlocked` status |
| Ephemeral UI state | Dot string, status color (locked / wrong / unlocked) |
| Domain state | `s_entry` buffer, `DEMO_PIN` compare |

## 2. Update level map

| User-visible change | Level | Notes |
|---|---|---|
| Key tap | **0** | Label + status only |
| Wrong PIN | **0** | Clear entry, status text |
| Initial paint | **2** | One-shot `paint_pad` (persistent screen thereafter) |

## 3. Widget and API choices

| UI region | Implementation | Rating | Rationale |
|---|---|---|---|
| Keys | 9× `lv_button` + label + Clear/0 row | **Neutral** | Island uses 3×4 incl. Clear/0; v2 policy is 3×3 only per `COMP-PIN-KEYPAD` |
| Key size | ~98×42, 6 px gap | **ADAPT** | Touch target reference; v2 product uses ~101×48 in lower slot |
| Button matrix | Not used | **Use** | Matches slim LVGL policy |

## 4. Standard measurement table

| Column | Target (design) | Measured |
|---|---|---|
| Widgets est. (steady) | ~14 buttons + 2 labels | |
| Ledger A | Low (~5–8 KiB class) | |
| Ledger B | **0** | |
| Invalidate strategy | Per-button press | |
| Paint lifecycle | Single paint at boot | |

## 5. Grep evidence

| Query | Finding |
|---|---|
| `lv_obj_clean` | None after initial paint |
| `heap_caps_malloc` | None |
| Blocking I/O | None |

## 6. Hotspots

| ID | Severity | Description | Mitigation |
|---|---|---|---|
| H-01 | Medium | h03 includes Clear/0; product does not | Use `COMP-PIN-KEYPAD` geometry only |
| H-02 | Low | Full-width island layout vs split pane | Map into `LAYOUT-SPLIT-PANE` lower slot |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | Adopt touch proof; adopt 3×3 digit grid from product shell (`v1_auth`), not h03 fourth row |
| P1 | Flash h03 for GT911 sanity; spec compliance against `SCR-PIN-ENTRY` |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial static assessment |
