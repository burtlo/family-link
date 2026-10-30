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

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_INBOX`: signed in and shell stage `INBOX` (or any stage not owned by volume, record, or send). |
| Owner | `InboxViewController` inside the product runtime. The header Play/Pause control and the selected-card sketch surface live there. |
| Visible surfaces | Upper: sender header and transport button. Lower: three-card carousel, or a status / empty body. |
| Input | Card tap or scroll end selects only. Header Play/Pause starts, pauses, resumes, or replays. Playback workers never touch widgets. |
| Enters when | Sign-in edge (`note_sign_in_edge` → shell `INBOX`); BOOT back from volume; picker / receipt / couldn't-record exit. |
| Leaves when | BOOT → volume. RED → picker (send stage). Mute → login screen, display blank. |
| Background work | `product_inbox_service` queues due fetch jobs for the inbox worker and applies results. While signed out the tick discards results so the worker queue cannot fill. The worker is not stopped from the tick. `ui_visible == 0` pauses fetch and connecting retry inside the carousel model. |
| Update levels | Level 1: header and transport labels use `set_label_if_changed`. Level 2: carousel ↔ status body fades the lower root. Level 3 (screen) is the compositor's. |
| Never | Inbox roots visible on any other screen. Inbox code reading the shell stage to decide its own visibility, or calling another module's visibility setter. A per-tick re-layout of cards that did not change. |
| Open gaps | Inbox → record and record → send flows are model-tested but not desk-verified after the 2026-10-24 refactor; see the desk matrix. |

## Readiness

- **Owner review:** Copy and slot swaps match locked carousel rows.
- **Implementation:** P6 owns carousel logic; LAB-14 mounts catalog scenes.
