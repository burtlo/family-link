# Record sketch screen

| Field | Value |
|---|---|
| ID | `SCR-RECORD-SKETCH` |
| Type | Screen composition |
| Revision | 0.1 |
| Status | Draft — focused drawing + side palette composed from owner-decided sketch layout |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Experience | [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) |

## Purpose and scope

Steady-state record sketch: a left drawing surface and a right color palette.
There is **no** recording timer and **no** record indicator on this screen
(`SK-005`, `EXP-SKP-008`).

This screen does not define capture duration, send, or playback. Terminal
outcome copy from the record-canvas lab is not part of this composition.

## Composition

```text
SCR-RECORD-SKETCH
└─ LAYOUT-SPLIT-PANE-FOCUSED-SIDE
   ├─ focused → PANEL-FOCUSED-DRAWING     (COMP-RECORD-CANVAS)
   └─ side    → PANEL-SIDE-COLOR-PALETTE  (six COMP-COLOR-SWATCH)
```

## Wireframe

```text
 0                              239 240                        319
 0 ┌────────────────────────────────┬────────────────────────────┐
   │                                │         ○ blue             │
   │                                │    ○ red                   │
   │     PAINTABLE 240 × 240        │         ○ green            │
   │     black, 8 px radius         │    ○ gold                  │
   │                                │         ○ white *          │
   │                                │    ○ black                 │
239└────────────────────────────────┴────────────────────────────┘
```

## Visual states

| State | Presentation |
|---|---|
| Empty, white selected | Black canvas. Gold outline on the white swatch (index 1). Default on entry |
| Empty, other color selected | Same canvas. Gold outline on the pressed swatch |
| Drawing | Ink in the selected color. Background swatch paints black |
| Catalog review | Canvas accepts touch and paints; swatches accept press and move the outline |

Entry bloom and ring-wipe are not this screen's steady-state layout.

## Copy

None on this screen.

## Continuity and updates

| Change | Update |
|---|---|
| Select a swatch | Level 0 on the side slot (two chip outlines). Canvas subtree stays |
| Stroke | Level 1 pixels inside the focused canvas |
| Leave record | Screen swap; this layout is not the upper/lower split-pane |

## Traceability

| Source | Screen behavior |
|---|---|
| `SK-004`, `EXP-SKP-003` | 240 × 240 focused canvas |
| `SK-005`, `EXP-SKP-004` | 80 × 240 palette, six colors, no timer |
| `SK-006`, `EXP-SKP-006` | Multi-color ink; background color erases visually |
| `SK-007` | Swatch size, zig-zag, ring, gold outline |

## Dependencies

- [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](../layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
- [`PANEL-FOCUSED-DRAWING`](../panels/PANEL-FOCUSED-DRAWING.md)
- [`PANEL-SIDE-COLOR-PALETTE`](../panels/PANEL-SIDE-COLOR-PALETTE.md)
- [`COMP-RECORD-CANVAS`](../components/COMP-RECORD-CANVAS.md)
- [`COMP-COLOR-SWATCH`](../components/COMP-COLOR-SWATCH.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-21)* No timer or recording indicator (`EXP-SKP-008`, send `DEC-021`) | — |
| `UI-OPEN-002` | *(Resolved 2026-10-22)* Stored points use RGB565 inside `flsk2` (`OPEN-SKP-01`). Placement is unchanged | — |

## Readiness

- **Owner review:** Composition and placement are ready for correction.
- **Visual approval:** Pending `LAB-14` scenes `CAT-17`–`CAT-19`.
- **Implementation:** Visual composition is specified. Capture remains a separate lab.
- **Verification:** Catalog scenes mount this screen.

## Revision history

- **0.1 — 2026-10-21:** Initial record-sketch composition from `SK-004`–`SK-007`
  and the lab-10 drawing layout.
- **0.2 — 2026-10-21:** Closed the timer/record-indicator question against the
  active sketch and send experience decisions.
