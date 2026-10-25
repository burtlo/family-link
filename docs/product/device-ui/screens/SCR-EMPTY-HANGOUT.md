# Empty hangout screen

ID: `SCR-EMPTY-HANGOUT`
Type: Screen composition
Revision: 0.7
Status: Draft — empty copy in the lower body; header stays `Sign in as ...`
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), empty hangout stage (`EXP-020`)

## Purpose

Signed-out presentation when the server returns a reachable hangout with **zero**
users, and the pre-roster surface before the first valid roster (`EXP-038`).
While connecting or Wi-Fi is unavailable, keep the current upper heading and
swap the lower slot to [`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md).
Not used when the roster has selectable cards.

| When | Upper heading | Lower body |
|---|---|---|
| Before the first valid roster | `Loading hangout ...` | `connecting` or `no Wi-Fi` |
| Server returned zero users | `Sign in as ...` | `No people in this hangout` |

## Composition

```text
SCR-EMPTY-HANGOUT
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-LEFT
   └─ lower → PANEL-STATUS-MESSAGE
```

## Copy

| Element | Copy | When |
|---|---|---|
| Heading | `Loading hangout ...` | Before first valid roster |
| Heading | `Sign in as ...` | Valid empty roster (`DEC-065`) |
| Lower body | `No people in this hangout` | Valid empty roster; not a header (`DEC-062`) |
| Lower body | `connecting` / `no Wi-Fi` | Connectivity status; replaces the empty-body copy |

No placeholder cards. The empty-roster sentence uses
[`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) with no dots.

## Transitions

When roster becomes non-empty, transition forward to
[`SCR-SIGN-IN-ROSTER`](SCR-SIGN-IN-ROSTER.md) with the cover-down default from
[`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) and
[`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md).

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_LOGIN`; same owner and presenter as `SCR-SIGN-IN-ROSTER`. |
| Visible surfaces | Upper: heading label (`Loading hangout ...` before the roster is known, `Sign in as ...` once known-empty). Lower: centered status label. No cards. |
| Input | None. |
| Enters when | Signed out and not muted, with no non-empty roster yet, or a known-empty hangout. |
| Leaves when | Roster gains members → roster. Mute → blank. |
| Background work | Login HTTP roster requests only. |
| Update levels | Level 1 text swaps through change-detected setters; alignment set once at attach. |
| Never | Label text or alignment rewritten every tick. Roster cards visible with empty copy. |

## Revision history

- **0.7 — 2026-10-21:** Known-empty header is `Sign in as ...` (`DEC-065`).
- **0.6 — 2026-10-21:** Empty-roster copy moves to the lower body (`DEC-062`).
- **0.5 — 2026-10-21:** Connecting / Wi-Fi swap the lower slot; no overlay card.
- **0.4 — 2026-10-20:** Pre-roster heading `Loading hangout ...`; known-empty heading unchanged.
- **0.3 — 2026-10-20:** Also used as the surface under the connecting / Wi-Fi overlay before the first roster.
- **0.2 — 2026-10-20:** Replace the stale cross-fade recommendation with the cover-down default.
- **0.1 — 2026-10-10:** Initial empty-hangout screen; fixed owner copy.
