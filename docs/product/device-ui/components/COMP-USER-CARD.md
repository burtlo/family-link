# User card component

ID: `COMP-USER-CARD`  
Type: Component  
Revision: 0.2  
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
│  card face (catalog background + gradient)       │
│           ┌──────────┐                         │
│           │ avatar   │  66 px circle            │
│           └──────────┘                         │
│              display name (fl-type-16)           │
└────────────────────────────────────────────────┘
        corner radius 8 px
```

| Region | Content | Rule |
|---|---|---|
| Face | Catalog **card background** | [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md) — user picks an allowed entry; not ad hoc hex |
| Name | Single line, centered | `fl-type-16`, weight **600**; **`fl-color-text-on-card`** from the same catalog entry (auto high-contrast) |
| Avatar | Portrait | **66 px** — `fl-avatar-roster`; **always present** (profile supplies default; avatar set catalog is a separate spec) |

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

- **2 px** outline `fl-color-accent`.

### Pressed

- Brief **opacity dip** on press (owner-decided).

## Roster size

Same layout for **4** or **8** users.

## Dependencies

- [`THEME-DARK`](../themes/THEME-DARK.md)
- [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md)
- [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md)

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Carousel snap animation after v1 performance sign-off |

## Revision history

- **0.2 — 2026-10-10:** Catalog backgrounds + on-card text; always avatar; ellipsis;
  pressed opacity; snap timing deferred.
- **0.1 — 2026-10-10:** Initial v1-derived geometry.
