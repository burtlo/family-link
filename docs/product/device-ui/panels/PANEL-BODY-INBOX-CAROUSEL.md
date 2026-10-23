# Inbox message carousel body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-INBOX-CAROUSEL` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — card geometry from `COMP-MESSAGE-CARD` |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md) |

## Purpose

Horizontal snap-to-center carousel of [`COMP-MESSAGE-CARD`](../components/COMP-MESSAGE-CARD.md)
for the signed-in inbox. Swipe, tap, selection, and ordering are owned by the
experience. This panel owns the lower-slot collection presentation.

## Chrome and bleed

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface-lower` on full lower `slot_chrome_bounds` |
| Track | Full 320 px width; side peeks use reduced scale and opacity like roster cards |

## Collection presentation

- One `COMP-MESSAGE-CARD` per inbox message, oldest left, newest right.
- While sketch ink is visible on the focused card, hide portrait and metadata
  on that card (`DEC-044`, `SK-009`).
- Audio-only messages keep portrait, sender name, and `m:ss` duration.

## Visual states

| State | Lower slot |
|---|---|
| One or more messages | Carousel with centered focus |
| Empty inbox | Not mounted; [`PANEL-STATUS-MESSAGE`](PANEL-STATUS-MESSAGE.md) shows `No messages` |
| Connecting / `no Wi-Fi` | Not mounted; status panel replaces lower slot |

## Dependencies and consumers

- [`SCR-MESSAGE-INBOX`](../screens/SCR-MESSAGE-INBOX.md)
- [`COMP-MESSAGE-CARD`](../components/COMP-MESSAGE-CARD.md)
