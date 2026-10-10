# Split-pane layout

ID: `LAYOUT-SPLIT-PANE`  
Type: Layout (shell)  
Revision: 0.3  
Status: Draft — shell geometry and inset model owner-aligned; theme-linked colors  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose and scope

`LAYOUT-SPLIT-PANE` is the **shell** for the common ⅓ / ⅔ device layout. It
defines:

- fixed slot geometry on the full canvas;
- a **default parent background**;
- per-slot **`slot_chrome_bounds`** and **`slot_content_bounds`** (margins
  codified here so panel modules do not re-apply `fl-margin-screen`);
- mount points for **one swappable panel module per slot**.

It does not define specific header or body UIs. Those are separate
**`PANEL-*`** modules following
[`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md).

Do not use this shell for full-canvas experiences (drawing, connecting hero, and
similar). Use an explicit full-canvas layout instead.

## Composition model

```text
SCR-*
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-*   (swappable)
   └─ lower → PANEL-*   (swappable)
```

Screens declare which panel module mounts in each slot. Prefer multiple panel
types over one header with many modes.

## Sources and authority

| Source | Authority | Contribution |
|---|---|---|
| Product owner | Owner decision | ⅓ / ⅔ split; composable panels; parent default bg + per-panel override; margins on shell |
| `EXP-LOGIN`, `DEC-004`, `EXP-006` | Experience draft | Sign-in heading above user-card region |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Foundation | `fl-margin-screen` = 12 px |
| [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md) | Pattern | Chrome vs content inset rules |

## Shell background

| Item | Rule |
|---|---|
| Default | Shell paints **`fl-color-surface`** (`#101418` in [`THEME-DARK`](../themes/THEME-DARK.md)) across the full canvas |
| Panel override | Each mounted panel **MAY** paint **`fl-color-surface-upper`** or **`fl-color-surface-lower`** on its **`slot_chrome_bounds`** |
| Same vs different | Sign-in family uses the **same** surface in both slots; message home **MAY** use two pane colors |

Background choice does not drive performance requirements. Scoped invalidation
and update level drive performance (see panel contract).

## Canvas and slot chrome

The shell occupies the complete framebuffer. Slots are **edge-to-edge** on the
canvas (full width; split at `y=80`).

```text
(0,0)                              (320,0)
┌────────────────────────────────────────┐
│         upper slot_chrome  80 px       │
├────────────────────────────────────────┤  y = 80
│         lower slot_chrome 160 px       │
└────────────────────────────────────────┘
(0,240)                            (320,240)
```

| Region | `slot_chrome_bounds` | Size |
|---|---|---|
| `upper` | `x=[0,320)`, `y=[0,80)` | 320 × 80 |
| `lower` | `x=[0,320)`, `y=[80,240)` | 320 × 160 |
| Split keyline | `y=80` | No required visible divider |

Panel backgrounds and allowed full-bleed chrome paint within these bounds.

## Slot content insets (margins codified)

Inset uses **`fl-margin-screen` (12 px)** from the foundation. The union of
upper and lower **content** rectangles equals the global
**`fl-content-well`** (296 × 216).

| Slot | `slot_content_bounds` | Size |
|---|---|---|
| `upper` | `x=[12,308)`, `y=[12,68)` | 296 × 56 |
| `lower` | `x=[12,308)`, `y=[92,228)` | 296 × 136 |

```text
        12 px                          12 px
          ↓                              ↓
    ┌─────┬────────────────────────┬─────┐
    │     │ upper content          │     │  y 12–68
    ├─────┴────────────────────────┴─────┤  y 80  (12 px band below upper content)
    │     │ lower content          │     │  y 92–228
    └─────┴────────────────────────┴─────┘
```

**Rules for panel authors:**

- Place **user-focused** text and header controls (titles, dots, play button)
  inside **`slot_content_bounds`** unless the panel spec lists a full-bleed
  exception.
- Paint **panel background** on full **`slot_chrome_bounds`**.
- **Lower-body** panels (carousel, grids) **MAY** use full slot width for cards
  and peeks inside **`slot_chrome_bounds`** when documented in the panel spec;
  they MUST NOT draw outside the slot or assume extra margin beyond this shell.
- Panel modules **MUST NOT** apply their own screen margin; the shell owns insets.

**Waiving insets** (edge-to-edge content on the full canvas, or different margin
geometry) requires a **new layout ID**, not a one-off panel flag.

## Slot contracts

### `upper`

- Hosts exactly **one** header-class **`PANEL-*`** (title left, PIN status, message
  chrome, and similar).
- Not the primary scroll region.
- Typical updates: **Level 0** (copy, dots, icons).

### `lower`

- Hosts exactly **one** body-class **`PANEL-*`** (roster carousel, PIN grid,
  message carousel, and similar).
- Owns primary touch, scroll, and **Level 1** collection updates within the slot.
- Must clip to **`slot_chrome_bounds`**.

## Visual and update invariants

- `y=80` and content inset keylines remain stable while this shell is active.
- Updating the lower panel MUST NOT require rebuilding the upper panel subtree
  when the screen snapshot for the upper slot is unchanged (and vice versa),
  except on intentional panel module swap (Level 2 for that slot).
- Follow [UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md).

## Accessibility and hardware constraints

- Touch targets ≥ **`fl-touch-min-height`** (foundation); remain inside the
  owning slot.
- Typography: **`fl-type-16`**, **`fl-type-24`**, **`fl-type-32`** only
  ([`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)).
- Upper content height is **56 px**; panels must define truncation or line count
  for titles (typically one line at `fl-type-24`).

## Traceability and consumers

| Decision | Shell behavior |
|---|---|
| ⅓ / ⅔ split | 80 px / 160 px chrome regions |
| Composable panels | One `PANEL-*` per slot |
| Margins in parent | `slot_content_bounds` per table above |
| `DEC-004`, `EXP-006` | Sign-in screen composes header + roster panels |

Consumers:

- [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md)

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | *(Superseded 2026-10-10)* Surface tokens — see [`THEME-DARK`](../themes/THEME-DARK.md) | — |
| `UI-OPEN-002` | Stage transition motion (cross-fade proposed) | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |

Resolved in 0.2: distinct pane backgrounds allowed; margins codified on shell;
no per-panel margin exceptions without new layout.

## Readiness

- **Owner review:** Shell model and inset table ready.
- **Visual approval:** Pending motion verification; colors in `THEME-DARK`.
- **Implementation:** Shell and inset math are specified; panel modules are not.

## Revision history

- **0.3 — 2026-10-10:** Link `THEME-DARK` surface tokens; sign-in same-color panes.
- **0.2 — 2026-10-10:** Three-layer shell: default bg, per-panel override,
  `slot_chrome_bounds` vs `slot_content_bounds`, panel module composition.
- **0.1 — 2026-10-10:** Initial 80/160 split.
