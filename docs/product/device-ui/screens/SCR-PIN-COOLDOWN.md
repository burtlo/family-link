# PIN cooldown screen

ID: `SCR-PIN-COOLDOWN`
Type: Screen composition
Revision: 0.1
Status: Draft
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN cooldown after five failed attempts (`EXP-014`, `DEC-034`)

## Purpose

Full-canvas presentation during the **60-second** wrong-PIN lockout. Replaces
the split PIN layout; not a panel on `SCR-PIN-ENTRY`.

## Entry and exit

| Event | Transition |
|---|---|
| Fifth wrong PIN on `SCR-PIN-ENTRY` | Forward [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) |
| Countdown reaches zero | Reverse cover-down to [`SCR-SIGN-IN-ROSTER`](SCR-SIGN-IN-ROSTER.md) |

Carousel touch on the roster is normal after exit. If Wi-Fi or the server is
unavailable during cooldown, [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md)
covers this screen and blocks touch.

## Copy

Neutral owner copy (experience):

`Too many attempts. Try again in <seconds>`

- `<seconds>` is a **whole-second** countdown updated at least once per second.
- Typography: `fl-type-24` or `fl-type-16` for the message body — **OPEN** until
  measured for one-screen fit; prefer `fl-type-24` centered.

## Visual

- Background: `fl-color-surface` ([`THEME-DARK`](../themes/THEME-DARK.md)).
- No keypad; no PIN dots.
- Message centered in the content well unless owner specifies top-weighted layout.

## Revision history

- **0.1 — 2026-10-10:** Full-screen cooldown with cover-down entry/exit.
