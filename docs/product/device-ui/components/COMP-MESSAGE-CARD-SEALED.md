# Sealed message-card component

ID: `COMP-MESSAGE-CARD-SEALED`  
Type: Component  
Revision: 0.3  
Status: Draft — owner-requested no-sketch layout; adoption pending reconciliation with the current message-card contract  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

A sealed-envelope presentation for one incoming message card while no sketch
ink is visible. The envelope artwork includes a white circular field at its
center; the sender's avatar is rendered as a separate circular overlay there.
The sender name is at the top of the card; the message duration is at the
bottom-right.

This component specifies appearance only. Inbox membership, card selection,
playback, sketch timing, deletion, and connectivity remain in
[`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md),
[`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md),
and the parent [`COMP-MESSAGE-CARD`](COMP-MESSAGE-CARD.md).

The envelope is a closed-message visual metaphor. It does not indicate an
unread flag or introduce read-state tracking; the inbox experience has no
unread state.

## Authority and dependencies

| Source | Authority | Contribution |
|---|---|---|
| Product owner request, 2026-10-22 | Requested visual direction | Same 132 × 133 px card footprint; prominent closed envelope; sender avatar centered on it; name at card top; duration at bottom-right |
| [`COMP-USER-CARD`](COMP-USER-CARD.md) | Existing component | 132 × 133 px outer footprint, 8 px corners, `fl-type-16` semibold name, long-name ellipsis, profile card background |
| [`COMP-MESSAGE-CARD`](COMP-MESSAGE-CARD.md) | Existing parent component | No-sketch and sketch-visible state boundary; message-card ownership |
| [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md) `DEC-044`, `DEC-046`, `EXP-041`, `EXP-043` | Owner-decided behavior and current visual contract | Hide identity/meta while sketch ink is visible; duration uses `m:ss`; existing no-sketch layout currently places portrait/name/duration in a centered stack |
| [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) `SK-009` | Owner-decided behavior | Restore identity/meta when no sketch ink is visible |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Owner-decided typography | Montserrat 600, using only `fl-type-16`, `fl-type-24`, or `fl-type-32` |
| [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md) | Owner-selected palette | Sender's current `card_background_id` and paired `fl-color-text-on-card` |
| [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md) | Current avatar authority | 66 px inbox/roster avatar and LAB-14 initials fallback; new bitmap artwork remains deferred |

## Anatomy

Wireframe shows hierarchy, not scale. The geometry table is authoritative.

```text
Card origin (0,0)                           132 px
┌────────────────────────────────────────────────┐
│  Audrey                                         │  name, top-left
│                                                │
│       ┌──────────────────────────────┐         │
│       │ \                          / │         │
│       │   \        ◉ avatar       /   │         │
│       │     \__________________/     │         │  closed envelope
│       └──────────────────────────────┘         │
│                                      1:24      │  duration, bottom-right
└────────────────────────────────────────────────┘
                       133 px
```

## Geometry and keylines

All coordinates are card-relative native pixels from the upper-left. Right and
bottom bounds are exclusive. The requested outer footprint is **132 × 133 px**.

| Region | x | y | Width | Height | Proposed placement |
|---|---:|---:|---:|---:|---|
| Outer card | 0 | 0 | 132 | 133 | 8 px corner radius; clips card contents |
| Sender name line box | 11 | 5 | 110 | 18 | Left-aligned; top row |
| Envelope body | 18 | 34 | 96 | 70 | Centered horizontally; 3 px corner radius; center `(66, 69)` |
| White circular field in envelope asset | 51 | 54 | 30 | 30 | Centered on envelope at `(66, 69)`; includes a 1.5 px dark outline |
| Avatar art viewport | 53 | 56 | 26 | 26 | Centered in white badge; circular clip |
| Duration line box | 69 | 112 | 52 | 18 | Right-aligned to x=121; 11 px right and 3 px bottom inset |
| Sketch face when visible | 6 | 6 | 120 | 120 | 1:1 drawing; 6 px left/top and 6 px right, 7 px bottom inset |

The envelope body fill and fold treatment in the review mock are `#FFF3CE`
and `#FBE7B0`; the outside stroke and fold lines use `#101418`. The envelope
has a **2 px** outside stroke. Its main closed fold runs from `(20, 53)` to
`(66, 84)` to `(112, 53)`. Secondary folds use a 1.5 px stroke; they meet the side edges near y=53 and
remain visible around the avatar badge. These envelope colors are proposed
sample values; they are not theme tokens yet.

The envelope artwork is [`message-card-sealed-envelope.png`](message-card-sealed-envelope.png),
a **96 × 70 px** RGBA image displayed at native size at `(18, 34)`. The PNG
contains the closed envelope and the centered opaque white circle; the visible
circle is **30 px** in diameter including its **1.5 px** dark outline. It
contains no avatar art. Render the sender avatar as a separate **26 × 26 px**
image at `(53, 56)`, circularly clipped and layered over the white field.

## Content and typography

| Content | Proposed contract |
|---|---|
| Sender name | One line, left-aligned in the top line box; `fl-type-16` (Montserrat 600) and the sender's paired `fl-color-text-on-card` |
| Long sender name | Ellipsize at the end with ASCII `...`; do not wrap or shrink the type. `Audrey` is the reference name used in the review mock. |
| Message duration | `m:ss`, sourced from the message duration; `fl-type-16` (Montserrat 600), right-aligned in the bottom-right line box using `fl-color-text-on-card` |
| Avatar | Render the sender identity as a separate 26 × 26 px circular image over the centered white field included in the envelope asset |
| Unread marker | None. The sealed envelope is not a read-state marker. |

The envelope asset is [`message-card-sealed-envelope.png`](message-card-sealed-envelope.png). It contains the envelope and white circular field but no portrait; the renderer supplies the sender avatar as an independent layer.

## Visual states

| State | Presentation |
|---|---|
| No sketch ink visible | Show the sender name, sealed envelope with centered avatar badge, and message duration. This covers unplayed messages, audio-only messages, and the next signed-in session after mute clears session sketch progress. |
| Sketch ink visible | The 120 × 120 sketch replaces the complete sealed face. Hide the envelope, avatar, sender name, and duration while ink is visible, including paused or completed sketch playback in the current session. |
| Audio-only playback | Keep the sealed face and duration visible; no sketch or playback progress mark is added. |
| Carousel peek | The complete card scales and dims with the parent carousel; the carousel owns clipping and snap behavior. |

The inbox carousel owns selection and touch behavior. This component adds no
separate hit target or gesture.

## Color, assets, and hardware constraints

- The card face uses the sender's current catalog `card_background_id` and
  paired `fl-color-text-on-card`. Do not use `profile_accent`.
- The white circular field and envelope are proposed foreground treatments.
  Envelope colors need theme-token review before adoption.
- `message-card-sealed-envelope.png` is a host-side RGBA artwork asset, not a
  packed firmware asset. It contains the envelope plus the white circle and
  outline; the renderer places the selected avatar image above that circle. The
  current profile-avatar foundation specifies the initials fallback for V2
  identity surfaces and defers new bitmap artwork.
- The proposed 26 px avatar is smaller than the existing 66 px inbox avatar
  treatment and the 28 px PIN avatar. It is local to this layout proposal and
  does not revise those shared tokens.
- Review at native size on the RGB565 target. Check that the 26 px avatar,
  white ring, envelope folds, `Audrey`, and `m:ss` duration remain legible.

## Traceability and consumers

| Source or consumer | Relationship |
|---|---|
| Product owner request, 2026-10-22 | Selects the sealed-envelope visual direction and requested card positions |
| [`COMP-MESSAGE-CARD`](COMP-MESSAGE-CARD.md) | Proposed no-sketch face variant; parent keeps playback and sketch behavior |
| [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md) | Owns message-card lifecycle and duration semantics |
| [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) | Owns sketch reveal and identity/meta hiding while ink is visible |
| Signed-in inbox carousel | Intended consumer; its component reference must be updated if this layout is adopted |

## OPEN items and readiness

| ID | Question | Consequence |
|---|---|---|
| `UI-OPEN-001` | Reconcile this requested envelope/name/duration arrangement with `EXP-MESSAGE-CAROUSEL` `DEC-046` / `EXP-043` and the current no-sketch arrangement in `COMP-MESSAGE-CARD`. | Blocks adoption by the inbox carousel. |
| `UI-OPEN-002` | Confirm the proposed 26 px avatar inside a 30 px white badge, and reconcile the sample `avatar-fa01` art with `FOUNDATION-PROFILE-AVATARS` and its 66 px inbox fallback. | Blocks production avatar treatment and adoption. |
| `UI-OPEN-003` | Align the 132 × 133 px requested outer height with `COMP-MESSAGE-CARD`'s current 132 × 134 px height and its centered 120 × 120 face. | Blocks parent-component integration; this component follows the requested 132 × 133 px size. |
| `UI-OPEN-004` | Approve or token-map the proposed envelope fills and line treatment in the active theme. | Blocks final visual approval. |
| `UI-OPEN-005` | Review native-size fit, long-name ellipsis, duration legibility, and RGB565 rendering on device. | Blocks visual approval. |

- **Owner review:** Ready to review as a concrete layout proposal.
- **Visual approval:** Pending `UI-OPEN-004` and `UI-OPEN-005`.
- **Implementation:** Not ready until `UI-OPEN-001`–`UI-OPEN-003` are resolved.
- **Verification:** Not performed; no firmware implementation was requested.

## Revision history

- **0.3 — 2026-10-22:** Combined the closed envelope and centered white circular field into one artwork asset; the sender avatar remains a separate overlay.
- **0.2 — 2026-10-22:** Specified a separate white circular backing so the renderer could layer the sender avatar above it.
- **0.1 — 2026-10-22:** Added the requested sealed-envelope no-sketch layout with fixed card geometry and metadata placements.
