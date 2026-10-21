# PIN keypad body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-PIN-GRID` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — panel boundary specified; inherited keypad fit and typography conflict OPEN |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN entry (`EXP-014`) |

## Purpose

Fills the lower split-pane slot with the PIN keypad. The panel provides the
slot-level background and contains [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md).
It owns composition, not digit acceptance, verification, retry, or PIN-entry
policy.

## Chrome, content, and composition

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` on the full lower `slot_chrome_bounds` or inherit the shell background |
| Child | Mount one `COMP-PIN-KEYPAD` inside the lower slot and clip it to `slot_chrome_bounds` |
| Insets | The panel must not re-apply `fl-margin-screen`; the shell owns `slot_content_bounds` |
| Full-bleed | The keypad grid may use the lower `slot_chrome_bounds` horizontally to preserve the existing 320 px grid math; its keys must remain inside the slot |
| Interaction | Key hit areas belong to the keypad component; accepted input and disabled-state policy come from `EXP-LOGIN` |

```text
lower slot_chrome_bounds (320 × 160; y=80…239)
┌────────────────────────────────────────┐
│          ┌── COMP-PIN-KEYPAD ──┐       │
│          │  1   2   3          │       │
│          │  4   5   6          │       │
│          │  7   8   9          │       │
└────────────────────────────────────────┘
```

The keypad's key sizes, grid gaps, and current vertical-origin proposal remain
defined by `COMP-PIN-KEYPAD`. This panel does not silently change those values.

## Visual and interaction states

| State | Presentation | Authority |
|---|---|---|
| Ready for entry | Show digits 1–9 in the 3×3 grid | `EXP-014`; `COMP-PIN-KEYPAD` |
| Verification in progress | Ignore keypad input after digit four; whether keys dim is OPEN in `SCR-PIN-ENTRY` | `EXP-014`; `SCR-PIN-ENTRY` |
| Cooldown | The screen leaves PIN entry for `SCR-PIN-COOLDOWN`; no keypad is mounted there | `EXP-014` |

The keypad component receives the applicable visual state. This panel does not
define when input is enabled or when a state transition occurs.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Reconcile the inherited keypad origin and dimensions: the current 10 px offset places a 152 px-tall grid at y=90…241, two pixels beyond the 240 px canvas and below the lower slot; the shell content inset is y=92…227. Confirm the intended placement and touch bounds. | Visual approval and implementation |
| `UI-OPEN-002` | Reconcile keypad digit typography: `COMP-PIN-KEYPAD` specifies `fl-type-24`, while `FOUNDATION-DEVICE-CANVAS` assigns `fl-type-32` to large numeric keypad digits. | Visual approval and implementation |

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md)
- [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)

## Readiness

- **Owner review:** Panel boundary and child ownership are ready for review.
- **Visual approval:** Pending the two OPEN items above and a disabled-state review.
- **Implementation:** Not ready until the keypad's native-pixel fit and typography agree with the canvas foundation.

## Revision history

- **0.1 — 2026-10-20:** Extract keypad composition into the lower panel required by the panel contract; record inherited geometry and typography conflicts.
