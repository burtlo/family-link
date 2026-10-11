# Lab plan: large inbox, reusable cards, and audio/sketch playback

Status: proposed experiments; **no run or target measurement is claimed here**.  
Scope: v2 BOX-3 carousel and media design before integration with the full experience.  
Broader x02 journey: [v2 experience demo and lab roadmap](v2-experience-demo-roadmap.md) (`LAB-06`–`LAB-13`).  
Implementation layout: [lab repository layout](repository-layout.md) (`client/labs/`, `server/labs/`, evidence). Assistant workflow: [lab assistant guide](assistant-guide.md).  
Sources: [carousel module assessment](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md), [x02 shell assessment](../product/device-ui/assessments/ASSESS-SHELL-x02.md), [demo explorer index](../product/device-ui/assessments/DEMO-EXPLORER-INDEX.md), [LVGL budget foundation](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md), [sketch experience](../product/experiences/sketch-record-and-playback.md), and [proposed inbox/media contract](../product/communication-contracts/inbox-and-media.md).

## Questions these labs must answer

1. Does inbox cost stay bounded as message count grows, including a push or page arrival during a snap?
2. Can a small number of card widgets be rebound without showing the previous message's portrait, sketch, transport, or asynchronous result?
3. Can audio and timed multicolor strokes stay synchronized while playback starts, pauses, stops, and moves between messages?
4. Is pause/resume sufficient with one active raster, or do older positions require a preview, replay from zero, or timed checkpoints?
5. What are the measured memory, frame-time, format-size, and failure boundaries on the target?

The v1 **16-message limit is a fetched-page/client bound**, not evidence that the product needs a 16-message total inbox. V1 eagerly built all fetched cards and could retain a sketch buffer per card; see the [carousel module assessment](../product/device-ui/assessments/ASSESS-MODULE-v1-carousel.md#4-lvgl-object-model-and-memory). The v2 contract proposes cursor paging but leaves page size, changing-inbox consistency, older-message navigation, and focus outside the loaded page **OPEN** ([COM-OPEN-002](../product/communication-contracts/inbox-and-media.md#failure-and-continuity-rules-to-decide)). Keep server retention, fetched summaries, live widgets, and decoded media as separately measured limits.

## Existing demos and baseline

Create a **new implementation** of each selected demo in an active lab code tree
outside the v1 archive, with its own build target, instrumentation, and version.
Use the paths in [repository layout](repository-layout.md) (for example
`client/labs/lab-02-bounded-inbox/` for `LAB-02`). Reproduce only the behavior
needed for the experiment; do not edit the original demo or add probes to its
source. The original may be run unmodified as a comparison. Record the original
revision and the new implementation revision separately. Publish run results under
[`docs/labs/evidence/`](evidence/README.md). This keeps the original as evidence
and gives the evaluator a clean place to change allocations, timing, rendering,
and fault injection.

| Reference behavior | New lab implementation and instrumentation | Server included in the lab version |
|---|---|---|
| **p13** snap/scroll | Recreate snap math and card visuals; add per-frame distribution, LVGL high-water/largest block, card bind count, and callbacks per gesture. | **None**; local UI lab. |
| **h18** playback screen | Recreate transport updates and audio worker; add play intent/first-sample, audio position, update cost, cancel latency, and underruns. | **Required:** new versioned playback fixture corresponding to the h18 playback server, including catalog JSON and audio route/bytes. |
| **h27** timed sketch | Recreate capture/replay at v2 120×120 logical size; add colored strokes, raster time, invalidation, pause/resume and restart cost. | **Required for send/fetch runs:** new versioned sketch fixture corresponding to the h27 sketch server. A wholly local raster-only run may omit it and must say so. |
| **h28** CPU-painted clip | Recreate a timed blit stress case; compare full-image and bounded sketch-image/dirty-region updates. | **None**; local display lab. |
| **h19** persistent transition root | Recreate the relevant transition and tree overlap with a working sketch image. The original record panel is only a visual stub. | **None**; local UI lab. |
| **x02** integrated shell | Recreate the evaluated x02 journey and its blocking/repaint behavior in a separate, instrumented lab implementation; retain an unmodified x02 run for comparison when possible. | **Required for network/inbox/play/send runs:** new versioned product fixture corresponding to the v1 product server, with isolated test data. |

The combined LAB-03 audio/sketch candidate needs **one new versioned server
fixture that serves both audio and sketch for the same stable message IDs**.
It can adapt h18 and h27 response behavior, but the firmware and server must be
released and evaluated as one lab version. LAB-02 may use a local deterministic
page provider for controller-only timing; its paging, push, and delayed-I/O runs
need a versioned server fixture that actually supplies those conditions.

Each lab package must identify `lab_id`, firmware build/revision, server
build/revision or `none`, fixture schema/revision, media hashes, and protocol
compatibility. If a server response or timing behavior changes, increment the
server fixture version and rerun affected comparisons. Do not point a lab at a
live household archive or treat a different server version as the same run.
Publish the firmware and required server together as one demo version, for
example `LAB-03/v1 = firmware <build> + server <build> + fixture <revision>`;
publish a local-only lab with `server = none`. A server-enabled demo is incomplete
without its paired server implementation and deterministic fixture.

The [demo index](../product/device-ui/assessments/DEMO-EXPLORER-INDEX.md#ledger-cross-check-rev-02) currently lists **zero** filled on-device measurement tables. Its demo PASS markers establish only their narrow smoke criteria. The v1 archive remains read-only under the [agent guide](../AGENTS.md); the new lab firmware and any required server fixture live outside it.

## Common method and evidence

- Run on the intended device and firmware configuration; record board, display/LVGL configuration, firmware and server build IDs, fixture version, codec, storage mode, and whether logs or probes affect timing. Use the same fixture and server version for baseline and candidate comparisons when both builds speak that version; otherwise verify response/media equivalence and label the comparison's limits.
- Use deterministic inboxes of **8, 16, 64, and 256 summaries**. The larger cases exercise paging and client metadata retention; they must not instantiate 256 widgets. Include an audio-only item, a short sketch, a maximum accepted sketch, an unavailable/corrupt asset, repeated senders, and an item removed while focused.
- Use at least one dense multicolor sketch with rapid strokes, pen-up boundaries, and background-colored strokes. Compare the same drawing and audio across format variants. Record the point/segment count, encoded bytes, duration, and media hashes.
- Trace monotonic timestamps with `session_id`, `message_id`, `operation_id`, and card binding generation. Record event enqueue, dequeue, operation start/terminal result, audio position, sketch time reached, card bind/unbind, UI tick start/end, render/invalidations, allocations, and queue overflow. Keep logs free of credentials and media content.
- Capture p50/p95/p99 **and worst** UI tick, callback, snap, paint, decode, and raster time; do not use average or free heap alone. Capture LVGL pool used/high-water/biggest free block, internal/PSRAM free and largest block, live object and framebuffer counts, active worker/queue depth, stack high-water, audio underruns, and allocation failures.
- Save raw serial traces, parsed metrics, fixture manifest, build/config IDs, and a short screen video for visual continuity and audio/sketch timing. Fill the [assessment measurement table](../product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md#4-standard-measurement-table) for each target run. Record whether a result is L0, L1, L2, or L3 under the [verification ladder](../standards/client-application-coding-standards.md#11-verification-ladder).
- Separate **baseline observation**, **candidate result**, **proposed gate**, and **decision**. The carousel assessment's `p95 < 33 ms` snap figure is a **draft target**, not an approved device budget. Set final numeric limits from measured target capability and the intended interaction quality before declaring pass/fail.

## LAB-01 — x02 integrated baseline

**Run:** Reproduce the same login → carousel → play → stop → inbox update → record/send-return journey with 8 and 16 messages. Repeat with a two-second view-write delay, delayed inbox response, sketch fetch failure, and network timeout. Save an unmodified x02 observation with an isolated v1 product-server data set if available, then measure the **new x02-like lab implementation** and its paired, versioned product server fixture. Prefer a fixture that can serve both builds through compatible v1 endpoints; if it cannot, compare only equivalent journeys and verified response/media content and state the limitation. The reimplementation is a diagnostic model, not a claim that x02 itself was instrumented.

**Capture:** UI-loop period and longest gaps; time spent in probe/join, inbox reload, view write, playback stop/chirp, paint, portrait refresh, and sketch raster; repaint count per inbox event; card/object and pixel-buffer peaks; audible interruption and visible animation stalls.

**Result sought:** A quantified failure-chain baseline that identifies which waits and rebuilds dominate latency and peak memory. **Re-evaluate the diagnosis** if a measured stall occurs without a corresponding timed wait, lock, allocation, or full paint; inspect display driver and task scheduling before attributing it to the carousel.

## LAB-02 — bounded inbox and card recycling

**Code (projected):** `client/labs/lab-02-bounded-inbox/`; Phase B server
`server/labs/lab-02-paging/`. See [lab registry](README.md#lab-id-registry) and
[LAB-02 implementation outline](lab-02-implementation-outline.md) (tree, scenario
IDs, phases, milestones).

**Run:** Implement a small standalone v2 carousel lab with a metadata page model, stable message IDs, and a controller binding **center plus immediate neighbors** to three reusable card views. Exercise 8/16/64/256-item fixtures, 20 repeated end-to-end traversals, rapid reversals, page arrival and focused-item removal during snap, delayed view writes, and repeated bind/unbind. Run a comparison build that eagerly creates all cards at 16 items. Phase A may use a local page provider only (`server = none`).

**Capture:** Live/peak widgets and buffers versus inbox size; bind/unbind counts; frame-time distribution; input-to-focus latency; allocations per scroll event; focus ID and visual center after each snap; wrong-message content, stale image, duplicate callback, and stale async completion count; page fetch bytes and metadata memory.

**Result sought:** View/resource cost stays approximately constant as inbox size grows; only the visible window owns card widgets; data arrival is a Level 1 update, and scroll/focus changes are Level 0; no remote wait occurs in a gesture. **Re-evaluate the controller or cache** if nodes/buffers grow with total inbox size, a card briefly shows another message's media, a page replaces the whole scene, focus is lost on insertion/removal, or p95/p99 worsens materially under delayed I/O. A hard total-inbox cap is not a substitute for fixing those effects.

## LAB-03 — audio/sketch switching and pause

**Run:** Build a new h18/h27-inspired lab, with a paired server version serving audio and sketch by stable message ID. Use a **120×120** timed raster. Exercise play A → pause → resume; play A → focus B → play B; rapid A/B/A; cancel during audio/sketch fetch; new inbox page during play; sign-out; asset failure; and pause while the card moves offscreen. Test audio-only, short, and maximum accepted dense sketches. Repeat the rapid-switch path at least 20 times and include one sustained run long enough to expose cache or heap drift. Keep the server's media bytes/hashes, delays, and failures fixed across candidate builds.

**Capture:** For each attempt, save intent, fetch start/end, validated media ready, first audible sample, first sketch frame, pause/resume/stop request and visible effect, worker terminal event, and final UI reconciliation timestamps. Sample audio clock and rendered sketch progress every update; separately record each stroke's **reveal lateness** (actual visible time minus the audio time when that stroke became due), since a gap with no strokes is not rendering lag. Record p50/p95/p99/worst of UI callback, raster, decode, cancel-to-visible, and start-to-audible time; segments processed per update and queued backlog; audio underrun count and duration; audio worker/renderer CPU or busy time and stack high-water; total and largest-free internal/PSRAM blocks; LVGL high-water/largest-free block; active framebuffer and media/cache bytes; allocation attempts/failures; exactly-one terminal events; stale completions rejected; and a synchronized screen/audio recording.

**Result sought and review triggers:** Use the following as **provisional lab criteria**, not approved product budgets. Record every measured value even when a row passes.

| Dimension | Evidence sought | Re-evaluate or optimize when |
|---|---|---|
| Audio continuity | **Zero audio underruns** during successful nominal playback, including sketch rendering and A/B switching after media is ready. Report network-buffering intervals separately. | Any renderer-induced underrun, audible dropout, or codec task starvation occurs. Distinguish network buffering from CPU, lock, and allocation pressure. |
| Sketch synchronization | p50/p95/p99/worst stroke reveal lateness and paused-frame position. Initial exploration target: p95 no more than **one intended sketch update interval**, p99 no more than **two**; if using a 100 ms update, that is 100/200 ms. | Backlog grows for the length of a message, strokes appear after audio completion, pause advances the drawing, or the user can see repeated jumps. A sparse-stroke gap must not be counted as lag. |
| Presentation time | p50/p95/p99/worst UI callback and paint times with audio active. Compare with the chosen frame deadline; the carousel's **<33 ms p95 snap** value is only a draft reference. Also report whether one dense catch-up tick monopolizes the UI owner. | p95 exceeds the proposed frame deadline, p99/worst shows visible hitching, or any callback waits on fetch, codec shutdown, sketch decode, or raster catch-up. Do not hide a long worst case inside a passing p95. |
| Raster/decode load | Per-tick segments, microseconds, backlog age, and worker busy percentage. Set a per-tick work cap before the run and record how long a dense sketch takes to catch up. | The cap is exceeded, worker demand grows without bound, decoding competes with audio, or skipping work changes the final drawing or stroke order. Compare off-thread raster, smaller invalidation, and LAB-04 format/checkpoint variants. |
| Memory and fragmentation | A 120×120 RGB565 active image is **28,800 B**. Record actual active/paused/preview image count, cache bytes, audio buffers, decoder copies, LVGL pool peak, and **largest free block** in each heap class at simultaneous peak. Define the next required allocation size and reserve before the run. | Any allocation fails; memory rises across repeated A/B/A cycles; a cache exceeds its declared cap; or the largest free block falls below the next required allocation plus its declared reserve, even when total free memory looks sufficient. |
| Start, stop, and identity | Warm/cold start-to-audible distribution, cancel request to visible stop, worker termination time, exactly one terminal event per accepted operation, and zero stale-result mutations of a rebound card. | A stop blocks input, late A data changes B, a cancelled fetch holds an active card or buffer indefinitely, duplicate terminals occur, or startup delay is dominated by avoidable full-media fetch/decode. |
| Pause and resume | Paused pixels and audio position remain stable; resume reconstructs the correct image at the saved position; report reconstruction bytes/time and any audio delay. | The card must replay thousands of strokes on the UI thread, resume presents an image from the wrong time, or preserving every paused image breaks the memory envelope. Compare one active raster, static preview, and LAB-04 checkpoints. |

The evaluator should identify the **limiting resource and exact path** for each miss, then rerun only the changed path against the same paired firmware/server fixture. If mid-message resume is required and replay-to-position cannot meet the measured wait and memory envelope off the UI thread, LAB-04 must compare checkpoints before choosing a format.

## LAB-04 — color sketch format and preview strategies

**Run:** Encode the same fixture set as timed colored points, timed stroke/segment records with color per stroke, and a segment stream plus a small final-image preview. If mid-message resume or seek remains in scope, add sparse checkpoints and seek to early/middle/late timestamps. Render all variants at the owner-decided **120×120 logical size**, with 240×240 capture mapping in the capture portion of the lab.

**Capture:** Encoded and compressed bytes, max capture RAM, encode/decode time and allocations, raster time per audio interval, restart/seek time, preview bytes and display time, visual fidelity (corners, color changes, timing, background-color strokes), malformed/oversized input behavior, and compatibility handling for legacy FLSK1.

**Result sought:** A versioned, color-capable format that preserves the [sketch experience](../product/experiences/sketch-record-and-playback.md#6-requirements) within measured memory and frame budgets. **Re-evaluate** if compression changes timing or geometry, a dense drawing exceeds a bounded work interval, preview generation duplicates a large full-screen buffer, a checkpoint costs more memory than its resume benefit, or a format cap silently truncates user drawing. Choose no-seek, preview-only, or checkpoints from these results and the product's resume decision.

## LAB-05 — transition and simultaneous-resource peak

**Run:** Use an h19-style persistent root to enter/leave a carousel with one active sketch image. Repeat while audio is stopping, an inbox page is applying, and a second card image is being prepared. Compare with full scene replacement only as a measured control.

**Capture:** Old/new tree overlap, LVGL pool high-water and largest free block, PSRAM/internal largest block, temporary image copies, animation frame time, and attempts to access invalidated handles.

**Result sought:** Stage motion and media preparation fit together with a defined failure path. **Re-evaluate** if a steady-state run fits but the transition cannot allocate its largest buffer, an animation causes a white/partial frame, or a callback reaches a destroyed card.

## Decision record after the runs

Record one decision row for each of these questions, with a trace or measurement link and the relevant product approval where behavior changes:

| Decision | Possible outcomes |
|---|---|
| Inbox paging and retention | Page size, metadata window, older-page UX, total retention policy; keep server retention separate from device view budget. |
| Card controller | Number of live views, binding-generation rule, media-cache capacity, and behavior when focus leaves a loaded page. |
| Playback switching | Stop versus pause on focus change, cancellation and late-result policy, one active audio operation, and inactive-card image. |
| Resume/seek | Start from zero, resume after off-thread reconstruction, or checkpoints; align with persisted `position_ms`. |
| Sketch format | Version, color/segment representation, final preview, limits, validation, and legacy compatibility. |
| Resource gates | Approved frame/callback budgets, node and byte caps, largest-block margin, failure behavior, and target verification level. |

Do not promote a demo PASS marker, static estimate, or clean build into a target-performance claim. Close each concern with a measured result or leave the design decision **OPEN**.
