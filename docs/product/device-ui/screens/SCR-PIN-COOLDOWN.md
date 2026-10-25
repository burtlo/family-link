# PIN cooldown screen

ID: `SCR-PIN-COOLDOWN`
Type: Screen composition
Revision: 0.4
Status: Draft — two-line copy owner-decided; type-fit delegated
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN cooldown after five failed attempts (`EXP-014`, `DEC-034`, `DEC-054`, `DEC-058`)

## Purpose

Full-canvas presentation during the **60-second** wrong-PIN lockout. Replaces
the split PIN layout; not a panel on `SCR-PIN-ENTRY`.

## Entry and exit

| Event | Transition |
|---|---|
| Fifth wrong PIN on `SCR-PIN-ENTRY` | Forward [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) |
| Wi-Fi or server becomes unavailable | Stay on this screen. The 60-second timer continues and does not pause or reset (`DEC-054`) |
| Mute | Display off. Timer continues in the background. Mute does not reset, pause, or extend the lockout (`DEC-058`) |
| Unmute while time remains | Show this screen with the remaining countdown |
| Unmute after the timer has reached zero | Normal signed-out sign-in flow; do not remount this screen |
| Countdown reaches zero and roster is available | Reverse cover-down to [`SCR-SIGN-IN-ROSTER`](SCR-SIGN-IN-ROSTER.md) |
| Countdown reaches zero and Wi-Fi or the server is still unavailable | Reverse cover-down to the split-pane login surface; lower slot shows [`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) |

This screen takes presentation priority over `connecting` and `no Wi-Fi`.
Do not swap back to split-pane until the timer ends.

## Copy

Two centered lines (`DEC-063`):

```text
Too many attempts.
Try again in <seconds>.
```

- `<seconds>` is a **whole-second** countdown updated at least once per second.
- Prefer `fl-type-24` centered. Engineering may adjust type size so the full
  60-second duration and remaining time stay readable.

## Visual

- Background: `fl-color-surface` ([`THEME-DARK`](../themes/THEME-DARK.md)).
- No keypad; no PIN dots.
- Message centered in the content well unless owner specifies top-weighted layout.

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_LOGIN`; cooldown branch of `render_login_copy()`. |
| Visible surfaces | Full-canvas cooldown labels only. Heading, status label, roster row, and PIN pad hidden. |
| Input | None. |
| Enters when | Session PIN phase `Cooldown`. |
| Leaves when | Session ends the cooldown → roster or status per the session. Mute → blank; the session keeps the remaining countdown. |
| Background work | Login HTTP only. The countdown is session time, not an LVGL timer. |
| Update levels | Level 1: the countdown line uses a change-detected setter, so it redraws once per second, not per tick. |
| Never | Keypad or roster visible or tappable during cooldown. |
| Open gaps | Cover-down entry and exit are not implemented. |

## Revision history

- **0.4 — 2026-10-21:** Two-line copy; prefer 24 px; type-fit delegated (`DEC-063`).
- **0.3 — 2026-10-21:** Mute preserves the remaining countdown (`DEC-058`).
- **0.2 — 2026-10-21:** Stay full-canvas through connectivity loss; show
  status only after the timer ends (`DEC-054`).
- **0.1 — 2026-10-10:** Full-screen cooldown with cover-down entry/exit.
