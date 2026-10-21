# Device motion foundation

ID: `FOUNDATION-MOTION`
Type: Foundation
Revision: 0.5
Status: Draft — stage transitions owner-aligned (h19 cover-down); lower-slot status uses a 150–200 ms crossfade
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

## Lower-slot status

[`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) replaces the lower
split-pane body. Use a **150–200 ms** crossfade: fade the current lower content
out, then fade the new state in. Keep the upper panel stable. Do not slide.
Rapid connectivity fluctuations must not flicker; the exact hold is delegated
to engineering. Cover-down remains the stage-change default.

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

- **0.5 — 2026-10-21:** Lower-slot status is a 150–200 ms crossfade; no slide.
- **0.4 — 2026-10-21:** Withdraw the status overlay slide. Lower-slot status uses a subtle in-slot change.
- **0.3 — 2026-10-20:** Record the centered status-message overlay as separate from cover-down.
- **0.2 — 2026-10-10:** Cover-down default; carousel snap deferred; web twin rejected.
- **0.1 — 2026-10-10:** Cross-fade proposed (superseded for login stages).
