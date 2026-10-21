# Empty hangout screen

ID: `SCR-EMPTY-HANGOUT`
Type: Screen composition
Revision: 0.4
Status: Draft
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), empty hangout stage (`EXP-020`)

## Purpose

Signed-out presentation when the server returns a reachable hangout with **zero**
users, and the surface under [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md)
before the first valid roster (`EXP-038`). Not used when the roster has
selectable cards.

Headings:

| When | Copy |
|---|---|
| Before the first valid roster | `Loading hangout ...` |
| Server returned zero users | `No people in this hangout` |

## Composition

```text
SCR-EMPTY-HANGOUT
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-LEFT  (copy: empty hangout)
   └─ lower → (no carousel; surface fill only)
```

## Copy

| Element | Copy | When |
|---|---|---|
| Heading | `Loading hangout ...` | Before first valid roster |
| Heading | `No people in this hangout` | Valid empty roster |

Lower slot shows only `fl-color-surface` background — no placeholder cards.

## Transitions

When roster becomes non-empty, transition forward to
[`SCR-SIGN-IN-ROSTER`](SCR-SIGN-IN-ROSTER.md) with the cover-down default from
[`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) and
[`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md).

## Revision history

- **0.4 — 2026-10-20:** Pre-roster heading `Loading hangout ...`; known-empty heading unchanged.
- **0.3 — 2026-10-20:** Also used as the surface under the connecting / Wi-Fi overlay before the first roster.
- **0.2 — 2026-10-20:** Replace the stale cross-fade recommendation with the cover-down default.
- **0.1 — 2026-10-10:** Initial empty-hangout screen; fixed owner copy.
