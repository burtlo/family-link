# LVGL screen assessment (template)

Copy this file to `docs/product/device-ui/assessments/ASSESS-<SCOPE>.md` and
delete the instructional lines marked *(remove in copy)*.

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern verdicts:** [`DEMO-PATTERN-LEDGER`](DEMO-PATTERN-LEDGER.md) (ADOPT / ADAPT / AVOID; add a ledger row when a demo graduates from the review queue)

---

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-` *(example: `ASSESS-SCR-PIN-ENTRY`)* |
| **Scope** | *(Screen `SCR-*`, demo `h**`, or component `COMP-*`)* |
| **Revision** | 0.1 |
| **Status** | Draft / Measured / Reviewed |
| **Author** | |
| **Date** | |
| **Firmware / demo ref** | *(git rev or build id — do not link archive paths in promoted v2 docs)* |
| **Related specs** | *(links to `SCR-*`, `EXP-*`, `COMP-*`)* |

## 1. User-visible scope

*(remove in copy)* Summarize what the user can do on this surface and what
session stage it belongs to.

| Question | Answer |
|---|---|
| Session stage(s) | |
| Entry triggers | |
| Exit triggers | |
| Ephemeral UI state (scroll, partial PIN, anim) | |
| Domain state kept outside widgets | |

## 2. Update level map

Classify **every** recurring change. If any row is Level 2, cite the session
stage transition.

| User-visible change | Level (0 / 1 / 2) | Why not lower? | Preserved IDs / state |
|---|---|---|---|
| | | | |
| | | | |
| | | | |

**Checklist**

- [ ] Data arrival alone does not use Level 2.
- [ ] Level 1 names stable identity keys (user id, message seq, …).
- [ ] Level 2 lists cancelled anims/timers and invalidated modules.
- [ ] Same snapshot → idempotent paint (no duplicate timers/callbacks).

## 3. Widget and API choices

| UI region | Implementation option | Rating (Use / Avoid / Neutral) | Rationale |
|---|---|---|---|
| *(example: PIN keys)* | 9 × `lv_button` + label | | |
| *(example: PIN keys)* | `lv_buttonmatrix` | | |
| *(example: roster)* | Horizontal scroll + N cards | | |
| *(example: sketch)* | `lv_canvas` / `lv_image` + buffer | | |
| Shell | Full `lv_obj_clean(scr)` | | |
| Shell | Persistent layout + `PANEL-*` swap | | |

**Checklist**

- [ ] Touch targets ≥ **48 px** height ([`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)).
- [ ] Font sizes only **16 / 24 / 32** unless foundation waived.
- [ ] No transform layers on large card stacks without measurement.

## 4. Standard measurement table

| Column | Target (design) | Measured (device) |
|---|---|---|
| Widgets est. (steady) | | |
| Widgets peak (transition) | | |
| Ledger A — LVGL pool used / biggest free | | |
| Ledger B — pixel RAM (list each buffer) | | |
| Ledger B — audio / other | | |
| Invalidate strategy | | |
| Paint lifecycle | | |
| Anim layer cost | | |
| Scroll / snap behavior | | |
| Hotspot notes | | |
| Frame / build time p95 | | |
| Degradation at boundary | | |

### Pixel RAM worksheet

| Buffer | W × H | bpp | Bytes | Caps | Lifetime |
|---|---|---|---|---|---|
| | | | | | |

## 5. Grep / code review evidence

*(remove in copy)* Paste **counts or file:function references** from the active
tree (not required to paste raw grep dumps in the filed assessment).

| Cookbook query (from foundation) | Finding | Action |
|---|---|---|
| `lv_obj_clean` | | |
| `heap_caps_malloc` / SPIRAM | | |
| `LV_EVENT_SCROLL` | | |
| `lv_anim` | | |
| Blocking I/O in UI path | | |

## 6. Hotspots and risks

| ID | Severity | Description | Mitigation |
|---|---|---|---|
| H-01 | Critical / High / Medium | | |
| H-02 | | | |

**Checklist**

- [ ] No UI-thread blocking HTTP or codec wait.
- [ ] No use-after-free: handles cleared before foreign scene destroy.
- [ ] Peak memory includes old+new tree during transitions.
- [ ] Worst case ≠ happy path (p99 scroll, max roster size, sketch on card).

## 7. Comparison to legacy v1 (evidence only)

| Topic | Legacy v1 behavior | v2 assessment choice |
|---|---|---|
| Paint teardown | | |
| PIN / keypad pattern | *(nine buttons in product PIN)* | |
| Sketch buffer geometry | *(full-screen record)* | *(SK-002–004 if applicable)* |
| Carousel rebuild on inbox | | |
| Heap logging only | | |

## 8. Recommendations

| Priority | Recommendation | Blocks |
|---|---|---|
| P0 | | |
| P1 | | |

## 9. Sign-off

| Role | Name | Date | Notes |
|---|---|---|---|
| Implementer | | | |
| Reviewer | | | |
| Product owner | | | *(budget promotion)* |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | | Initial assessment |
