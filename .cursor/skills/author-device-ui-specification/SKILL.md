---
name: author-device-ui-specification
description: Interviews the product owner and creates or revises fixed-hardware device UI foundations, layouts, components, and screen compositions with native-pixel geometry, visual states, traceability, and explicit OPEN items. Use when the user asks to define a device screen, layout, carousel, PIN pad, drawing canvas, visual component, wireframe, or UI design-system artifact.
disable-model-invocation: true
---

# Author device UI specification

Create reviewable visual specifications for the v2 fixed-hardware device. This
skill authors documentation; it does not implement firmware or approve product
decisions.

## Canonical workflow

Read and follow
[`docs/product/device-ui-specification-authoring.md`](../../../docs/product/device-ui-specification-authoring.md).
It is the source of truth for artifact types, structure, identifiers, review
criteria, and readiness.

Use the current examples:

- [`PANEL-MODULE-CONTRACT`](../../../docs/product/device-ui/patterns/PANEL-MODULE-CONTRACT.md)
- [`FOUNDATION-DEVICE-CANVAS`](../../../docs/product/device-ui/foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`LAYOUT-SPLIT-PANE`](../../../docs/product/device-ui/layouts/LAYOUT-SPLIT-PANE.md)
- [`SCR-SIGN-IN-ROSTER`](../../../docs/product/device-ui/screens/SCR-SIGN-IN-ROSTER.md)

Examples demonstrate structure, not automatic product policy.

## Before authoring

1. Read [`docs/AGENTS.md`](../../../docs/AGENTS.md) and the
   [device UI index](../../../docs/product/device-ui/README.md).
2. Read the applicable experience specification and client standards.
3. Obey the
   [v1 archive rule](../../../.cursor/rules/poc-v1-reference-archive.mdc):
   read historical material only as evidence, extract needed facts, and do not
   link active artifacts to archive paths.
4. Classify the requested artifact as a foundation, layout, component, screen,
   or revision.
5. Prefer one coherent artifact. Name dependencies without silently specifying
   them.

## Workflow

### 1. Reconstruct

Summarize:

- visual purpose and target stage;
- full canvas or parent slot;
- known owner decisions and experience requirements;
- inherited layouts, components, and foundations;
- unresolved visual choices and source conflicts.

Ask the owner to correct this reconstruction before treating it as settled.

### 2. Interview in small rounds

Ask two to four related questions at a time:

- anatomy and hierarchy;
- geometry, keylines, padding, clipping, and overflow;
- visual states and variants;
- content limits, touch, legibility, and accessibility;
- locked presentation, bounded variation, and delegated choices.

Do not ask the owner to choose LVGL APIs or internal object structure.
Recommendations remain `PROPOSED` until accepted.

### 3. Draft incrementally

Follow the required structure for the artifact type. Include:

- stable `FOUNDATION-`, `LAYOUT-`, `COMP-`, or `SCR-` ID;
- status, revision, authority, dependencies, and revision history;
- text wireframe or anatomy sketch;
- authoritative native-pixel geometry where decided;
- visual states and boundaries;
- traceability to owner decisions and `EXP-` requirements;
- stable `UI-OPEN-###` items for unresolved presentation;
- separate owner-review, visual-approval, implementation, and verification
  readiness.

Do not duplicate the behavioral contract. Link it.

### 4. Challenge

Walk minimum and maximum content, long text, focus, disabled/empty/waiting/error
states when applicable, clipping, touch reachability, and hardware color/asset
constraints.

Ask: could two competent implementers produce materially different visible
results? Resolve unintended differences or mark them `OPEN`.

### 5. Owner review

Summarize:

1. locked presentation;
2. bounded variation;
3. delegated implementation choices;
4. blocking and non-blocking OPEN items.

Do not claim approval from silence. Update the artifact after corrections and
record the review status honestly.

### 6. Deliver

Provide:

1. created or revised artifact paths;
2. a concise review-criteria summary;
3. unresolved visual decisions and their consequences;
4. readiness for owner review versus implementation;
5. any missing dependency artifacts.

Update the device UI index when adding, renaming, or superseding an artifact.

## Hard boundaries

- Do not implement firmware, generate production assets, or refactor UI code.
- Do not resolve behavior, copy, retry, timeout, or side-effect policy in a
  visual specification.
- Do not invent responsive breakpoints for hypothetical hardware.
- Do not turn v1 implementation details into v2 requirements.
- Do not claim owner approval without an explicit review checkpoint.
- Do not conceal unknown geometry behind an illustrative wireframe.

## Suggested invocation

```text
/author-device-ui-specification

Create or revise [artifact] from the following owner description: [...]
```

If behavior is too incomplete to specify the visual result, identify the exact
experience gap and recommend running `/author-experience-specification` first.
