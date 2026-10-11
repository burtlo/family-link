# V1 assessment synthesis and v2 implementation practices

Status: implementation guide; historical assessments are evidence, while the
[client application coding standards](../standards/client-application-coding-standards.md)
and approved product specifications govern v2 behavior.

## What the assessments establish

The integrated x02 app has a useful stage router, connecting-dot updates, roster
snap behavior, and in-place playback transport refresh. Its recurring failures
come from work and ownership crossing those boundaries: synchronous probe, Wi-Fi,
inbox, view-write, picker, and codec paths delay presentation; feature modules
apply session transitions directly; data and receipt changes can trigger full
scene replacement. These are source-review findings, not measured v2 results.
See [x02 shell](../product/device-ui/assessments/ASSESS-SHELL-x02.md),
[authentication/connectivity](authentication-connectivity.md),
[carousel/playback](carousel-playback.md), and
[recording/send](recording-send.md).

The demo assessments identify narrower patterns worth adapting: persistent
roots and cover transitions in [h19](../product/device-ui/assessments/ASSESS-DEMO-h19.md),
in-place transport updates in [h18](../product/device-ui/assessments/ASSESS-DEMO-h18.md),
badge updates in [h14](../product/device-ui/assessments/ASSESS-DEMO-h14.md),
property motion in [p04](../product/device-ui/assessments/ASSESS-DEMO-p04.md),
and snap behavior in [p13](../product/device-ui/assessments/ASSESS-DEMO-p13.md).
They do not establish that an entire demo is a product screen. The
[pattern ledger](../product/device-ui/assessments/DEMO-PATTERN-LEDGER.md)
distinguishes deep reviews from static scans, and the
[demo index](../product/device-ui/assessments/DEMO-EXPLORER-INDEX.md)
lists what each island actually exercises.

Memory pressure spans two different pools. The carousel eagerly builds up to
16 cards, can retain a sketch buffer per card, and rebuilds portraits during
scroll. Record/send holds large capture and multipart copies. A full-screen
RGB565 buffer is 153,600 bytes; h19's record panel has **no** capture buffer.
The v2 design needs separate LVGL-object and pixel/media budgets, including
transition overlap and the largest allocatable block. See the
[carousel module](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md),
[resource budget guide](resource-budget-envelope.md),
[h19 assessment](../product/device-ui/assessments/ASSESS-DEMO-h19.md), and
[LVGL budget foundation](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md).

The UI evidence is uneven: many demo sheets are static reviews with their
Measured column empty. Use their mechanisms and estimates as candidates, then
measure the v2 implementation on its target. Do not promote an estimated
widget count, frame time, or memory target into a proven device result. See the
[assessment template](../product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md)
and [verification ladder](../standards/client-application-coding-standards.md#11-verification-ladder).

## Practices for each v2 feature slice

The table turns the standards into reviewable work. A feature slice is one
user-visible journey or component plus the operations needed to run it. Record
the artifacts in its implementation change or linked specification; a short
table is sufficient when the behavior is simple.

| Practice and standards section | Required artifact or code evidence | Verification gate | Assessment basis |
|---|---|---|---|
| **1. Trace the journey before coding** (§7, §12): link the approved experience, screen/component, and communication contract; list each stage, wait, failure, cancellation, preserved input, and OPEN decision. | A scenario table with product/spec links and an owner for each OPEN item. Keep wire uncertainty in the communication contract. | Contract scenarios can be named and tested; no implementation choice silently resolves an OPEN product rule. | [Operational contract](operational-contract-shape.md), [communication inventory](communication-contract-inventory.md), [record/send](recording-send.md) |
| **2. Draw the ownership and wait map** (§1–3): identify presentation, session, and I/O owners for every callback, tick, and worker completion; mark any operation that can wait on a network, codec, lock, queue, or storage. | A small call/sequence map and code review of hot paths. Presentation emits intent; one session reducer applies stage transitions; workers return typed events. | Delayed I/O and queue-full injection leave input and animation responsive and produce an observable result. | [Authentication/connectivity](authentication-connectivity.md), [carousel/playback](carousel-playback.md), [record/send](recording-send.md), [x02 shell](../product/device-ui/assessments/ASSESS-SHELL-x02.md) |
| **3. Specify each async operation** (§4, §9): freeze input, assign operation and session identity, enumerate terminal outcomes, deadlines, cancellation, retry, and side-effect key. | Operation table or type definition plus reducer handling for success, failure, cancel, stale, timeout, and queue rejection. For multi-recipient send, record the decided atomicity rule. | Race tests prove exactly one terminal outcome; a late result cannot mutate a newer operation or session. | [Async envelope](async-operation-envelope.md), [record/send](recording-send.md), [carousel/playback](carousel-playback.md) |
| **4. Classify every UI update** (§5): list recurring changes as Level 0, 1, or 2 before picking widget APIs. State the stable identity keys and teardown duties. | An update-level map in the screen/component assessment. Level 1 diff preserves focus and scroll by ID; Level 2 records stage transition, timer/animation cancellation, and handle invalidation. | Inbox arrival, badge, transport, and send progress do not destroy the scene; repeated rendering of one snapshot does not duplicate callbacks or timers. | [UI update taxonomy](ui-update-taxonomy.md), [carousel module](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md), [h18](../product/device-ui/assessments/ASSESS-DEMO-h18.md), [h14](../product/device-ui/assessments/ASSESS-DEMO-h14.md) |
| **5. Make background policy explicit** (§3, §6): separate session stage, dependency truth, and poll schedule. Decide ignore/defer/merge/cancel/transition for each result during an active operation. | Stage × background-event matrix with freshness/operation identity and retry timing source. | A probe or inbox notification during PIN entry, playback, or send follows the matrix; cached data never implies ready service. | [Stage vs poller](session-stage-vs-background-poller.md), [authentication/connectivity](authentication-connectivity.md) |
| **6. Budget both memory ledgers and time** (§2, §8): set target limits before building; count steady and transition nodes, pixel/media buffers, copies, queue depth, largest allocation, and frame p95/p99. Name degradation at each boundary. | A filled [LVGL assessment](../product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md) or equivalent for the target runtime, with target and measured columns clearly separated. | Max collection/media and allocation-failure scenarios reach defined UX without a white screen, stale handle, or indefinite operation. Timing and high-water claims carry L2/L3 evidence as appropriate. | [Resource budget](resource-budget-envelope.md), [LVGL foundation](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md), [carousel module](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md), [h23](../product/device-ui/assessments/ASSESS-DEMO-h23.md) |
| **7. Close the slice with evidence** (§10–13): use deterministic checks for forbidden imports/calls and reducer ownership, then run the contract's failure and race scenarios. | A short evidence note naming build, model, scripted runtime, and target observations separately; include trace, measurement, or screenshot links where available. | Reviewer can reproduce the claimed level. A static demo review or clean build is not reported as target confirmation. | [Verification ladder](../standards/client-application-coding-standards.md#11-verification-ladder), [assessment template](../product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md) |

## First applications of these practices

- **Login:** move join/probe and roster refresh off presentation; use typed
  auth outcomes and one reducer; test 401 versus server failure, stale success,
  queue overflow, and a probe during PIN entry. See
  [authentication/connectivity](authentication-connectivity.md).
- **Inbox carousel:** keep snap and transport property updates, diff inbox by
  message ID, coalesce view writes, virtualize live cards, and measure scroll
  while the write is delayed. Product must settle carousel visibility during
  send and sketch reveal on arrival before those behaviors ship. See the
  [carousel module](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md).
- **Record/send:** render the picker from a snapshot, freeze recipients and
  session in a send operation, define partial success and retention policy,
  stream or cap media copies, and prove cancel/timeout/late-result behavior.
  See [record/send](recording-send.md) and the
  [communication inventory](communication-contract-inventory.md).
- **Device UI:** use h19 for stage motion, h27 for timed sketch evidence, and
  the approved sketch experience for geometry; h19's record panel is a visual
  stub. Measure the actual v2 buffer and transition peak. See
  [h19](../product/device-ui/assessments/ASSESS-DEMO-h19.md),
  [h27](../product/device-ui/assessments/ASSESS-DEMO-h27.md), and the
  [sketch experience](../product/experiences/sketch-record-and-playback.md).
