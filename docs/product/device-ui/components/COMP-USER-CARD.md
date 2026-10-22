# User card component

ID: `COMP-USER-CARD`
Type: Component
Revision: 0.5
Status: Draft — geometry and states owner-aligned
Owner: Product owner
Approval: No visual approval claimed
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

Visual contract for one hangout user card in horizontal carousels (sign-in roster,
message inbox, send-user picker). Behavior stays in experience and screen specs.

## Anatomy

```text
┌──────────────────── 132 px ────────────────────┐
│  card face (catalog solid fill)                    │
│           ┌──────────┐                         │
│           │ avatar   │  66 px circle            │
│           └──────────┘                         │
│              display name (fl-type-16)           │
└────────────────────────────────────────────────┘
        corner radius 8 px
```

| Region | Content | Rule |
|---|---|---|
| Face | Catalog **solid fill** | [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md) — one of eight fixed fills; not ad hoc hex |
| Name | Single line, centered | `fl-type-16`, weight **600**; **`fl-color-text-on-card`** from the same catalog entry |
| Avatar | Identity circle | **66 px** — `fl-avatar-roster`; until the new bitmap catalog is ready, show the centered `initials_from_name` output from LAB-14 in a `#F4F4F4` circle with dark `fl-type-16` text; do not use V1 avatar art |

Long names: **ellipsize at end** (`...`). No adaptive font sizing (fixed three
sizes only per foundation).

Duplicate display names: show **as-is** (product does not require disambiguation).

## Geometry (roster / carousel)

| Token | Value |
|---|---|
| `fl-card-roster-width` | **132 px** |
| `fl-card-roster-height` | **133 px** (vertically centered in lower chrome) |
| `fl-card-roster-gap` | **12 px** |
| `fl-card-roster-radius` | **8 px** |

Touch: full card ≥ **`fl-touch-min-height`**.

## Visual states

### Default (not focused)

- Opacity ≈ **0.58**, scale ≈ **0.9** (targets; snap timing **OPEN** — see motion foundation).

### Focused (carousel center)

- Opacity **1.0**, scale **1.0**.

### Picked (send-user)

- **2 px** outline `fl-color-accent` (the global selection token, not the
  profile's `profile_accent`).

The card face uses `card_background_id`; core V2 user cards do not render the
profile's `profile_accent`.

### Pressed

- Brief **opacity dip** on press (owner-decided).

## Roster size

Same layout for **4** or **8** users.

## Dependencies

- [`THEME-DARK`](../themes/THEME-DARK.md)
- [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md)
- [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md)
- [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md)

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Carousel snap animation after v1 performance sign-off |

## Revision history

- **0.5 — 2026-10-22:** Recorded that card faces use `card_background_id`; `profile_accent` is not rendered on core V2 cards. Clarified that the selection outline uses the global accent token.
- **0.4 — 2026-10-22:** Adopted the eight solid catalog fills and LAB-14 initials circle as the interim portrait treatment; V2 avatar artwork remains deferred.
- **0.3 — 2026-10-22:** Linked roster avatar to profile avatar catalog foundation.
- **0.2 — 2026-10-10:** Catalog backgrounds + on-card text; always avatar; ellipsis;
  pressed opacity; snap timing deferred.
- **0.1 — 2026-10-10:** Initial v1-derived geometry.
