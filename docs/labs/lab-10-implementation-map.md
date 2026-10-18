# LAB-10 — Record Canvas and Media Budget implementation map

**Status:** Ready with explicit unknowns  
**Repository:** `/Users/lynnfrank/src/family-link`  
**Map owner:** LAB-10 orchestration run `20261017-L10-RECORD-CANVAS-001`  
**Evidence date:** 2026-10-17 (America/Chicago)  
**Scope:** Local v2 record-canvas capture and performance investigation. No send, recipient, inbox, product networking, or server fixture unless a later package proves a separately named experiment needs it.

## Authority and source alignment

The external LAB-10 brief supplied in the task is the package contract. Active repository requirements and context are:

- [`docs/labs/v2-experience-demo-roadmap.md`](v2-experience-demo-roadmap.md), LAB-10 section and common lab method.
- [`docs/labs/README.md`](README.md), lab registry and evidence boundary.
- [`docs/labs/inbox-carousel-playback-evaluation.md`](inbox-carousel-playback-evaluation.md), shared metrics and evidence conventions.
- [`docs/product/experiences/sketch-record-and-playback.md`](../product/experiences/sketch-record-and-playback.md), owner-decided 240×240 display geometry, 80×240 rail, 120×120 logical storage, and multicolor intent.
- [`docs/v1-assessments/resource-budget-envelope.md`](../v1-assessments/resource-budget-envelope.md) and [`docs/v1-assessments/recording-send.md`](../v1-assessments/recording-send.md), historical evidence and risks, not v2 architecture.
- The read-only historical v1 h19 drawing screen, v1 sketch type, and v1 record implementation, inspected only as a visual/interaction and resource baseline.

The user-supplied v1 assessment paths under `poc-v1/docs/v1-assessments/` are not present in this checkout; the corresponding active copies under `docs/v1-assessments/` are used.

## Experiment contract

### Fixed geometry and interaction baseline

| Item | Contract |
|---|---|
| LCD | 320×240 pixels |
| Capture canvas | Display x=0…239, y=0…239; 240×240; black, 8px radius where the hardware baseline supports it |
| Palette rail | Display x=240…319, y=0…239; 80×240; surface `#101418` |
| Swatches | Six circular 44px targets; vertical pitch 36px; zig-zag horizontal offset ±11px; 10px bottom margin |
| Palette | Bottom-to-top black/background, white, gold, green, red, blue; default white |
| Logical storage | 120×120; display coordinates map by integer division by 2 and are clamped to 0…119 |
| Touch routing | Palette hit regions never append canvas ink; canvas hit region excludes x≥240 |
| Stroke model | DOWN starts a stroke; accepted MOVE points retain phase/color; UP closes it; color changes affect subsequent points and do not silently split or rewrite prior points |
| Erase | No eraser control; black/background-colored strokes are visible erase behavior |
| Entry/exit | Red FAB bloom and physical-button ring wipe are measured historical entry transitions; reverse transition is a candidate lab behavior and must be labeled as such |

### Bounds and clock

The implementation must declare one capture-duration cap, one accepted-point cap, one bounded queue policy, and the minimum movement/coalescing policy in its manifest and trace schema. Capacity exhaustion is an explicit terminal condition, never silent truncation. Timestamps use the monotonic device clock for hardware runs; host synthetic timestamps are separate evidence and must declare their jitter bound. No host-only test is a touch-latency or display-scanout claim.

### Workloads and evidence levels

Manual and deterministic human-like workloads are separate. Synthetic paths must include variable spacing and velocity, corners, pauses, pen-up gaps, multiple strokes, color changes, edge-adjacent points, and bounded timestamp jitter. Repeated identical straight lines are not an accepted primary workload.

Every result labels evidence as `hardware`, `host`, `derived_static`, or `unmeasured/blocked`. Hardware display, touch-to-ink, audio continuity, and task scheduling claims require hardware evidence; host tests may validate state machines, bounds, parsers, generators, aggregation, and deterministic renderer pressure only.

## Requirement traceability

| ID | Requirement | Status at mapping | Packages |
|---|---|---|---|
| L10-R01 | Preserve the h19 320×240 visual geometry and transitions without modifying the archive | missing in v2 lab | L10-01, L10-02 |
| L10-R02 | Keep palette rail and canvas hit regions disjoint | missing in v2 lab | L10-02, L10-03 |
| L10-R03 | Capture bounded DOWN/MOVE/UP events with raw/display/logical coordinates, color, stroke boundaries, timestamps, and explicit loss outcomes | missing in v2 lab | L10-03 |
| L10-R04 | Render visible multicolor ink with bounded work and measured control/candidate modes | missing in v2 lab | L10-04 |
| L10-R05 | Measure drawing concurrently with audio, including continuity and memory/resource pressure | partial; LAB-03 is playback-only and not a capture lab | L10-05 |
| L10-R06 | Provide manual and deterministic realistic workloads | missing in v2 lab | L10-06 |
| L10-R07 | Emit complete latency, frame, task, queue, heap, storage, and cleanup metrics with evidence provenance | partial; shared schemas exist, LAB-10 fields do not | L10-07 |
| L10-R08 | Integrate with lab runner conventions, parse malformed/missing evidence, and separate workflow from experiment verdict | partial; generic runner exists, LAB-10 adapter does not | L10-08 |
| L10-R09 | Qualify cancellation, failure, repeated runs, terminal uniqueness, cleanup, and memory drift | missing in v2 lab | L10-09 |
| L10-R10 | Keep hardware-only claims separate from host-only evidence and do not mark unmeasured performance as pass | process requirement | all packages |

## Present state and delta

| Present state | Required state | Suggested resolution |
|---|---|---|
| LAB-03 has bounded media playback and resource tracing, but no local touch capture and no LAB-10 record screen | A distinct record-canvas lab with real capture and audio-concurrency evidence | Create `client/labs/lab-10-record-canvas/`; reuse runner conventions only where ownership remains clear |
| The h19 screen is a historical stub; v1 record code has capture/send coupling and unresolved buffer risks | A new v2 lab must preserve geometry while replacing the mock recorder with bounded capture | Extract measured baseline facts into active LAB-10 docs/code; never modify or depend on the archive at build time |
| Shared v2 run manifest/event schemas and adapters exist | LAB-10 needs trace schema, parser, evidence, and missing-field rules | Add LAB-10-specific schemas/tools and a surgical shared-runner registration |
| No hardware result is available at mapping time | Hardware visual, latency, audio, and memory claims need synchronized runs | Mark these `UNMEASURED` or `BLOCKED` until an available kit is run; preserve host evidence separately |

## Packages

| ID | Outcome | Dependencies | Main scope | Verification gate |
|---|---|---|---|---|
| L10-01 | Frozen experiment contract, lab README/manifest, evidence template, and explicit host/hardware boundary | — | LAB-10 docs and manifest only | Map is implementable; bounds, geometry, metrics, and exclusions are explicit |
| L10-02 | h19-based 320×240 drawing UI with palette state and transitions | L10-01 | LAB-10 firmware UI | Geometry, swatch positions, state rendering, and transition paths are independently inspected |
| L10-03 | Real bounded touch capture state machine | L10-01, L10-02 | Capture model, touch adapter, native tests | Phase/color/mapping/bounds/coalescing/cap/cancel behavior is proven |
| L10-04 | Measured rendering/presentation modes | L10-03 | Raster worker, control/candidate modes, UI publication | Work per update is bounded; modes and latency/invalidation hooks are visible |
| L10-05 | Audio-concurrency experiment | L10-03, L10-04 | Local audio capture integration and scenarios | No-stroke/sparse/continuous/dense/color-switch/stop-fail cases are represented; audio health is not inferred from UI responsiveness |
| L10-06 | Manual and deterministic human-like workload suite | L10-03, L10-04 | Workload generator, instructions, fixtures | Generated traces are varied, bounded, and not repeated-line fixtures |
| L10-07 | Complete instrumentation and resource evidence | L10-03, L10-04, L10-05, L10-06 | Trace emitters, metric aggregation, schemas | Each field has provenance and percentiles/cleanup/memory-drift handling |
| L10-08 | Runner, parser, structured evidence, and host tests | L10-01…L10-07 | LAB-10 tools, adapter, registry integration, evidence output | Malformed/missing traces fail closed; workflow and experiment verdicts stay separate |
| L10-09 | Failure/cancellation/repeated-run qualification and final report | L10-03, L10-05, L10-07, L10-08 | Failure injection, repeated-run tests, report | One terminal outcome, no stale worker mutation, no leaks/drift, explicit blocked/unmeasured outcomes |

## Implementer briefs

### L10-01 — evidence baseline and experiment contract

Create the LAB-10 project scaffold and its README/manifest/evidence template. Encode the exact geometry, coordinate mapping, palette hit rectangles, accepted-point/duration/queue bounds, monotonic clock rule, workload separation, metric field list, evidence provenance, and exclusions. Record that LAB-03 is playback evidence only. Do not add a server or change any shared runner file in this package.

### L10-02 — h19-based drawing interface

Implement the steady-state 240×240 canvas plus 80×240 rail, six 44px zig-zag swatches, selected-color state, red bloom entry, physical ring-wipe entry where the BSP supports it, reverse exit, and stopped/cancelled/failed/completed presentation states. Touch routing must prevent rail touches from becoming ink. Palette changes must update only bounded palette UI, not force a full-screen redraw. Historical v1 source is read-only and out of scope.

### L10-03 — real touch capture pipeline

Replace the mock recorder with bounded DOWN/MOVE/UP capture. Preserve raw reports separately from accepted points. Store raw display and mapped logical coordinates, timestamp, color, and stroke identity. Declare movement/coalescing rules and explicit handling for identical points, edges, rail touches, leave/re-enter, rapid color changes, second touch, caps, stop/cancel, and allocation/overflow failure. Emit explicit accepted/coalesced/rejected/overflow counts.

### L10-04 — rendering and presentation pipeline

Implement and instrument direct-point, segment, bounded-batch, and whole-frame control modes where the hardware path permits. Keep the control path measurable. Bound raster and UI publication work per update, expose invalidation/lock/backlog information, and prevent unbounded replay/encoding work from blocking the UI task.

### L10-05 — audio-concurrency experiment

Add local audio capture/concurrency scenarios only; no send or recipient flow. Cover no-stroke control, sparse, continuous, dense, rapid color switching, stop/cancel during audio, capture failure, and allocation failure. Measure audio continuity, occupancy/high-water, overruns/underruns, drawing latency, retained/transient PCM/sketch/framebuffer bytes, and cleanup time. Unknown audio/DAC facts must remain unknown.

### L10-06 — realistic workload suite

Add manual instructions for all requested human gestures and a deterministic generator with variable velocity/spacing, corners, pauses, pen-up gaps, multiple strokes, color changes, edge points, and declared timestamp jitter. Keep synthetic traces distinct from manual touch-latency evidence and provide workload identities and schema versions.

### L10-07 — instrumentation and resource evidence

Emit or derive the required input, latency/UI, CPU/task, memory, storage/format, queue, cleanup, and repeated-run fields. Include p50/p95/p99/max where samples exist, field-level provenance, missing-field reasons, and explicit point-cap/duration-cap events. Preserve color changes and pen-up boundaries in the chosen representation.

### L10-08 — runner, parser, and evidence

Integrate with the existing shared runner using append-only, conflict-preserving edits to shared files. Provide manual and deterministic modes, readiness/run/scenario markers, bounded logs, parser output, malformed/missing-field tests, metric aggregation, terminal-event uniqueness, cancellation, bounds, color, stroke, and cap tests. Keep workflow verdict and experiment verdict independent; no hardware result may be synthesized.

### L10-09 — failure and repeated-run qualification

Exercise cancellation, stop-in-stroke, color change in stroke, audio stop, allocation/capture/encoder/worker/UI-lock failures, repeated cycles, full-duration runs, shell exit, and safe reset/boot cases. Prove resource release, stale-worker isolation, callback/timer cleanup, bounded queues, terminal uniqueness, cancellation latency, and memory drift. Produce the final report separating implemented, host-verified, hardware-verified, unmeasured, blocked, unresolved decisions, and next experiment.

## Independent verifier briefs

Each verifier must use a fresh read-only invocation, inspect actual repository state, and not continue the implementer context.

- **L10-01:** Confirm the map/scaffold is implementable, bounded, traceable, and does not inherit unresolved v1 behavior silently.
- **L10-02:** Inspect geometry numerically and compare palette hit regions against the h19 baseline; prove no archive modification and no palette-triggered full-screen redraw.
- **L10-03:** Inspect the state machine and native tests for every cap, phase, color, mapping, multi-touch, stop, cancel, and cleanup branch.
- **L10-04:** Review all render modes and bounded worker/UI handoff; reject unmeasured performance claims and unbounded work.
- **L10-05:** Verify scenario coverage and that audio health requires audio evidence, not UI responsiveness.
- **L10-06:** Run the generator and inspect traces for human-like variation; reject repeated-line fixtures as primary workload.
- **L10-07:** Cross-check every required metric, unit, provenance label, percentile, missing-field path, and format-boundary preservation.
- **L10-08:** Run parser/host tests and inspect registry/runner integration, malformed traces, missing evidence, bounded logs, and separate verdicts.
- **L10-09:** Independently exercise or inspect failure/repeat paths and final report claims; verify no stale worker, leak, drift, or duplicate terminal event.

## Exclusions

- Do not modify the historical v1 archive.
- Do not add server, send, recipient, inbox, durable outbox, or product networking behavior.
- Do not treat LAB-03 playback as LAB-10 capture evidence.
- Do not claim display scanout, touch latency, audio continuity, or hardware memory safety from host-only tests.
- Do not commit, push, merge, reset, stash, clean, or create a pull request.

## Validation and hardware limits

Host evidence may cover pure capture/render models, parser/schema/metric behavior, workload generation, and native tests. Hardware evidence is required for visual fidelity, touch-to-ink latency, LVGL invalidation/lock behavior, task load, audio continuity, actual heap/PSRAM/stack pressure, and synchronized screen/audio conclusions. If no suitable kit or synchronized capture is available, the final report must say `UNMEASURED` or `BLOCKED` and preserve the exact missing evidence.

## Suggested run commands

The exact LAB-10 registry name and build directory are implementation outputs. The final README must provide the resolved commands using the repository's `make lab.<name>`, build, flash, capture, parser, and evidence conventions, plus the manual workload instructions and hardware prerequisites. Do not invent a successful hardware command before the registry and adapter are verified.

