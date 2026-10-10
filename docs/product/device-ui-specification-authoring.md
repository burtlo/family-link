# Authoring device UI specifications

Status: reusable authoring workflow. This document does not approve a visual
design or authorize implementation.

## Purpose

Use this workflow to specify the fixed-hardware v2 device interface in a form
that product owners can review and implementation agents can follow without
inventing geometry, visual states, or composition.

Device UI specifications complement, but do not replace, experience
specifications:

- an **experience specification** defines user intent, stages, behavior, copy,
  failures, continuity, and acceptance scenarios;
- a **device UI specification** defines the canvas, visual tokens, layout
  regions, component anatomy, screen composition, and visual states;
- an **implementation plan** chooses framework APIs, ownership, memory strategy,
  and work packages.

When behavior or copy is unresolved, link the applicable experience item and
mark the presentation dependency `OPEN`. Do not settle product behavior in a
layout or component document.

## Design-system model

The v2 device UI uses five primary specification types plus one composition
pattern:

1. **Foundations** define the fixed canvas, typography constraints, spacing,
   keylines, input affordances, and non-color tokens.
2. **Themes** (`THEME-*`) define color and accent vocabulary (`fl-color-*`) for a
   named look (for example dark). Layouts and components reference theme tokens,
   not raw hex, unless measuring on hardware.
3. **Layouts** define reusable regions and named slots. A **shell layout** (for
   example split-pane) owns split geometry, default background, and codified
   slot content insets.
4. **Panel modules** (`PANEL-*`) follow
   [`PANEL-MODULE-CONTRACT`](device-ui/patterns/PANEL-MODULE-CONTRACT.md): one
   swappable unit per slot, chrome vs content bounds, optional background
   override. Prefer several panel types over one component with many modes.
5. **Components** define reusable visual elements inside a panel: anatomy,
   dimensions, variants, and visual states.
6. **Screens** compose shell + panel modules (+ nested layouts/components). A
   screen owns which `PANEL-*` mounts in each slot, not journey policy.

Composition is hierarchical:

```text
screen
└─ shell layout (e.g. LAYOUT-SPLIT-PANE)
   ├─ upper slot → PANEL-* (panel module)
   └─ lower slot → PANEL-* (panel module)
        └─ optional nested layout / COMP-*
```

**Chrome vs content:** the shell defines **`slot_chrome_bounds`** (full slot,
backgrounds, allowed full-bleed) and **`slot_content_bounds`** (inset for
user-focused text and aligned controls). Panel modules MUST NOT re-apply screen
margin unless a new layout ID waives insets.

Components are leaves. Full-canvas experiences use an explicit full-canvas
layout rather than bypassing specifications.

## Influences and adaptation to fixed hardware

This format borrows established design-system concepts without importing
phone-specific requirements:

- [Apple layout guidance](https://developer.apple.com/design/human-interface-guidelines/layout)
  uses layout guides to name rectangular alignment regions.
- [Apple Design Resources](https://developer.apple.com/design/resources/)
  separate visual libraries and device presentation resources from behavioral
  guidance.
- [Android grids and units](https://developer.android.com/design/ui/mobile/guides/layout-and-content/grids-and-units)
  recommend a consistent grid and explicit dimensions.
- [Android content structure](https://developer.android.com/design/ui/mobile/guides/layout-and-content/content-structure)
  distinguishes margins, columns, containment, and hierarchy.
- [Material layout](https://m3.material.io/foundations/layout/layout-overview/overview)
  builds screens from scaffolds, panes, and reusable canonical layouts.
- [Material spacing](https://m3.material.io/styles/spacing/overview) distinguishes
  padding, gaps, and margins and represents repeated choices as tokens.

V2 targets one hardware canvas. Device UI specifications therefore use native
pixels and one target profile rather than density-independent units,
breakpoints, safe areas, or adaptive phone/tablet classes. If the hardware
target changes, revise the foundation and affected specifications deliberately;
do not silently scale pixel values.

## Authority and sources

Consult sources in this order:

1. current product-owner instructions and recorded decisions;
2. approved experience specifications under `docs/product/experiences/`;
3. approved device UI foundations, layouts, components, and screens;
4. client engineering standards;
5. v1 assessments and the read-only v1 POC archive as historical evidence.

The v1 archive may be read for visual facts and examples, but its layout is not
v2 policy. Extract needed facts into active documents and do not add links or
path references to the archive.

Every specification records its status and source authority. A draft, historical
example, or implementation observation is not owner approval.

## Stable identifiers and paths

Use these identifiers:

- `FOUNDATION-<NAME>` for a shared visual foundation;
- `LAYOUT-<NAME>` for a reusable layout (shell or nested);
- `PANEL-<NAME>` for a swappable slot panel module;
- `COMP-<NAME>` for a reusable component;
- `SCR-<NAME>` for a screen composition;
- `UI-OPEN-###` for unresolved presentation decisions local to an artifact.

Use these paths:

```text
docs/product/device-ui/
  README.md
  patterns/PANEL-MODULE-CONTRACT.md
  foundations/<slug>.md
  themes/THEME-<NAME>.md
  layouts/LAYOUT-<NAME>.md
  panels/PANEL-<NAME>.md
  components/COMP-<NAME>.md
  screens/SCR-<NAME>.md
```

Preserve an identifier when its meaning remains stable. If meaning changes
materially, supersede it and record the replacement rather than silently
repurposing it.

## Shared metadata

Every device UI artifact begins with:

- title and stable ID;
- type: foundation, layout, component, or screen;
- revision, status, owner, and approval state;
- target hardware profile;
- applicable experience requirements and owner decisions;
- dependencies and dependents;
- revision history.

Use `MUST`, `SHOULD`, and `MAY` only for accepted requirements. Proposed values
use plain language and are labeled `PROPOSED`. Unresolved values are `OPEN`.

## Required structure by artifact type

### Layout

1. **Purpose and scope** — intended use and explicit non-use.
2. **Canvas and parent contract** — full canvas or named parent slot.
3. **Anatomy** — labeled wireframe and named slots.
4. **Geometry and keylines** — coordinates, dimensions, padding, gaps,
   alignment, clipping, and overflow.
5. **Slot contracts** — permitted content, ownership, and touch constraints.
6. **Nesting and variants** — allowed child layouts and bounded variation.
7. **Visual and update invariants** — what remains stable during property,
   subtree, and stage updates.
8. **Accessibility and hardware constraints**.
9. **Traceability** — source/decision/experience links and consuming screens.
10. **OPEN items and readiness**.

### Component

1. Purpose and use.
2. Anatomy diagram.
3. Dimensions and internal keylines.
4. Content contract and limits.
5. Visual states and variants.
6. Tokens and assets.
7. Interaction surface; behavioral links remain in experience specifications.
8. Accessibility and hardware constraints.
9. Traceability, OPEN items, and readiness.

### Screen

1. Stage, purpose, and experience links.
2. Composition tree.
3. Full-canvas wireframe at the target resolution.
4. Region-to-layout/component map.
5. Visual states, including empty, focused, disabled, waiting, and error states
   that are required by the linked experience.
6. Copy references; do not duplicate behavioral copy policy.
7. Continuity and update mapping using the shared UI update taxonomy.
8. Traceability from decisions and experience requirements through layouts and
   components.
9. Dependencies, OPEN items, and readiness.

### Foundation

1. Scope and target hardware facts.
2. Named tokens or keylines and their native-pixel values.
3. Rules for color, typography, spacing, assets, touch, and motion as
   applicable.
4. Constraints and exceptions.
5. Migration policy if a value changes.
6. Source authority, OPEN items, and readiness.

## Wireframes and measurements

Use text wireframes for fast review and include numeric keylines separately.
Wireframes communicate hierarchy; numeric tables are authoritative for
implementation.

Use the display origin at the upper-left:

```text
x increases →
y increases ↓
```

Specify whether each coordinate is relative to the canvas or a named parent
slot. Avoid ambiguous phrases such as “roughly centered” when the choice affects
implementation. If an exact value has not been decided, give the invariant and
mark the value `OPEN` rather than guessing.

Do not use screenshots as the sole specification. They may illustrate the
intended result, but agents also need named regions, dimensions, states, and
traceability.

## Authoring workflow

### 1. Classify and scope

Identify whether the request creates or revises a foundation, layout, component,
or screen. Prefer one coherent artifact; include dependencies only when needed
to make it reviewable.

### 2. Reconstruct

Summarize:

- the target stage or reusable visual purpose;
- the parent canvas or slot;
- known geometry and owner decisions;
- relevant experience requirements;
- inherited foundations and components;
- unknown visual choices and source conflicts.

Ask the owner to correct the reconstruction before treating it as settled.

### 3. Interview in small rounds

Ask two to four related questions at a time:

- anatomy and information hierarchy;
- keylines, dimensions, and overflow;
- visual states and variants;
- content limits and accessibility;
- allowed variation versus locked appearance.

Do not ask the owner to choose implementation APIs. Recommendations remain
`PROPOSED` until accepted.

### 4. Draft and trace

Draft independent sections while decisions are pending. Give unresolved choices
stable `UI-OPEN-###` identifiers. Link every screen region to a layout,
component, foundation, or explicitly temporary inline composition.

For screen artifacts, trace:

```text
owner decision → experience requirement → screen → layout/component
```

Do not duplicate the full behavioral contract.

### 5. Challenge

Review at least:

- minimum and maximum expected content;
- long and short text;
- focused, disabled, empty, waiting, and failure appearances when applicable;
- clipping and overflow;
- touch-target reachability;
- RGB565 or asset constraints;
- whether two implementers could produce materially different visible results.

Resolve unintended differences or mark them `OPEN`.

### 6. Owner review and readiness

Summarize:

- locked presentation;
- bounded variation;
- delegated implementation choices;
- blocking and non-blocking `OPEN` items.

State separately whether the artifact is:

- ready for owner review;
- visually approved;
- ready for implementation;
- implemented and verified.

No earlier state implies a later one.

## Review criteria

Rate each artifact `CLEAR`, `INCOMPLETE`, `CONFLICTING`, or `OPEN` for:

1. **Concrete** — anatomy and hierarchy are visible.
2. **Measured** — implementation-significant geometry is numeric.
3. **Composable** — parent slots and child ownership are unambiguous.
4. **Consistent** — tokens, keylines, terminology, and states agree.
5. **Traceable** — visual choices link to owner and experience authority.
6. **Bounded** — content limits, clipping, and variants are defined.
7. **Accessible** — touch, contrast, legibility, and nonvisual needs are covered.
8. **Implementable** — agents need not invent visible product policy.
9. **Testable** — the visible result can be compared with the specification.
10. **Proportionate** — the artifact does not duplicate behavioral or technical
    design documents.

Explain each non-`CLEAR` rating with the affected region and the decision or
evidence needed.

## Hard boundaries

- Do not implement firmware while authoring a device UI specification.
- Do not turn an implementation accident into a product requirement.
- Do not choose unresolved behavior, copy, or failure policy in a visual spec.
- Do not link active documents to paths in the v1 POC archive.
- Do not claim owner approval from silence or from completion of a draft.
- Do not add responsive breakpoints for hypothetical hardware.

## Handoff

An implementation task should name the exact `SCR-`, `LAYOUT-`, `COMP-`, and
experience requirement IDs in scope. If implementation evidence requires a
visible change, return that choice to the owner and revise the applicable
specification rather than silently diverging.
