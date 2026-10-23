# Recipient picker header panel

| Field | Value |
|---|---|
| ID | `PANEL-HEADER-RECIPIENT-PICKER` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft |
| Owner | Product owner |
| Slot | `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-MESSAGE-RECORD-SEND`](../../experiences/message-recording-and-sending.md) `EXP-006`, `EXP-007` |

## Purpose

Single-line upper heading for recipient selection. Reuses the geometry of
[`PANEL-HEADER-TITLE-LEFT`](PANEL-HEADER-TITLE-LEFT.md). Copy is owned by the
send experience:

| Selection | Heading |
|---|---|
| None | `Send a message to ...` |
| One | `To [name]` |
| Multiple | `To` plus selected names |
| Overflow | First selected first name plus `+N` when names exceed 25 characters |

## Dependencies

- [`SCR-RECIPIENT-PICKER`](../screens/SCR-RECIPIENT-PICKER.md)
