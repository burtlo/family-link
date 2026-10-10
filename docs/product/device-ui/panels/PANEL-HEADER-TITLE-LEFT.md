# Header title (left) panel

ID: `PANEL-HEADER-TITLE-LEFT`  
Type: Panel module  
Revision: 0.1  
Status: Draft  
Owner: Product owner  
Approval: No visual approval claimed  
Slot: `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)

## Purpose

Single-line, left-aligned heading in the upper content inset. Used on sign-in
roster, empty hangout, and roster refresh-wait states.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` on full upper `slot_chrome_bounds` (sign-in family) |
| Text | Place inside upper `slot_content_bounds`; left-aligned, vertically centered in the 56 px content band |

## Typography

| Property | Value |
|---|---|
| Token | `fl-type-24` |
| Weight | **600** (matches v1 sign-in / picker title emphasis) |
| Color | `fl-color-text-primary` |
| Lines | One line; ellipsize end if needed |

## Copy variants (experience-driven)

| State | Heading copy | Authority |
|---|---|---|
| Roster ready | `Sign in as ...` | `EXP-006`, `DEC-004` |
| Awaiting roster after unknown user | `Updating hangout ...` | `EXP-036` |
| Empty hangout | `No people in this hangout` | `EXP-020`, `DEC-017` |

Use ASCII three periods in `Sign in as ...` and `Updating hangout ...`.

## Interaction

This panel is display-only. It does not own touch targets.

## Revision history

- **0.1 — 2026-10-10:** Initial panel; three heading variants tied to experience.
