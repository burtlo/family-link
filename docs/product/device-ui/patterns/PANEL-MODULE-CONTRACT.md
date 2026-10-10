# Panel module contract (pattern)

Status: normative pattern for v2 split-pane screens. Not a single implementable
artifact.

## Purpose

A **panel module** is a swappable UI unit that mounts in **one slot** of a parent
layout (typically `upper` or `lower` on `LAYOUT-SPLIT-PANE`). Screens compose
**which panel** fills each slot; they do not embed one header component with many
modes.

This pattern separates:

1. **Shell** — split geometry, default background, slot chrome bounds, slot
   content insets (margins codified once).
2. **Panel module** — slot-local background override, chrome painting, and
   user-facing layout inside the inset.
3. **Components** — leaves inside a panel (digits, cards, icons).

## Three layers (composition)

```text
SCR-*  screen
└─ LAYOUT-SPLIT-PANE                    shell: split + default bg + insets
   ├─ upper slot → PANEL-* (one)       e.g. PANEL-HEADER-TITLE-LEFT
   └─ lower slot → PANEL-* (one)       e.g. PANEL-BODY-ROSTER-CAROUSEL
```

Stage changes **swap panel modules** in one or both slots (usually Level 2 for
that slot). The shell boundary at `y=80` stays fixed.

Prefer **several small panel types** over one panel with many modes. Shared
structure (for example a left title and right control) belongs in a **nested
layout** referenced by multiple panels, not in a single mega-component.

## Two rectangles per slot (core idea)

Each slot exposes two canvas-relative regions defined by the **parent shell**.
Panel modules MUST NOT re-apply `fl-margin-screen` unless a future layout
explicitly waives insets.

| Region | Name | Use |
|---|---|---|
| Full slot rectangle | **`slot_chrome_bounds`** | Panel background fill, full-bleed chrome, carousel peeks, scrubber track to slot edges when the panel spec allows |
| Inset rectangle | **`slot_content_bounds`** | Left-justified titles, status copy, PIN dots row, header controls aligned to the user-focused margin |

```text
upper slot_chrome (320 × 80)
┌────────────────────────────────────────┐
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│  chrome paints full slot
│▒  slot_content (296 × 56)            ▒│  text / header controls
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
└────────────────────────────────────────┘
```

**Rules:**

- **Background:** Panel paints its background on **`slot_chrome_bounds`** (or
  inherits the shell default). Upper and lower **MAY** use different colors.
- **User-focused elements:** Default placement inside **`slot_content_bounds`**.
- **Full-bleed within slot:** Allowed only when the **panel specification**
  lists which elements may extend into chrome margins (for example carousel
  cards edge-to-edge in the lower slot). Full-bleed never crosses into the
  sibling slot.
- **No per-panel margin tokens:** Insets come from the shell. Avoiding ad hoc
  `margin-left: 8` in components is a goal of this model.

Waiving content insets (screen-edge or slot-edge) requires a **new layout ID**,
not a silent panel exception.

## Background policy

| Layer | Background |
|---|---|
| Shell (`LAYOUT-SPLIT-PANE`) | **Default** `fl-color-surface` (token OPEN until foundation defines hex) |
| Panel module | **MAY override** solid fill for its slot only; often matches shell |
| Components | Transparent; do not own slot background |

Distinct upper/lower colors are allowed for visual grouping. This is not a
performance requirement either way.

## Panel specification requirements

Each `PANEL-<NAME>.md` (or equivalent section in a screen) MUST state:

1. **Slot:** `upper` or `lower` on `LAYOUT-SPLIT-PANE`.
2. **Chrome:** background color token or inherit shell default.
3. **Content:** what appears in `slot_content_bounds` (copy roles, controls).
4. **Full-bleed list:** elements that may use `slot_chrome_bounds` beyond the
   inset (if any).
5. **Touch:** targets ≥ `fl-touch-min-height`; scroll/gesture owner if lower
   body.
6. **Update level:** typical Level 0 vs Level 1 behavior; must not force full
   screen rebuild when sibling slot updates.

## Update and performance expectations

| Slot | Typical panel role | Update level |
|---|---|---|
| Upper header panels | Title, dots, play/record affordance | **Level 0** |
| Lower body panels | Carousel, PIN grid, lists | **Level 1** (identity-keyed) |

Lower-panel inbox or roster churn MUST NOT rebuild upper-panel widgets when the
screen snapshot unchanged. See
[UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md) and
[client coding standards](../../../standards/client-application-coding-standards.md)
§5.

## Example panel map (illustrative)

| Stage | Upper panel | Lower panel |
|---|---|---|
| Sign-in roster | `PANEL-HEADER-TITLE-LEFT` | `PANEL-BODY-ROSTER-CAROUSEL` |
| PIN entry | `PANEL-HEADER-PIN-STATUS` | `PANEL-BODY-PIN-GRID` |
| Message home | `PANEL-HEADER-MESSAGE-CHROME` | `PANEL-BODY-MESSAGE-CAROUSEL` |

Panel IDs are created as separate artifacts when authored; this table is not
approval of their geometry.

## Related artifacts

- Shell geometry and numeric insets:
  [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
- Margin token: [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
