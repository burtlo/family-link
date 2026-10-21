# PIN entry screen

| Field | Value |
|---|---|
| ID | `SCR-PIN-ENTRY` |
| Type | Screen composition |
| Revision | 0.7 |
| Status | Draft — PIN contents composed through panels; `checking...` placement OPEN |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN entry and verification stages |

## Purpose and scope

Four-digit PIN entry for the selected user. Cooldown after five failures uses
[`SCR-PIN-COOLDOWN`](SCR-PIN-COOLDOWN.md), not this screen.

## Stage transitions

| Event | Motion |
|---|---|
| Roster → PIN | Forward [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) (h19, 400 ms, ease-in-out quint) |
| PIN → roster (BOOT with no digits) | Reverse cover-down |
| PIN → message inbox (success) | Reverse cover-down (inbox underneath) |
| Fifth wrong PIN → cooldown | Forward cover-down to `SCR-PIN-COOLDOWN` |

## Composition

```text
SCR-PIN-ENTRY
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-PIN-STATUS
   └─ lower → PANEL-BODY-PIN-GRID
       └─ COMP-PIN-KEYPAD
```

The two panels satisfy the one-panel-per-slot rule in
[`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md). Their
slot-local visual details are specified in
[`PANEL-HEADER-PIN-STATUS`](../panels/PANEL-HEADER-PIN-STATUS.md) and
[`PANEL-BODY-PIN-GRID`](../panels/PANEL-BODY-PIN-GRID.md).

## Upper band

[`PANEL-HEADER-PIN-STATUS`](../panels/PANEL-HEADER-PIN-STATUS.md) occupies the
80 px upper slot and presents the selected identity, PIN progress, and
verification feedback.

## Lower band

[`PANEL-BODY-PIN-GRID`](../panels/PANEL-BODY-PIN-GRID.md) occupies the 160 px
lower slot and contains [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md).
The keypad's existing origin and digit-size disagreement are recorded in the
panel's OPEN items; this screen does not choose a correction.

## Wireframe

```text
┌────────────────────────────────────────┐
│ Sign in as [av] Name      * * _ _      │  upper
├────────────────────────────────────────┤
│        [ 1 ] [ 2 ] [ 3 ]               │
│        [ 4 ] [ 5 ] [ 6 ]               │  lower
│        [ 7 ] [ 8 ] [ 9 ]               │
└────────────────────────────────────────┘
```

## Visual states

| State | Presentation |
|---|---|
| Partial PIN | Up to four `*` / `_` slots as above |
| Checking | **`checking...` placement OPEN** (see question below) |
| Wrong PIN | Identity cluster → `wrong pin` for **2000 ms**, then restore + clear |
| Cooldown | Leave screen → `SCR-PIN-COOLDOWN` (cover-down) |
| Success | Reverse cover-down to inbox; no success chrome on this screen |
| Overlay (`connecting` / `no Wi-Fi`) | Keep this screen mounted. [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) covers it and ignores all keypad touch. Entered digits stay. |
| Verification / cooldown on keypad | Ignore touch; visual dim **OPEN** if not full-screen cooldown |

BOOT (`DEC-037`): no digits → roster; 1–3 digits → clear in place; after digit
four → ignore BOOT until verification completes. While
[`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) is visible,
ignore BOOT and pause the 60-second partial-PIN idle timer.

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | While verifying after the 4th digit, where does **`checking...`** appear? (A) replace identity cluster like `wrong pin`; (B) small line under identity; (C) replace the four PIN slots temporarily) |

Resolved: cover-down transitions; v1 PIN vertical placement; v1 keypad grid;
wrong-pin 2 s; cooldown on separate screen.

## Revision history

- **0.5 — 2026-10-10:** h19 cover-down; v1 product PIN layout; 2 s wrong pin;
  cooldown screen; keypad from product shell not h03 demo.
- **0.7 — 2026-10-20:** Overlay may cover this screen and block keypad touch.
- **0.6 — 2026-10-20:** Compose upper and lower PIN content through panels;
  retain checking placement as OPEN.
- **0.4 — 2026-10-10:** Prior h03-based keypad note superseded.
