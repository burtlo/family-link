# Device motion foundation

ID: `FOUNDATION-MOTION`
Type: Foundation
Revision: 0.3
Status: Draft — stage transitions owner-aligned (h19 cover-down); status-overlay motion separate
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware device UI (LVGL)

## Purpose

Records motion policy for v2. Stage changes use the h19-validated
**cover-down** family unless a screen spec says otherwise.

## Normative pattern

[`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) — **400 ms**,
ease-in-out quint, forward and reverse as defined there.

Cross-fade is **withdrawn** as the default roster ↔ PIN policy (owner, 2026-10-10).

## Information overlay

[`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) slides up from the
bottom to the canvas center and slides down off screen when its condition
clears. That motion is not cover-down: cover-down parks a full screen at
`y = 0`. Duration and easing for the overlay are OPEN on the component.

## Carousel focus motion

Roster carousel **snap/focus** animation (scale/opacity) is **not** signed off.
The v1 product web twin is **not** authoritative. Tuning and performance work
remain on the v1 device carousel implementation before v2 copies timing constants.

| Item | Status |
|---|---|
| Focus scale/opacity targets | See [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) numeric targets |
| Snap duration/easing on device | **OPEN** — owner pending performance sign-off |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Carousel snap animation after v1 performance tweaks | Roster feel parity |
| `UI-OPEN-002` | Shared transition helper vs per-screen LVGL roots | Firmware architecture |

## Revision history

- **0.3 — 2026-10-20:** Record the centered status-message overlay as separate from cover-down.
- **0.2 — 2026-10-10:** Cover-down default; carousel snap deferred; web twin rejected.
- **0.1 — 2026-10-10:** Cross-fade proposed (superseded for login stages).
