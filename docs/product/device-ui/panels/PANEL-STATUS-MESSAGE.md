# Connection status body panel

| Field | Value |
|---|---|
| ID | `PANEL-STATUS-MESSAGE` |
| Type | Panel module |
| Revision | 0.2 |
| Status | Draft — lower-slot role and 150–200 ms crossfade owner-decided |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md) `DEC-051`–`DEC-052`, `EXP-005`, `EXP-023`, `EXP-037` |

## Purpose

Fills the lower split-pane slot with a centered connection-status message.
Login uses it for `connecting`, `no Wi-Fi`, and `No people in this hangout`.
The upper slot stays on its current header panel. This is not an overlay and
it does not dim the rest of the screen.

The panel owns slot composition. Copy, retry, and recovery stay in
`EXP-LOGIN`. The message leaf is
[`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md).

## Chrome, content, and composition

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` on the full lower `slot_chrome_bounds` or inherit the shell |
| Child | Mount one `COMP-STATUS-MESSAGE` centered in lower `slot_content_bounds` |
| Insets | The panel must not re-apply `fl-margin-screen` |
| Interaction | The panel has no buttons. It replaces carousel or keypad content, so those controls are not mounted |

```text
lower slot_chrome_bounds (320 × 160; y=80…239)
┌────────────────────────────────────────┐
│                                        │
│              connecting                │
│                 · · ·                  │
│                                        │
└────────────────────────────────────────┘
```

## Visual states

| State | Presentation | Authority |
|---|---|---|
| Waiting | Centered `connecting` plus three progressive dots | `DEC-052`, `EXP-005` |
| Link absent | Centered `no Wi-Fi`; no dots | `DEC-043`, `EXP-023` |
| Empty hangout | Centered `No people in this hangout`; no dots | `DEC-062`, `EXP-048` |

Switching between those two states, or swapping this panel with roster or PIN
body content, uses a **150–200 ms** crossfade (`DEC-060`). Fade the current
lower content out, then fade the new state in. The transition is not cover-down
and not a sliding card. Rapid connectivity fluctuations must not flicker.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Exact anti-flicker hold when connectivity flaps faster than the crossfade | Motion implementation |

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md)
- [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md)
- [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md)
- [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)
- [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md) does **not** mount this panel until the 60-second timer ends (`DEC-054`)

## Readiness

- **Owner review:** Lower-slot replacement, copy, and no-icon rule are ready for correction.
- **Visual approval:** Pending the subtle-transition timing.
- **Implementation:** Geometry of the centered text is specified by the child; motion is not.

## Revision history

- **0.2 — 2026-10-21:** 150–200 ms crossfade; no slide; anti-flicker hold delegated.
- **0.1 — 2026-10-21:** Lower-slot status panel after the overlay card was withdrawn.
