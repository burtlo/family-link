# Recipient carousel body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-RECIPIENT-CAROUSEL` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft |
| Owner | Product owner |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-MESSAGE-RECORD-SEND`](../../experiences/message-recording-and-sending.md) `EXP-006`, `EXP-010` |

## Purpose

Horizontal snap-to-center carousel of [`COMP-USER-CARD`](../components/COMP-USER-CARD.md)
for other hangout members. Focus (centered card) is distinct from selection
(tap-to-toggle). Selected cards show a **2 px** `fl-color-accent` outline only;
no checkmark.

When nobody else is in the hangout, this panel is not mounted. The lower slot
shows centered `No people to message` via [`PANEL-STATUS-MESSAGE`](PANEL-STATUS-MESSAGE.md).

## Dependencies

- [`SCR-RECIPIENT-PICKER`](../screens/SCR-RECIPIENT-PICKER.md)
- [`PANEL-BODY-ROSTER-CAROUSEL`](PANEL-BODY-ROSTER-CAROUSEL.md) — shared snap rules
