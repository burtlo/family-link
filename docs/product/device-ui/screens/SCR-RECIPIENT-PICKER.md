# Recipient picker screen

ID: `SCR-RECIPIENT-PICKER`
Type: Screen composition
Revision: 0.1
Status: Draft
Owner: Product owner
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-MESSAGE-RECORD-SEND`](../../experiences/message-recording-and-sending.md)

## Purpose

Choose message recipients after the physical red record control from the inbox.
Selection rules, empty hangout, and header overflow are owned by the experience.

## Composition tree

```text
SCR-RECIPIENT-PICKER
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-RECIPIENT-PICKER
   └─ lower → PANEL-BODY-RECIPIENT-CAROUSEL
        or → PANEL-STATUS-MESSAGE (`No people to message`)
```

## Experience authority

| State | Upper heading | Lower | Authority |
|---|---|---|---|
| None selected | `Send a message to ...` | Recipient carousel | `EXP-007`, `DEC-014` |
| One or more selected | `To [name]` or `To` + names; `+N` overflow | Carousel with accent outline on selected cards | `EXP-006`, `EXP-007`, `DEC-043` |
| No other members | `Send a message to ...` | Centered `No people to message`; no cards | `EXP-010`, `DEC-045` |

## Catalog scenes

`CAT-27` through `CAT-30`.

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_SEND` with shell stage `PICKER`. |
| Owner | `product_send_bridge.cpp` → `paint_for_stage()`; visibility only from `product_send_present`. |
| Visible surfaces | Upper: `Send a message to ...` / `To ...` header. Lower: picker card row or `No people to message`. |
| Input | Card tap → `toggle_selection`; row scroll → `set_focus`. Callbacks repaint picker widgets only. RED and BOOT arrive through the button poll and `product_stage_on_*`. |
| Enters when | RED on the inbox (send stage `Picker`); the send module pauses playback and remembers the inbox card. |
| Leaves when | RED with ≥1 selected → record (wipe). BOOT → inbox (picker reset). Mute → inbox place and login screen; capture discarded. |
| Background work | `product_send_service`: stage clock, outbox HTTP. No painting in the service. |
| Update levels | Level 1 header and card chrome; Level 3 enter/leave is the compositor's. |
| Never | Picker cards and inbox carousel visible together. Send code showing or hiding the inbox. |

