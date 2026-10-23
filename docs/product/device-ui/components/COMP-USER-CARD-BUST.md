# User card — bust portrait

ID: `COMP-USER-CARD-BUST`
Type: Component
Revision: 0.1
Status: Draft — proposed layout for owner review
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

Proposed card treatment for a user bust portrait that uses most of the card
above the display name. It is intended for roster, inbox, and send-user
carousels where [`COMP-USER-CARD`](COMP-USER-CARD.md) is currently used.

This is a new visual proposal, not an adopted replacement. Consuming panels
continue to reference `COMP-USER-CARD` until the owner approves this treatment
and the avatar foundation is reconciled. Behavior stays in the applicable
experience and screen specs.

The proposed portrait treatment follows the twelve transparent pixel-art busts
in `codex://threads/01a1617e-ff23-7462-8af5-fa4b004b9877`: head, shoulders, and
upper torso, with no circular frame or background shape.

## Anatomy

```text
┌────────────────── 132 px ───────────────────┐
│             card face, radius 8             │
│        ┌──────────────────────────┐         │
│        │                          │         │
│        │  bust portrait canvas    │ 102 px  │
│        │  transparent background  │  (+8 px top inset) │
│        │                          │         │
│        └──────────────────────────┘         │
│             display name                    │ 18 px line box
└─────────────────────────────────────────────┘
                       133 px
```

The drawing shows hierarchy, not scale. The geometry table below is
authoritative.

## Geometry (proposed; card-relative pixels)

| Region / token | x | y | Width | Height | Rule |
| --- | --- | --- | --- | --- | --- |
| Card | 0 | 0 | 132 | 133 | Retain `fl-card-roster-width`, `fl-card-roster-height`, and 8 px corner radius |
| Bust canvas | 11 | 8 | 110 | 102 | Centered; transparent bust shown at native pixel size, with no circular mask or crop; 8 px top inset keeps the full head visible on scaled peek cards |
| Name line box | 6 | 112 | 120 | 18 | Centered; 2 px clear gap after the bust canvas (bottom y=110) and 3 px bottom inset |

The bust asset canvas is 110 × 110 px. If source art is narrower, preserve its
native pixels and center it on this transparent canvas; do not stretch or crop
the character. The complete head and shoulders remain visible. Only the
card's 8 px outer corner radius clips card-face content.

The unchanged 132 × 133 px footprint remains vertically centered in the
160 px lower carousel slot. The full card remains the touch surface and exceeds
`fl-touch-min-height`.

## Content contract

| Content | Rule |
| --- | --- |
| Bust | One original, family-safe catalog portrait per user; transparent canvas, no circular frame, full bust visible |
| Name | One line, centered, `fl-type-16`, weight 600, paired `fl-color-text-on-card` |
| Long name | Ellipsize at end using ASCII `...`; no adaptive font sizing |
| Duplicate names | Show as-is; disambiguation remains outside this component |
| Card face | Continue to use the user's current `card_background_id` from [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md) |

Transparent pixels reveal the card face. The card does not add a portrait
backplate, tint the bust, or render `profile_accent`.

## Visual states

These proposed card states retain the current user-card contract:

### Default (not focused)

- Opacity approximately **0.58** and scale approximately **0.9**; targets only,
  with snap timing still governed by [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md).

### Focused (carousel center)

- Opacity **1.0**, scale **1.0**.

### Picked (send-user)

- **2 px** outline `fl-color-accent`, the global selection token.

### Pressed

- Brief opacity dip.

## Tokens, assets, and hardware constraints

- Card geometry and typography use [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md).
- Face color and paired name color use [`THEME-DARK`](../themes/THEME-DARK.md) and the card-background catalog.
- This layout expects transparent bust assets up to 110 × 110 px, composited over the card face. Asset authoring, conversion format, catalog membership, flash budget, and rendering cost are not specified here.
- The existing avatar foundation specifies 66 px circular card portraits on a matte background. That conflicts with this proposal; do not treat this component as approval to change the asset pipeline or other avatar surfaces.
- RGB565 contrast and pixel-art legibility require review at native card size on the device.

## Traceability and consumers

| Source | Contribution |
| --- | --- |
| Product owner request, 2026-10-22 | Replace the circular card portrait treatment with a larger bust that uses more of the card while retaining the name |
| Avatar source thread `01a1617e-ff23-7462-8af5-fa4b004b9877` | Twelve standalone transparent pixel-art busts; head, shoulders, and upper torso; source canvas up to 110 × 110 px |
| [`COMP-USER-CARD`](COMP-USER-CARD.md) revision 0.5 | Existing outer geometry, card background, name, focus, selection, and pressed-state contract |
| [`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md) | Current lower-slot card consumer and 160 px carousel slot |

The roster panel and other existing consumers still name `COMP-USER-CARD`.
Update their references only after the owner selects this proposal.

## OPEN items

| ID | Question | Consequence |
| --- | --- | --- |
| `UI-OPEN-001` | Approve the proposed 110 × 110 px bust window, 132 × 133 px outer card, and name-row placement? | Required before adopting this component in consuming panels |
| `UI-OPEN-002` | Reconcile [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md) and its tokens with transparent bust canvases; decide which non-card surfaces keep the existing circle treatment | Required before treating the busts as the V2 catalog asset contract |
| `UI-OPEN-003` | Review bust/name balance, transparency over all catalog card fills, and focused/unfocused legibility on the device | Required for visual approval |

## Readiness

- **Owner review:** Ready to review the proposed card proportions and placement.
- **Visual approval:** Pending owner review and a native-size device rendering.
- **Implementation:** Not ready for product adoption; `UI-OPEN-001` and `UI-OPEN-002` remain unresolved.

## Revision history

- **0.1 — 2026-10-22:** Proposed a large transparent bust portrait treatment as a separate component; retained the existing card footprint and documented the avatar-foundation conflict.
