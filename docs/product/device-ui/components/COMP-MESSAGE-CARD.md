# Message card component

ID: `COMP-MESSAGE-CARD`  
Type: Component  
Revision: 0.3  
Status: Draft — geometry, content, sketch states, and card palette owner-aligned; final visual fit pending  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

One incoming message card in the signed-in inbox carousel. The linked
experience owns selection, playback, deletion, connectivity, and sketch timing.
This component owns only the card face and its visual states.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md) `DEC-044`, `DEC-046`, `EXP-041`, `EXP-043` | Owner-decided | Hide card portrait and metadata while sketch ink is visible; no-sketch identity and duration; no unread dot |
| [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) `SK-002`, `SK-003`, `SK-009` | Owner-decided | 120 × 120 sketch at 1:1 inside approximately 132 × 134 outer card; metadata hidden while sketch is visible |
| [`COMP-USER-CARD`](COMP-USER-CARD.md) | Existing visual component | Reuse 66 px portrait and one-line name treatment |
| [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md) | Owner-selected catalog | Sender's current profile card background from the eight solid fills and paired `#101418` text |
| [`COM-USER-PROFILE`](../../communication-contracts/user-profile.md) `COM-005` | Owner-decided | Core V2 inbox cards use `card_background_id`; they do not render `profile_accent` |
| [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md) | Owner-approved interim fallback | LAB-14 initials-in-circle until new avatar artwork is ready; V1 avatar art is not carried forward |
| Carousel assessment | Engineering evidence | Identity-keyed card updates; do not rebuild portrait or card subtree during scroll |

## Anatomy and geometry

The card uses **132 × 134 px** outer chrome. Its centered face is **120 × 120
px**, at a 6 px horizontal and 7 px vertical inset. The face contains either
the no-sketch identity presentation or the 120 × 120 sketch.

```text
outer card: 132 × 134 px
┌────────────────────────────────┐
│      ┌────────────────────┐    │  6 px left/right
│      │                    │    │
│      │  portrait + meta   │    │  120 × 120 face
│      │  or message sketch │    │
│      │                    │    │
│      └────────────────────┘    │  7 px top/bottom
└────────────────────────────────┘
```

### No sketch visible

- Center the sender portrait in the face. Reuse the **66 px** circular
  `fl-avatar-roster` treatment from `COMP-USER-CARD`.
- Beneath the portrait, show the sender display name on one centered line,
  followed by duration on a centered line in **`m:ss`** format, for example
  `0:08` or `2:14`.
- Use `fl-type-16` and the card's paired `fl-color-text-on-card` for both
  lines. Ellipsize a long sender name at the end. Do not shrink the type.
- Center the portrait and two text lines as a group in the 120 px face. A
  4 px vertical gap between portrait/name and name/duration is the current
  layout proposal; verify legibility in the visual review.
- Do not show an unread dot or other read-state marker. The experience does
  not track unread state.

### Sketch visible

- Draw the logical sketch at **120 × 120 px**, 1:1 and centered in the face.
- Hide the portrait, sender name, and duration for as long as sketch ink is
  visible. The sketch is the card face while playing, paused, or completed in
  the current signed-in session.
- Audio-only messages have no sketch; keep the no-sketch identity and
  duration presentation while their audio plays.
- Mute clears this session's sketch progress. After the next login, the card
  shows its no-sketch identity and duration state until playback draws ink.

## Carousel appearance

| State | Presentation |
|---|---|
| Centered card | Opacity 1.0, scale 1.0 |
| Partial side peek | Dimmed, reduced scale; use `COMP-USER-CARD` non-focused targets (about 0.58 opacity, 0.9 scale) pending device visual review |
| Playing sketch | Sketch is visible; portrait and metadata are hidden |
| Paused or complete sketch | Current partial or full sketch remains visible; portrait and metadata stay hidden |
| Audio-only | Portrait, sender name, and duration remain visible; no progress mark is added to the card |

The carousel panel owns clipping, snapping, and collection changes. This
component must not own swipe or tap behavior.

## Color and contrast

Use the sender's current server-owned `card_background_id` and matching
`fl-color-text-on-card` from the eight-entry
[`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md)
catalog. Do not render `profile_accent` on this core V2 card or invent other
per-user colors. Avatar artwork remains deferred; use the
approved initials fallback from `FOUNDATION-PROFILE-AVATARS` meanwhile.

## Update boundaries

- Sender profile changes update the existing card by stable sender identity.
- Sketch frames update only the sketch face. They must not rebuild the card,
  sender portrait, or adjacent carousel cards.
- Playback timing is owned by audio and sketch services, not a card timer.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-22)* Use the eight owner-selected solid fills and paired `#101418` text in `FOUNDATION-CARD-BACKGROUNDS` | — |
| `UI-OPEN-002` | Verify the proposed 4 px metadata gaps, portrait size, and text fit on target hardware | Final visual approval; implementation may use current geometry proposal for a draft |

## Dependencies

- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md)
- [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md)
- [`COMP-USER-CARD`](COMP-USER-CARD.md)
- [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md)
- [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md)

## Revision history

- **0.3 — 2026-10-22:** Recorded that core V2 message cards use `card_background_id` and do not render `profile_accent`.
- **0.2 — 2026-10-22:** Linked the owner-selected card-background catalog and interim LAB-14 initials fallback. Final visual fit remains open.
- **0.1 — 2026-10-21:** Owner decisions recorded for no-sketch metadata, duration format, no unread dot, and sketch replacement; geometry and spacing are a visual draft.
