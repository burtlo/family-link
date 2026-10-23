# Message inbox screen

ID: `SCR-MESSAGE-INBOX`
Type: Screen composition
Revision: 0.1
Status: Draft — composition owner-aligned to carousel experience
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md)

## Purpose

Signed-in message browsing and playback stage after endpoint login (`EXP-012`).
Behavior, ordering, connectivity, and playback policy are owned by the
experience. This document owns visible composition only.

## Composition tree

```text
SCR-MESSAGE-INBOX
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-MESSAGE-SENDER
   └─ lower → PANEL-BODY-INBOX-CAROUSEL
        or → PANEL-STATUS-MESSAGE (empty, connecting, no Wi-Fi)
```

## Experience authority (copy and slots)

| State | Upper | Lower | Authority |
|---|---|---|---|
| Messages present | `From [sender name]`; Play/Pause | Message carousel | `DEC-002`, `EXP-004` |
| Empty inbox | `Messages`; Play hidden | Centered `No messages` | `DEC-036`, `EXP-034` |
| Connecting | Retained selection header or `Messages` | `connecting` + dots | `DEC-014`, `EXP-016` |
| `no Wi-Fi` | Same | `no Wi-Fi` | `EXP-017` |
| Sketch visible on card | Header unchanged | Card face is sketch; portrait/meta hidden | `DEC-044`, `SK-009` |

## Wireframe (messages present)

```text
┌────────────────────────────────────────┐
│ From Arlo                    [ Play ]  │
├────────────────────────────────────────┤
│  [dim]   [ message card ]    [dim]     │
└────────────────────────────────────────┘
```

## Catalog scenes

See [`catalog/README.md`](../catalog/README.md) — `CAT-21` through `CAT-26`.

Empty inbox (`CAT-23`) mounts `PANEL-HEADER-MESSAGE-SENDER` in the upper slot
with identity `Messages` and transport hidden—the same panel as carousel and
connectivity variants, not `PANEL-HEADER-TITLE-LEFT`.

## Readiness

- **Owner review:** Copy and slot swaps match locked carousel rows.
- **Implementation:** P6 owns carousel logic; LAB-14 mounts catalog scenes.
