# Empty hangout screen

ID: `SCR-EMPTY-HANGOUT`  
Type: Screen composition  
Revision: 0.1  
Status: Draft  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas  
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), empty hangout stage (`EXP-020`)

## Purpose

Signed-out presentation when the server returns a reachable hangout with **zero**
users. Not used when the roster has selectable cards.

## Composition

```text
SCR-EMPTY-HANGOUT
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-LEFT  (copy: empty hangout)
   └─ lower → (no carousel; surface fill only)
```

## Copy

| Element | Copy |
|---|---|
| Heading | `No people in this hangout` |

Lower slot shows only `fl-color-surface` background — no placeholder cards.

## Transitions

When roster becomes non-empty, transition to [`SCR-SIGN-IN-ROSTER`](SCR-SIGN-IN-ROSTER.md).
Stage motion **SHOULD** use cross-fade per
[`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) once verified.

## Revision history

- **0.1 — 2026-10-10:** Initial empty-hangout screen; fixed owner copy.
