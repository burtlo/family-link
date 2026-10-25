# Send feedback screen

ID: `SCR-SEND-FEEDBACK`
Type: Screen composition
Revision: 0.1
Status: Draft
Owner: Product owner
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-MESSAGE-RECORD-SEND`](../../experiences/message-recording-and-sending.md)

## Purpose

Continuous preparing → sending → sent sequence, connectivity wait, and definite
refusal after recording finishes. One screen composition; phase changes swap the
lower panel only.

## Composition tree

```text
SCR-SEND-FEEDBACK
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-SUBTITLE
   └─ lower → PANEL-SEND-FEEDBACK
```

## Experience authority

| Phase | Upper title | Upper subtitle | Lower body | Authority |
|---|---|---|---|---|
| Progress | `sending to ...` | Frozen recipient names; `+N` overflow | `preparing` / `sending` + dots | `EXP-018`, `EXP-020`, `EXP-042` |
| Success | Same | Same | `sent` | `EXP-020` |
| Refusal | Same | Same | `couldn't send` | `EXP-034`, `DEC-034` |
| Queue | Same | Same | `connecting` or `no Wi-Fi` | `EXP-028`, `DEC-028` |

Subtitle and body copy are text-only; no status icons (`EXP-043`).

## Catalog scenes

`CAT-31` through `CAT-36`.

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_SEND` with shell stage `RECEIPT`. Couldn't-record is painted by the same module under shell stage `PICKER`. |
| Owner | `product_send_bridge.cpp` → `paint_for_stage()`; visibility only from `product_send_present`. |
| Visible surfaces | Upper: `sending to ...` and frozen recipients. Lower: phase body (`preparing`, `sending` + dots, `sent`, `couldn't send`, connectivity copy). |
| Input | BOOT leaves when the session allows it (`receipt_boot_leaves`). RED is ignored. No touch targets. |
| Enters when | RED finishes a take (finish collapse → receipt), or RED on the inbox during a pending own send. Couldn't-record: RED in the picker when capture cannot start. |
| Leaves when | Stage clock returns to the inbox (`returned_inbox` → selection restored) or BOOT. Mute → login screen; a finished take stays stored and does not upload while signed out. |
| Background work | Outbox upload and dot phase (400 ms) in `product_send_service`. |
| Update levels | Level 1 body text and dots only. |
| Never | Receipt visible with the inbox carousel. An upload while signed out. |

