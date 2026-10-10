# Sign-in roster screen

ID: `SCR-SIGN-IN-ROSTER`  
Type: Screen composition  
Revision: 0.3  
Status: Draft — composition and roster visuals owner-aligned; motion OPEN  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas  
Experience: [`EXP-LOGIN`](../../experiences/endpoint-login.md), sign-in roster stage

## Purpose and experience contract

This screen lets a signed-out person identify and select their user card after
the endpoint has obtained a non-empty hangout roster.

The linked experience specification owns:

- when this screen may appear;
- roster source, ordering, refresh, additions, and removals;
- swipe and tap behavior;
- selection, focus, and transition to PIN entry;
- connectivity, empty-hangout, roster refresh-wait, and stale-result behavior.

This document owns only the visible composition of the sign-in roster stage. It
must not be used to infer unresolved operational behavior.

## Sources and authority

| Source | Authority | Applicable decision |
|---|---|---|
| `EXP-LOGIN`, `DEC-004`, `EXP-006` | Owner-decided | Top third heading; bottom two thirds carousel |
| `EXP-007`, `EXP-036` | Owner-decided | Swipe/tap; center snap; updating-hangout wait UI |
| `EXP-009`, `EXP-019` | Owner-decided | Roster card additions/removals and focus fallback |
| `THEME-DARK` | Draft theme | Surface `#101418`; accent for pick mode elsewhere |
| `LAYOUT-SPLIT-PANE` | Draft layout | 80 / 160 split; chrome vs content insets |

## Composition tree

```text
SCR-SIGN-IN-ROSTER
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-LEFT
   └─ lower → PANEL-BODY-ROSTER-CAROUSEL
        └─ COMP-USER-CARD (per user)
```

See [`PANEL-HEADER-TITLE-LEFT`](../panels/PANEL-HEADER-TITLE-LEFT.md),
[`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md), and
[`COMP-USER-CARD`](../components/COMP-USER-CARD.md).

Both panes use **`fl-color-surface`** on sign-in (same upper and lower color).

## Wireframe

```text
(0,0)                                   (320,0)
┌────────────────────────────────────────┐
│   Sign in as ...                       │  upper — fl-type-24, weight 600
│                                        │
├────────────────────────────────────────┤
│                                        │
│ [dim]      [FOCUSED user card]    [dim] │
│          ← horizontal browse →         │  lower — 132×133 cards, 12 px gap
│                                        │
└────────────────────────────────────────┘
```

Non-focused cards use reduced scale and opacity per `COMP-USER-CARD`.

## Visual states

### Roster ready

- Heading: `Sign in as ...` (`PANEL-HEADER-TITLE-LEFT`).
- One `COMP-USER-CARD` per roster user; message-color gradient face, 66 px
  avatar, `fl-type-16` name.
- Carousel touch **enabled**.

### Roster refresh wait (`EXP-036`)

After return from PIN with `unknown user` while an immediate roster refresh is
in flight:

- Heading: `Updating hangout ...`.
- Carousel touch **disabled** (no swipe/tap).
- Cards may show stale data until the refresh response arrives.

When refresh completes with a non-empty roster:

- Heading returns to `Sign in as ...`.
- Carousel touch **enabled**.
- Reconcile cards to the new roster.

If refresh fails, the experience leaves this screen for connecting (`EXP-021`).

### Roster updated in place

- Add/remove cards by stable user ID.
- Focus fallback per `EXP-019`; snap focused card to viewport center (including
  first, last, and single-user cases).

### User selected

Focused card centers where geometry allows, then stage moves to PIN entry.
Transition **SHOULD** cross-fade per
[`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) once verified
(`UI-OPEN-001`).

### One user

Single card **centered** horizontally in the lower slot.

### First / last user focused

Center the focused card; outward side shows intentional empty track space.

### Empty roster

Use [`SCR-EMPTY-HANGOUT`](SCR-EMPTY-HANGOUT.md), not this screen.

### Connecting or server unavailable

Use the connecting screen; do not show selectable roster cards.

## Copy

| Element | Copy | When |
|---|---|---|
| Heading | `Sign in as ...` | Roster ready |
| Heading | `Updating hangout ...` | Roster refresh wait |
| Heading | *(not used)* | Empty → `SCR-EMPTY-HANGOUT` |

Heading: `fl-type-24`, weight **600**, `fl-color-text-primary`, left-aligned in
upper content bounds.

## Continuity and updates

- Heading copy changes are **Level 0**.
- Card add/remove/update is **Level 1** by user ID.
- Roster → PIN is **Level 2** stage replacement.
- Preserve split at `y=80`.

## Accessibility and hardware constraints

- Card touch targets meet `fl-touch-min-height`.
- Focus uses scale and opacity, not color alone.
- Roster supports up to **eight** users with the same geometry as four.

## Traceability

| Decision | Screen result |
|---|---|
| `DEC-004`, `EXP-006` | Split + heading + carousel |
| `EXP-007` | Focused card before PIN |
| `EXP-036` | Updating heading + touch lock |
| `EXP-019` | Focus fallback |
| `EXP-020` | Empty → separate screen |

## Dependencies

1. [`THEME-DARK`](../themes/THEME-DARK.md)
2. [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
3. [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) — cross-fade timing OPEN
4. Panel and component docs listed above

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Cross-fade duration/easing roster → PIN | Motion polish |
| `UI-OPEN-002` | Long names, missing avatars, duplicate names on card | Edge-case UX |

Resolved in 0.3: card geometry, avatar size, focus treatment, center snap,
theme surface, heading type size, updating-hangout presentation.

## Readiness

- **Owner review:** Ready for roster visual policy and refresh-wait UI.
- **Visual approval:** Pending motion and RGB565 gradient check.
- **Implementation:** Geometry and states are specified; motion timing is not.

## Revision history

- **0.3 — 2026-10-10:** Theme, panels, user card geometry, center snap, EXP-036
  wait state, cross-fade intent.
- **0.2 — 2026-10-10:** Shell chrome/content insets; planned `PANEL-*` per slot.
- **0.1 — 2026-10-10:** Initial screen composition.
