# PIN entry screen

| Field | Value |
|---|---|
| ID | `SCR-PIN-ENTRY` |
| Type | Screen composition |
| Revision | 0.5 |
| Status | Draft — v1 shell layout and transitions owner-aligned; `checking...` placement OPEN |
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
   ├─ upper → identity row + four PIN slot labels (v1 product layout)
   └─ lower → COMP-PIN-KEYPAD
```

## Upper band (v1 product shell, ribbonless split)

Mapped into upper `slot_chrome_bounds` (80 px). Legacy v1 product PIN screen
geometry:

| Element | Rule |
|---|---|
| Identity cluster (left) | `Sign in as` prefix + **28 px** avatar + display name (`fl-type-24`, weight 600); name ellipsize end |
| PIN slots (right) | Four positions, LTR; filled digit shows `*` in `fl-color-accent`; empty shows `_` in `fl-color-text-secondary`; slot font **`fl-type-32`** (or nearest linked size if 32 absent — match v1 device fallback) |
| Vertical alignment | PIN slot vertical center aligned with identity row midline (v1 `pin_shared_mid_y_ofs` behavior) |

Wrong PIN: replace the **identity cluster** (prefix, avatar, name — not the
PIN slots) with the literal copy **`wrong pin`** for **2 seconds**, then restore
the identity cluster and clear entered digits (experience clears entry).

## Lower band

[`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) — v1 product keypad math in
the 160 px lower chrome (10 px top inset in pane).

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
| Verification / cooldown on keypad | Ignore touch; visual dim **OPEN** if not full-screen cooldown |

BOOT (`DEC-037`): no digits → roster; 1–3 digits → clear in place; after digit
four → ignore BOOT until verification completes.

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | While verifying after the 4th digit, where does **`checking...`** appear? (A) replace identity cluster like `wrong pin`; (B) small line under identity; (C) replace the four PIN slots temporarily) |

Resolved: cover-down transitions; v1 PIN vertical placement; v1 keypad grid;
wrong-pin 2 s; cooldown on separate screen.

## Revision history

- **0.5 — 2026-10-10:** h19 cover-down; v1 product PIN layout; 2 s wrong pin;
  cooldown screen; keypad from product shell not h03 demo.
- **0.4 — 2026-10-10:** Prior h03-based keypad note superseded.
