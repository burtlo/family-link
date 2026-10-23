# Send feedback body panel

| Field | Value |
|---|---|
| ID | `PANEL-SEND-FEEDBACK` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — text-only status; dots only on `preparing` and `sending` |
| Owner | Product owner |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-MESSAGE-RECORD-SEND`](../../experiences/message-recording-and-sending.md) `EXP-020`, `EXP-028`, `EXP-034`, `EXP-043` |

## Purpose

Centered lower-body status after recording finishes. Replaces the recipient
carousel for the preparing → sending → sent sequence, connectivity wait, and
definite refusal. No status icons.

| Phase | Body copy | Dots |
|---|---|---|
| `preparing` | `preparing` | Three progressive dots |
| `sending` | `sending` | Three progressive dots |
| `sent` | `sent` | None |
| `couldn't send` | `couldn't send` | None |
| Connectivity wait | `connecting` or `no Wi-Fi` | Dots only for `connecting` |

The upper slot stays on [`PANEL-HEADER-TITLE-SUBTITLE`](PANEL-HEADER-TITLE-SUBTITLE.md)
with title `sending to ...` and frozen recipient names.

## Dependencies

- [`SCR-SEND-FEEDBACK`](../screens/SCR-SEND-FEEDBACK.md)
- [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) — shared centering rules
