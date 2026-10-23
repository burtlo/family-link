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
