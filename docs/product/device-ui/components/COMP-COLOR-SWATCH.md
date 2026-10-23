# Color swatch component

ID: `COMP-COLOR-SWATCH`
Type: Component
Revision: 0.1
Status: Draft — 44 px circular chip, gray ring, gold selected outline from owner-decided sketch layout
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

One circular color chip in
[`PANEL-SIDE-COLOR-PALETTE`](../panels/PANEL-SIDE-COLOR-PALETTE.md). The panel
places six instances. This component owns fill, ring, selected outline, and the
circular hit rule.

## Anatomy

```text
        outline (selected only)
          2 px #E8C040
          1 px pad
     ┌─────────────────┐
     │   ring 2 px     │
     │   #B8BEC6       │
     │    ┌───────┐    │
     │    │ fill  │    │  44 × 44 circle
     │    └───────┘    │
     └─────────────────┘
```

## Dimensions

| Constant | Value |
|---|---|
| Diameter | **44 px** |
| Radius | **22 px** |
| Ring | **2 px** border, `#B8BEC6` (`fl-color-swatch-ring`), full opacity |
| Selected outline | **2 px**, `#E8C040` (`fl-color-accent`), full opacity |
| Outline pad | **1 px** between ring and outline |
| Corner | Circle (`radius = 22`) |

Placement of the six chips (centers, zig-zag, pitch) belongs to the palette
panel, not this component.

## Visual states

| State | Presentation |
|---|---|
| Resting | Fill + 2 px gray ring. Outline width **0** |
| Selected | Resting chrome plus 2 px gold outline and 1 px outline pad |
| Pressed | Same as selected after the press is accepted. There is no separate pressed fill |

Only the selected chip shows the gold outline. Switching selection removes the
outline from the previous chip and adds it to the next. Invalidate those two
rectangles only.

## Interaction surface

Hit area is the **circle** of radius 22 about the chip center, not the
rectangular widget box. A press inside that circle selects this chip. A press
in the corner of the 44 × 44 box but outside the circle does not select.

Selection happens on **press** (finger down). Moving across a chip does not
select it.

## Tokens and assets

| Token / value | Use |
|---|---|
| Palette fill hex | Face color from the mounting panel |
| `fl-color-swatch-ring` (`#B8BEC6`) | Ring on every chip |
| `fl-color-accent` (`#E8C040`) | Selected outline |

No label. No icon.

## Accessibility and hardware constraints

These chips are **44 px**, below `fl-touch-min-height` (48 px). `SK-007` locks
that size for this palette.
[`FOUNDATION-BUTTON-MINIMUMS`](../foundations/FOUNDATION-BUTTON-MINIMUMS.md)
does not apply to circular pickers.

RGB565 quantization applies. Gold outline uses the same accent as filled PIN
marks.

## Traceability

| Source | Contribution |
|---|---|
| `SK-007` | 44 × 44 circle, 2 px gray ring, 2 px gold outline, 1 px pad |
| Lab-10 swatch chrome | Border `#B8BEC6`; selected outline `#E8C040`; circular hit test |

## OPEN items

None for this chip. Stored points use RGB565 inside `flsk2` (`OPEN-SKP-01`). That packing does not change the ring or outline.

## Readiness

- **Owner review:** Diameter, ring, outline, pad, and circular hit are ready for correction.
- **Visual approval:** Pending catalog review of selected vs resting chips.
- **Implementation:** Chrome is specified.
- **Verification:** Six instances on `CAT-17`–`CAT-19`.

## Revision history

- **0.1 — 2026-10-21:** Initial swatch from `SK-007` and the lab-10 palette.
