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
[`FOUNDATION-MOTION`](foundations/FOUNDATION-MOTION.md); stage changes use
[`TRANSITION-COVER-DOWN`](patterns/TRANSITION-COVER-DOWN.md) (h19).

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

### Remaining x02 coverage

The v1 x02 client is the historical stage inventory for the v2 migration. These
handoff files name the visual artifacts still to author; they are planning
inventories, not approved specifications:

| Directory | Missing-artifact inventory | Main coverage |
|---|---|---|
| Components | [MISSING](components/MISSING.md) | Reusable status/error message, message card, transport, settings and recipient tiles, record canvas and swatches |
| Layouts | [MISSING](layouts/MISSING.md) | Full-canvas, carousel-stage, record canvas/rail, receipt geometry |
| Panels | [MISSING](panels/MISSING.md) | Status-message composition, signed-in carousel, picker, settings, color rail, send feedback |
| Screens | [MISSING](screens/MISSING.md) | Connecting, Wi-Fi unavailable, inbox, settings, recipient picker, record, send |

The active [login experience](../experiences/endpoint-login.md) and [sketch
experience](../experiences/sketch-record-and-playback.md) govern the behavior and
geometry they already decide. The signed-in and send stages need further
experience work before their visual artifacts can be implementation-ready.

### Foundations

| ID | Document | Status |
|---|---|---|
| `FOUNDATION-DEVICE-CANVAS` | [Device canvas](foundations/FOUNDATION-DEVICE-CANVAS.md) | Draft — margin, touch, Montserrat **16 / 24 / 32** |
| `FOUNDATION-BUTTON-MINIMUMS` | [Button minimums](foundations/FOUNDATION-BUTTON-MINIMUMS.md) | Draft — PIN grid **101 × 48** suggested minimum |
| `FOUNDATION-MOTION` | [Motion](foundations/FOUNDATION-MOTION.md) | Draft — cross-fade proposed; timing OPEN |
| `THEME-DARK` | [Dark theme](themes/THEME-DARK.md) | Draft — token placement map (single scheme) |
| `FOUNDATION-CARD-BACKGROUNDS` | [Card backgrounds](foundations/FOUNDATION-CARD-BACKGROUNDS.md) | Placeholder — catalog TBD |
| `FOUNDATION-LVGL-BUDGET` | [LVGL budget & assessments](foundations/FOUNDATION-LVGL-BUDGET.md) | Draft — two-budget model, measurement table, review template |

### Assessments

| Document | Role |
|---|---|
| [LVGL screen assessment template](assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md) | Copy per screen/demo; file as `ASSESS-*.md` in same folder |
| [Demo explorer index](assessments/DEMO-EXPLORER-INDEX.md) | **Rev 0.1** — 51 island demos by theme; PASS lines; V2 flash shortlist |
| [Demo pattern ledger](assessments/DEMO-PATTERN-LEDGER.md) | **Rev 0.2** — 51 island demos + x02 modules (SCAN + 12 DEEP); verdict summary |
| **V2 flash shortlist (static `ASSESS-*`)** | [h19](assessments/ASSESS-DEMO-h19.md) · [h03](assessments/ASSESS-DEMO-h03.md) · [h18](assessments/ASSESS-DEMO-h18.md) · [h14](assessments/ASSESS-DEMO-h14.md) · [h02](assessments/ASSESS-DEMO-h02.md) · [p04](assessments/ASSESS-DEMO-p04.md) · [p13](assessments/ASSESS-DEMO-p13.md) · [p10](assessments/ASSESS-DEMO-p10.md) · [x02 shell](assessments/ASSESS-SHELL-x02.md) |
| **Sketch / media tier (static `ASSESS-*`)** | [h27](assessments/ASSESS-DEMO-h27.md) · [h26](assessments/ASSESS-DEMO-h26.md) · [h28](assessments/ASSESS-DEMO-h28.md) · [h13](assessments/ASSESS-DEMO-h13.md) · [p12](assessments/ASSESS-DEMO-p12.md) · [h23](assessments/ASSESS-DEMO-h23.md) (record policy; no LVGL) |
| **[`ASSESS-MODULE-v1-carousel`](assessments/ASSESS-MODULE-v1-carousel.md)** | Deep v1 inbox carousel / playback / sketch analysis for v2 component reuse |
| **On-device** | Fill **Measured** columns per [`FOUNDATION-LVGL-BUDGET`](foundations/FOUNDATION-LVGL-BUDGET.md) appendix A |

### Layouts

| ID | Document | Status |
|---|---|---|
| `LAYOUT-SPLIT-PANE` | [Split pane shell](layouts/LAYOUT-SPLIT-PANE.md) | Draft — chrome/content insets, theme-linked surfaces |

### Patterns

| Document | Role |
|---|---|
| [Panel module contract](patterns/PANEL-MODULE-CONTRACT.md) | Chrome vs content inset; swappable `PANEL-*`; background override |
| [Cover-down transition](patterns/TRANSITION-COVER-DOWN.md) | h19 stage motion (400 ms, ease-in-out quint) |

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
| `SCR-PIN-ENTRY` | [PIN entry](screens/SCR-PIN-ENTRY.md) | `EXP-LOGIN` | Draft — v1 layout, cover-down; `checking...` OPEN |
| `SCR-PIN-COOLDOWN` | [PIN cooldown](screens/SCR-PIN-COOLDOWN.md) | `EXP-LOGIN` | Draft — full-screen countdown |

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
