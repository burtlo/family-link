# Message sender header panel

| Field | Value |
|---|---|
| ID | `PANEL-HEADER-MESSAGE-SENDER` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — identity and transport owner-aligned |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md) `DEC-002`, `EXP-004`, `EXP-034` |

## Purpose

Upper-slot header for the signed-in message inbox. Shows `From [sender name]`
for the selected message and a labeled **Play** or **Pause** control on the
right. There is no playback progress bar.

Copy and when transport is shown, hidden, or disabled are owned by
`EXP-MESSAGE-CAROUSEL`. This panel owns slot composition only.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface-upper` on full upper `slot_chrome_bounds` |
| Identity | One line, left-aligned, `fl-type-24`, weight 600, `fl-color-text-primary` |
| Transport | One trailing button in the upper content band; reuse the 101 × 56 control from [`PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION`](PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md) |

## Visual states

| State | Identity line | Transport |
|---|---|---|
| Message selected | `From [sender name]` | `Play` or `Pause` per playback |
| Empty inbox | `Messages` | Hidden |
| Connecting / `no Wi-Fi` | Unchanged for a retained selection; `Messages` when empty | Disabled or hidden per experience |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Exact disabled styling for transport during connectivity loss | Visual polish |

## Dependencies and consumers

- [`SCR-MESSAGE-INBOX`](../screens/SCR-MESSAGE-INBOX.md)
- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)

## Readiness

- **Owner review:** Copy and Play/Pause-only transport are ready for correction.
- **Implementation:** LAB-14 mounts labeled transport; product firmware follows in P6.
