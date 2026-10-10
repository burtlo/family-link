# V2 device UI specifications

Status: active index; individual artifacts carry their own approval status.

This directory defines the reusable visual language for the fixed-hardware v2
device interface. It is written for product-owner review and for agents
designing, implementing, or verifying the device UI.

Follow [Authoring device UI specifications](../device-ui-specification-authoring.md)
when creating or revising an artifact.

## What belongs here

| Artifact | Defines | Does not define |
|---|---|---|
| Foundation | Canvas facts, visual tokens, keylines, touch and asset constraints | Experience behavior |
| Layout | Shell geometry, slots, chrome/content insets, default background | Meaning of screen stages |
| Panel module (`PANEL-*`) | Swappable slot UI; chrome paint vs content inset | Behavior and copy |
| Component | Visual anatomy inside a panel | Journey or operation policy |
| Screen | Shell + which `PANEL-*` per slot | Retry, timeout, data, or side-effect contracts |

Behavioral authority remains in
[`docs/product/experiences/`](../experiences/). Engineering constraints remain
in [`docs/standards/`](../../standards/client-application-coding-standards.md).

## Target profile

The current v2 UI target is one landscape device canvas:

- display: 320 × 240 native pixels;
- coordinate origin: upper-left;
- touch: direct capacitive interaction;
- color rendering: RGB565;
- system chrome: none inside the framebuffer;
- physical controls and bezel features are outside the 320 × 240 canvas.

These values reflect the product owner's decision to build v2 only for the
current hardware. A future hardware change requires an explicit foundation and
impact review; specifications must not silently scale.

Shared margin, touch, slider, and typography tokens live in
[`FOUNDATION-DEVICE-CANVAS`](foundations/FOUNDATION-DEVICE-CANVAS.md). Default
colors live in [`THEME-DARK`](themes/THEME-DARK.md). Stage motion intent is in
[`FOUNDATION-MOTION`](foundations/FOUNDATION-MOTION.md) (timing OPEN).

## Composition model

```text
SCR-* screen
└─ LAYOUT-SPLIT-PANE (shell)
   ├─ upper → PANEL-*
   └─ lower → PANEL-*
```

See [`patterns/PANEL-MODULE-CONTRACT.md`](patterns/PANEL-MODULE-CONTRACT.md).
Shell layouts codify **`slot_chrome_bounds`** and **`slot_content_bounds`** so
panels do not re-apply margins. Waiving insets requires a new layout ID.
Drawing and other edge-to-edge experiences use an explicit full-canvas layout.

## Artifact index

### Foundations

| ID | Document | Status |
|---|---|---|
| `FOUNDATION-DEVICE-CANVAS` | [Device canvas](foundations/FOUNDATION-DEVICE-CANVAS.md) | Draft — margin, touch, Montserrat **16 / 24 / 32** |
| `FOUNDATION-MOTION` | [Motion](foundations/FOUNDATION-MOTION.md) | Draft — cross-fade proposed; timing OPEN |
| `THEME-DARK` | [Dark theme](themes/THEME-DARK.md) | Draft — surface `#101418`, accent, per-user message color |

### Layouts

| ID | Document | Status |
|---|---|---|
| `LAYOUT-SPLIT-PANE` | [Split pane shell](layouts/LAYOUT-SPLIT-PANE.md) | Draft — chrome/content insets, theme-linked surfaces |

### Patterns

| Document | Role |
|---|---|
| [Panel module contract](patterns/PANEL-MODULE-CONTRACT.md) | Chrome vs content inset; swappable `PANEL-*`; background override |

Planned examples include a horizontal carousel layout, PIN pad grid, and
full-canvas drawing layout. Planned entries are not requirements until authored
and reviewed.

### Panel modules

| ID | Document | Slot |
|---|---|---|
| `PANEL-HEADER-TITLE-LEFT` | [Header title left](panels/PANEL-HEADER-TITLE-LEFT.md) | `upper` |
| `PANEL-BODY-ROSTER-CAROUSEL` | [Roster carousel](panels/PANEL-BODY-ROSTER-CAROUSEL.md) | `lower` |

### Components

| ID | Document | Role |
|---|---|---|
| `COMP-USER-CARD` | [User card](components/COMP-USER-CARD.md) | Roster/inbox card geometry and states |
| `COMP-PIN-KEYPAD` | [PIN keypad](components/COMP-PIN-KEYPAD.md) | 3×3 grid for digits 1–9; v1 h03 key sizing and spacing |

### Screens

| ID | Document | Experience | Status |
|---|---|---|---|
| `SCR-SIGN-IN-ROSTER` | [Sign-in roster](screens/SCR-SIGN-IN-ROSTER.md) | `EXP-LOGIN` | Draft — roster visuals owner-aligned |
| `SCR-EMPTY-HANGOUT` | [Empty hangout](screens/SCR-EMPTY-HANGOUT.md) | `EXP-LOGIN` | Draft — fixed empty copy |
| `SCR-PIN-ENTRY` | [PIN entry](screens/SCR-PIN-ENTRY.md) | `EXP-LOGIN` | Draft; masked progress, error placement, keypad setup decided; some layout/states OPEN |

## Authority and readiness

Each artifact distinguishes:

- owner-decided behavior inherited from an experience specification;
- owner-decided visual presentation;
- proposed presentation;
- unresolved visual choices;
- implementation and verification evidence.

An artifact listed here is not automatically approved or implementation-ready.
Read its status, dependencies, and `UI-OPEN` items.

## Agent workflow

Use the project skill:

```text
/author-device-ui-specification

[layout, component, or screen description]
```

The skill is mirrored under `.cursor/skills/` and `.agents/skills/`. It authors
specifications only; firmware implementation is a separate task.

## Naming

- foundations: `FOUNDATION-<NAME>`;
- layouts: `LAYOUT-<NAME>`;
- components: `COMP-<NAME>`;
- screens: `SCR-<NAME>`;
- unresolved presentation decisions: `UI-OPEN-###` local to each artifact.

Use stable identifiers in experience traceability and implementation work
packages.
