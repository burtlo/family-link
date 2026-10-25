# PIN entry screen

| Field | Value |
|---|---|
| ID | `SCR-PIN-ENTRY` |
| Type | Screen composition |
| Revision | 0.9 |
| Status | Draft — PIN contents composed through panels; checking dims keypad; 32 px digits |
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
   └─ lower → PANEL-BODY-PIN-GRID        (entry / checking)
            or PANEL-STATUS-MESSAGE     (connecting / no Wi-Fi)
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
Digit labels are `fl-type-32`. The 3×3 grid must fit inside the 160 px lower
slot (`DEC-061`). The extra 10 px v1 vertical origin is withdrawn.

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
| Checking | Replace the four PIN marks with `checking...`; keep name and avatar; dim and disable the keypad |
| Wrong PIN | Restore PIN marks; restore keypad appearance and input; identity cluster → `wrong pin` for **2000 ms**, then clear entry |
| Connecting / no Wi-Fi | Keep the PIN header. Crossfade the lower slot to [`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) in 150–200 ms. Entered digits stay. |
| Cooldown | Leave screen → `SCR-PIN-COOLDOWN` (cover-down) |
| Success | Reverse cover-down to inbox; no success chrome on this screen |

BOOT (`DEC-037`): no digits → roster; 1–3 digits → clear in place; after digit
four → ignore BOOT until verification completes. While
[`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) occupies the lower
slot, ignore BOOT and pause the 60-second partial-PIN idle timer.

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_LOGIN`; `render_pin_screen()` inside `render_login_copy()`. |
| Visible surfaces | Upper: heading (avatar, name, marks, `checking...`, `wrong pin`). Lower: PIN pad, or the status label while connecting / no Wi-Fi. |
| Input | Key `LV_EVENT_PRESSED` → `enter_pin_digit` when `keypad_open()`. The fourth digit submits to the login HTTP worker from the callback. BOOT goes through the button poll to the session. |
| Enters when | `pin_entry_open()` after a roster selection. Roster cards are deleted on the first PIN render. |
| Leaves when | Success → signed in → `PRODUCT_SCREEN_INBOX` (login hidden by the compositor). BOOT with no digits → roster. Fifth failure → cooldown. Mute → blank, digits discarded. |
| Background work | Login HTTP verify on the worker. The tick drains results; the render never does. |
| Update levels | Level 1: heading and status text change-detected; key opacity changes only when `keypad_open` flips. Level 2: pad ↔ status label in one render. The pad is created once and hidden, never rebuilt per entry. |
| Never | `lv_indev_reset`, display wake, or z-order calls in this render. Keypad live while `checking...` shows. A sign-in side effect inside render (that runs once from `note_sign_in_edge`). |
| Open gaps | Cover-down transitions are not implemented (instant swap). |

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Exact keypad dim amount while checking |

Resolved: cover-down transitions; v1 PIN vertical placement; in-slot 3×3 grid
with `fl-type-32` digits; wrong-pin 2 s; cooldown on separate screen;
`checking...` replaces the four PIN marks; keypad dims and ignores taps while
checking.

## Revision history

- **0.5 — 2026-10-10:** h19 cover-down; v1 product PIN layout; 2 s wrong pin;
  cooldown screen; keypad from product shell not h03 demo.
- **0.9 — 2026-10-21:** Checking dims the keypad. Digits are `fl-type-32` and
  stay inside the lower slot. Lower-slot status uses a 150–200 ms crossfade.
- **0.8 — 2026-10-21:** `checking...` replaces PIN marks. Connecting swaps the lower slot; no overlay card.
- **0.7 — 2026-10-20:** Overlay may cover this screen and block keypad touch.
- **0.6 — 2026-10-20:** Compose upper and lower PIN content through panels;
  retain checking placement as OPEN.
- **0.4 — 2026-10-10:** Prior h03-based keypad note superseded.
