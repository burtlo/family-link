# Roster carousel body panel

ID: `PANEL-BODY-ROSTER-CAROUSEL`  
Type: Panel module  
Revision: 0.1  
Status: Draft  
Owner: Product owner  
Approval: No visual approval claimed  
Slot: `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)

## Purpose

Horizontal, snap-to-center carousel of [`COMP-USER-CARD`](../components/COMP-USER-CARD.md)
for the sign-in roster. Swipe, tap, focus index, and touch lock are owned by
[`EXP-LOGIN`](../../experiences/endpoint-login.md).

## Chrome and bleed

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` on full lower `slot_chrome_bounds` |
| Track | **Full width** of lower `slot_chrome_bounds` (320 px); cards may peek at edges |
| Vertical | Center `fl-card-roster-height` (133 px) cards within the 160 px slot |

## Snap and alignment

| Rule | Behavior |
|---|---|
| Focus target | Scroll so the focused card's horizontal center aligns with the **viewport center** (160 px wide lower chrome) |
| One user | Center that card; equal empty space left and right |
| First / last user | Center the focused card; **intentional** empty space on the outward side |
| 4 vs 8 users | Same card size, gap, and snap rules |

During programmatic snap animation, carousel touch **MAY** be disabled (v1
reference disables pointer events while `is-snapping`).

## Touch lock (roster refresh wait)

When the experience sets **roster refresh wait** (`EXP-036`):

- Ignore swipe and tap on the carousel.
- Continue showing the last rendered cards until new roster data arrives (stale
  cards acceptable briefly).

When refresh completes, restore normal touch handling.

## Collection updates

- Add/remove children by stable **user ID**, not list index.
- Updating avatar, name, or message color is a **Level 1** property update on
  the existing card node.

## Empty roster

This panel is **hidden** (no cards) for empty hangout; the screen uses the same
shell with an empty lower slot or a dedicated empty presentation per
[`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md).

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Snap animation duration/easing on device | Feel parity with v1 web twin |

## Revision history

- **0.1 — 2026-10-10:** Full-bleed lower carousel; center snap; touch lock for
  updating hangout.
