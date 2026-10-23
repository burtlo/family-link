# Card background catalog

ID: `FOUNDATION-CARD-BACKGROUNDS`
Type: Foundation
Revision: 0.4
Status: Draft — eight flat fills owner-selected; visual approval pending
Owner: Product owner

## Purpose

Defines the fixed card-background values used by roster, recipient, and inbox
cards. The card background is a server-owned profile field
(`card_background_id`); the device resolves it through this catalog rather than
using free-form user colors.

## Owner decisions

- On 2026-10-22, the owner adopted the eight flat colors already used by the
  LAB-14 roster fixtures as the V2 card-background palette.
- Each entry uses the fixture's paired dark text color, `#101418`, for
  `fl-color-text-on-card`.
- Profile editing remains outside the current implementation scope. These
  values still apply when rendering a user's current profile on a card.
- Profile avatars are a separate catalog; until its artwork is available,
  identity surfaces use the LAB-14 initials-in-circle fallback described in
  [`FOUNDATION-PROFILE-AVATARS`](FOUNDATION-PROFILE-AVATARS.md).

## Catalog

All entries are solid fills. Do not add a gradient or alter these colors
without a catalog revision.

| ID | Display name | Fill | `fl-color-text-on-card` |
|---|---|---|---|
| `card-bg-01` | Blue | `#5AA0E8` | `#101418` |
| `card-bg-02` | Green | `#67C587` | `#101418` |
| `card-bg-03` | Gold | `#E8C040` | `#101418` |
| `card-bg-04` | Pink | `#D875A8` | `#101418` |
| `card-bg-05` | Red | `#E85B5B` | `#101418` |
| `card-bg-06` | Purple | `#8B7CE8` | `#101418` |
| `card-bg-07` | Orange | `#E88840` | `#101418` |
| `card-bg-08` | Teal | `#4EC4C8` | `#101418` |

`card_background_id` values in the proposed profile contract use these IDs.
A missing or unrecognized id displays as `card-bg-01` and does not hide the
person or message ([`COM-USER-PROFILE`](../../communication-contracts/user-profile.md)
`COM-010`). V1 color migration remains `COM-OPEN-003`.

## Rendering rules

- Roster and message cards use the current profile's catalog entry; they do not
  freeze a background into each sent message.
- A missing or unrecognized `card_background_id` uses `card-bg-01`. The card
  stays visible.
- The paired text color is used for display names and message-card metadata.
- RGB565 output follows [`THEME-DARK`](../themes/THEME-DARK.md) quantization.
- The selected background does not recolor avatar bitmap art. The temporary
  initials fallback uses its LAB-14 circle treatment.

## Dependencies

- [`THEME-DARK`](../themes/THEME-DARK.md)
- [`COMP-USER-CARD`](../components/COMP-USER-CARD.md)
- [`COMP-MESSAGE-CARD`](../components/COMP-MESSAGE-CARD.md)
- [`COM-USER-PROFILE`](../../communication-contracts/user-profile.md)
- LAB-14 fixture colors in `catalog/scenes/sign-in-roster-eight.yaml`

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Review the eight fills and text pairing on target hardware | Final visual approval |

## Revision history

- **0.4 — 2026-10-22:** A missing or unrecognized id displays as `card-bg-01`.
- **0.3 — 2026-10-22:** Owner adopted the eight LAB-14 fixture colors as solid
  V2 card fills with `#101418` text.
- **0.2 — 2026-10-21:** Recorded that the chosen catalog entry is server-owned,
  not endpoint-local.
- **0.1 — 2026-10-10:** Placeholder for owner-directed catalog and paired
  text colors.
