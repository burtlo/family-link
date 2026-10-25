# Sign-in roster screen

ID: `SCR-SIGN-IN-ROSTER`
Type: Screen composition
Revision: 0.6
Status: Draft — composition, card palette, and interim initials fallback owner-aligned; motion OPEN
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
- One `COMP-USER-CARD` per roster user; solid catalog fill, 66 px identity
  circle with LAB-14 initials until new avatar art is ready, and `fl-type-16`
  name.
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
Transition to PIN uses forward **cover-down** per
[`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md).

### One user

Single card **centered** horizontally in the lower slot.

### First / last user focused

Center the focused card; outward side shows intentional empty track space.

### Empty roster

Use [`SCR-EMPTY-HANGOUT`](SCR-EMPTY-HANGOUT.md), not this screen.

### Connecting or Wi-Fi unavailable

Keep the split layout and [`PANEL-HEADER-TITLE-LEFT`](../panels/PANEL-HEADER-TITLE-LEFT.md)
in the upper slot. Swap the lower slot to
[`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md): `connecting` with
three dots when Wi-Fi is up and the server is not ready, or `no Wi-Fi` when
Wi-Fi is down. Do not dim the upper panel. Restore this carousel when the
condition clears.

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
3. [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md)
4. Panel and component docs listed above
5. [`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) in the lower slot while connecting or Wi-Fi is unavailable

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_LOGIN` (signed out). Shared with `SCR-EMPTY-HANGOUT`, `SCR-PIN-ENTRY`, `SCR-PIN-COOLDOWN`; the login session stage picks the panel. |
| Owner | `product_login_bridge.cpp` → `render_login_copy()`, called only from `product_login_present(1)`. |
| Visible surfaces | Upper: heading label. Lower: roster row of cards (`s_roster_row`). Status label hidden. PIN pad and cooldown hidden. |
| Input | Card tap → `select_pin_user` (intent only). Horizontal drag scrolls the row; a tap that moved the carousel is ignored. Ignored while `roster_updating`. |
| Enters when | Signed out, not muted, Wi-Fi up, stage `Roster`, `user_count > 0`, PIN phase idle. |
| Leaves when | Card selected → PIN entry (cards deleted in the same render). Mute latched → display blank. Roster empties → empty hangout. Wi-Fi lost → status panel. |
| Background work | Login HTTP roster refresh on the worker. Inbox results are discarded while signed out. |
| Update levels | Level 1: heading text change-detected. Level 2: roster row ↔ status label in one render. Cards are rebuilt only when the roster content changes (`roster_cards_current`). |
| Never | Roster visible with any inbox, volume, send, or record widget. Cards rebuilt every tick. A pointer reset outside display wake. |
| Open gaps | Cover-down to PIN is not implemented (instant swap). |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Carousel snap animation (after v1 performance sign-off) | Roster feel |

Resolved in 0.3+: card geometry, ellipsis, catalog backgrounds, cover-down to PIN,
updating-hangout UI.

## Readiness

- **Owner review:** Ready for roster visual policy and refresh-wait UI.
- **Visual approval:** Pending motion and target-hardware review of the solid
  card fills and text pairing.
- **Implementation:** Geometry and states are specified; motion timing is not.

## Revision history

- **0.6 — 2026-10-22:** Adopted the eight solid card fills and LAB-14 initials-in-circle fallback; avatar art remains deferred.
- **0.5 — 2026-10-21:** Connecting and Wi-Fi replace the lower slot; no overlay card.
- **0.4 — 2026-10-20:** Connecting and Wi-Fi stay on this screen under `COMP-STATUS-MESSAGE`.
- **0.3 — 2026-10-10:** Theme, panels, user card geometry, center snap, EXP-036
  wait state, cross-fade intent.
- **0.2 — 2026-10-10:** Shell chrome/content insets; planned `PANEL-*` per slot.
- **0.1 — 2026-10-10:** Initial screen composition.
