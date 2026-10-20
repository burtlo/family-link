# Cover-down screen transition

ID: `TRANSITION-COVER-DOWN`
Type: Pattern
Revision: 0.1
Status: Draft — owner-aligned with h19 device demo
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 (LVGL)

## Purpose

Normative stage transition used when a new screen **covers** the previous one by
sliding up from the bottom edge. **Back** and **reveal-under** use the reverse
(slide down off screen).

Validated in the v1 **h19** message-list transition showcase on device.

## Parameters (h19 as-built)

| Parameter | Value |
|---|---|
| Duration | **400 ms** (`TRANS_MS` in h19) |
| Easing | **Ease-in-out quint** (`LV_ANIM_SET_EASE_IN_OUT_QUINT` / custom bezier path in h19) |
| Geometry | Incoming root starts at `y = +240`, animates to `y = 0`; outgoing root stays fixed underneath |

Forward (**to_detail**): foreground screen is the incoming stage; it moves
`SCR_H → 0`.

Reverse (**back** / reveal previous): foreground (current stage) moves `0 → SCR_H`;
previous stage is exposed underneath.

## Product assignments

| Transition | Direction | Notes |
|---|---|---|
| Sign-in roster → PIN entry | Forward cover-down | Incoming: `SCR-PIN-ENTRY` |
| PIN entry → roster (BOOT / cancel) | Reverse | Same animation inverted |
| PIN entry → message inbox (auth success) | Reverse cover-down | Underlying inbox was prepared; PIN slides away |
| Roster or PIN → PIN cooldown | Forward cover-down | Incoming: [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md) |
| Cooldown → roster (timer elapsed) | Reverse cover-down | Return to sign-in roster |

Other h19 modes (fade, push, wipe) are **not** default product policy unless a
future screen spec cites them explicitly.

## Interaction during transition

Input on the outgoing stage **MUST** be ignored while `trans_busy` (h19 pattern)
until the animation completes.

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Whether v2 reuses h19's dual-layer object model verbatim or a shared transition helper |

## Revision history

- **0.1 — 2026-10-10:** Owner selects h19 cover-down / reverse for login and cooldown flows.
