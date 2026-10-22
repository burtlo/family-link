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

SCR-RECORD-SKETCH
└─ LAYOUT-SPLIT-PANE-FOCUSED-SIDE (shell)
   ├─ focused (left)  → PANEL-FOCUSED-DRAWING
   └─ side    (right) → PANEL-SIDE-COLOR-PALETTE
```

See [`patterns/PANEL-MODULE-CONTRACT.md`](patterns/PANEL-MODULE-CONTRACT.md).
Shell layouts codify **`slot_chrome_bounds`** and **`slot_content_bounds`** so
panels do not re-apply margins. Waiving insets requires a new layout ID.
The record sketch uses [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE`](layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
(edge-to-edge left canvas / right rail). Other full-bleed experiences still use
an explicit full-canvas layout.

A machine-readable twin of this composition lives in
[`catalog/`](catalog/README.md). [`LAB-14`](../../../client/labs/lab-14-device-ui-catalog/README.md)
packs those YAML scenes at build and mounts them on the BOX-3 so a spec change
can be flashed as a visual review image. The Markdown artifacts remain the
review surface.

## Artifact index

### Remaining x02 coverage

The v1 x02 client is the historical stage inventory for the v2 migration. These
handoff files name the visual artifacts still to author; they are planning
inventories, not approved specifications:

| Directory | Missing-artifact inventory | Main coverage |
|---|---|---|
| Components | [MISSING](components/MISSING.md) | Message card, transport, settings and recipient tiles |
| Layouts | [MISSING](layouts/MISSING.md) | Full-canvas, carousel-stage, receipt geometry |
| Panels | [MISSING](panels/MISSING.md) | Status-message composition, signed-in carousel, picker, settings, send feedback |
| Screens | [MISSING](screens/MISSING.md) | Inbox, settings, recipient picker, send. Login connecting and Wi-Fi are overlays, not replacement screens |

The active [login experience](../experiences/endpoint-login.md) and [sketch
experience](../experiences/sketch-record-and-playback.md) govern the behavior and
geometry they already decide. The signed-in and send stages need further
experience work before their visual artifacts can be implementation-ready.

### Foundations

| ID | Document | Status |
|---|---|---|
| `FOUNDATION-DEVICE-CANVAS` | [Device canvas](foundations/FOUNDATION-DEVICE-CANVAS.md) | Draft — margin, touch, Montserrat **16 / 24 / 32** |
| `FOUNDATION-BUTTON-MINIMUMS` | [Button minimums](foundations/FOUNDATION-BUTTON-MINIMUMS.md) | Draft — PIN grid **101 × 48** suggested minimum |
| `FOUNDATION-MOTION` | [Motion](foundations/FOUNDATION-MOTION.md) | Draft — cover-down is the stage-transition default; carousel snap timing OPEN |
| `THEME-DARK` | [Dark theme](themes/THEME-DARK.md) | Draft — token placement map (single scheme) |
| `FOUNDATION-CARD-BACKGROUNDS` | [Card backgrounds](foundations/FOUNDATION-CARD-BACKGROUNDS.md) | Draft — eight owner-selected solid fills; hardware visual review pending |
| `FOUNDATION-PROFILE-AVATARS` | [Profile avatars](foundations/FOUNDATION-PROFILE-AVATARS.md) | Draft — new artwork deferred; LAB-14 initials fallback approved for now |
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
| `LAYOUT-SPLIT-PANE-FOCUSED-SIDE` | [Focused-side split pane](layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md) | Draft — 240 × 240 left canvas, 80 × 240 right rail |

### Patterns

| Document | Role |
|---|---|
| [Panel module contract](patterns/PANEL-MODULE-CONTRACT.md) | Chrome vs content inset; swappable `PANEL-*`; background override |
| [Cover-down transition](patterns/TRANSITION-COVER-DOWN.md) | h19 stage motion (400 ms, ease-in-out quint) |

Planned examples include a horizontal carousel layout and a centered receipt
layout. Planned entries are not requirements until authored and reviewed.

### Panel modules

| ID | Document | Slot |
|---|---|---|
| `PANEL-HEADER-TITLE-LEFT` | [Header title left](panels/PANEL-HEADER-TITLE-LEFT.md) | `upper` |
| `PANEL-HEADER-TITLE-SUBTITLE` | [Header title + subtitle](panels/PANEL-HEADER-TITLE-SUBTITLE.md) | `upper` |
| `PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION` | [Header title + subtitle with action](panels/PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION.md) | `upper` |
| `PANEL-BODY-ROSTER-CAROUSEL` | [Roster carousel](panels/PANEL-BODY-ROSTER-CAROUSEL.md) | `lower` |
| `PANEL-BODY-FOUR-BUTTON` | [Four-button body](panels/PANEL-BODY-FOUR-BUTTON.md) | `lower` |
| `PANEL-BODY-LABEL-WITH-TWO-BUTTONS` | [Label with two buttons](panels/PANEL-BODY-LABEL-WITH-TWO-BUTTONS.md) | `lower` |
| `PANEL-HEADER-PIN-STATUS` | [PIN status header](panels/PANEL-HEADER-PIN-STATUS.md) | `upper` |
| `PANEL-BODY-PIN-GRID` | [PIN keypad body](panels/PANEL-BODY-PIN-GRID.md) | `lower` |
| `PANEL-STATUS-MESSAGE` | [Connection status body](panels/PANEL-STATUS-MESSAGE.md) | `lower` — `connecting` / `no Wi-Fi` |
| `PANEL-FOCUSED-DRAWING` | [Focused drawing](panels/PANEL-FOCUSED-DRAWING.md) | `focused` — 240 × 240 record canvas |
| `PANEL-SIDE-COLOR-PALETTE` | [Side color palette](panels/PANEL-SIDE-COLOR-PALETTE.md) | `side` — 80 × 240 six-swatch rail |

### Components

| ID | Document | Role |
|---|---|---|
| `COMP-USER-CARD` | [User card](components/COMP-USER-CARD.md) | Roster/inbox card geometry and states |
| `COMP-PIN-KEYPAD` | [PIN keypad](components/COMP-PIN-KEYPAD.md) | 3×3 grid for digits 1–9; v1 h03 key sizing and spacing |
| `COMP-STATUS-MESSAGE` | [Status message](components/COMP-STATUS-MESSAGE.md) | Centered lower-slot copy; login uses `connecting` and `no Wi-Fi` |
| `COMP-RECORD-CANVAS` | [Record canvas](components/COMP-RECORD-CANVAS.md) | 240 × 240 black drawing surface, 8 px corners |
| `COMP-COLOR-SWATCH` | [Color swatch](components/COMP-COLOR-SWATCH.md) | 44 px circle, gray ring, gold selected outline |

### Screens

| ID | Document | Experience | Status |
|---|---|---|---|
| `SCR-SIGN-IN-ROSTER` | [Sign-in roster](screens/SCR-SIGN-IN-ROSTER.md) | `EXP-LOGIN` | Draft — roster visuals owner-aligned |
| `SCR-EMPTY-HANGOUT` | [Empty hangout](screens/SCR-EMPTY-HANGOUT.md) | `EXP-LOGIN` | Draft — fixed empty copy |
| `SCR-PIN-ENTRY` | [PIN entry](screens/SCR-PIN-ENTRY.md) | `EXP-LOGIN` | Draft — panel composition; checking dims keypad; 32 px digits |
| `SCR-PIN-COOLDOWN` | [PIN cooldown](screens/SCR-PIN-COOLDOWN.md) | `EXP-LOGIN` | Draft — full-canvas lockout stays through connectivity loss |
| `SCR-RECORD-SKETCH` | [Record sketch](screens/SCR-RECORD-SKETCH.md) | `EXP-SKETCH-RECORD-PLAYBACK` | Draft — left canvas + right palette |

### On-device catalog

| Artifact | Role |
|---|---|
| [`catalog/`](catalog/README.md) | YAML scenes, tokens, and schema (`family-link.device-ui-catalog/v1`) |
| `LAB-14` | Flashable renderer: `make lab.lab14` |

Adding a screen that reuses existing panels is a new `catalog/scenes/*.yaml`
file with the next unused `CAT-NN` id. New panel types still need a Markdown
spec and a C renderer. Name a visual by its catalog ID (`CAT-01` … `CAT-19`).

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
