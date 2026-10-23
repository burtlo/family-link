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
