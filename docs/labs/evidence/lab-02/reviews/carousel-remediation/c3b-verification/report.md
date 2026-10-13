# C3b independent verification / LAB02CAR-F002

## Outcome

**F002 mechanism verified fixed at L1/L2; overall visual/input/timing closure remains unverified pending L3.** No demonstrated C3b regression, no focused repair needed, no evidence waiver. F003 reversal/snap terminal defect and F006 failed-page retry remain separate open units.

Fresh read-only verifier `/root/c3b_window_verify` inspected the complete original finding and frozen implementation, independently reran original baseline and current runtime checks, and added a temporary adversarial harness. No implementation or maintained documentation was edited; outputs exist only under `/tmp/lab02-c3b-verify/`.

## Boundary and authority

Target: `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/`, following its production session/model/controller/card/snap paths and relevant host gates. Branch main, HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9`, including preserved WIP. Fix diff/provenance: `/tmp/lab02-c3b-fix/unit-byte.diff`, `unit.patch`, `before/`, `before-sha256.json`, `after-sha256.json` and saved packet under `docs/labs/evidence/lab-02/reviews/carousel-remediation/c3b-implementation/`.

Read full skill and canonical review workflow, docs/AGENTS, full client coding standard, original review, execution decisions and applicable carousel requirements; inspected README/manifest/session architecture and v2 implementation practices. No applicable root or client AGENTS exists. CAR-GESTURE-1/3/4, CAR-BIND-1, standards §§2/5/8/11 govern F002. APP-O-05 explicitly authorizes 320px viewport, 132px cards, 12px gaps, FOUR recycled roots, superseding the draft three-view premise. APP-O-01 permits traversing loading/error cards; APP-O-02 defers identity-changing mutation visual reconciliation during snap. C1a/C2/C3a mechanism prerequisites were independently accepted at L1/L2 in the supplied execution record; their target gaps remain distinct. No product numeric performance budget or target waiver is inferred.

## Complete finding and causal assessment

Original reachable defect: start with focus 0 (characterization uses populated focus 4), drag/fling multiple card widths through 8/16/64/256; old controller handles BEGIN/END only, leaving static focus-neighbor roots behind until reconciliation. This can leave no surfaces even with local materialized data, and delays fetch focus until end.

The new production SCROLL callback always recycles against actual viewport displacement, including custom animation while snap retains target focus. Window math uses half-open intersections: card `[12+144*i, 144+144*i)` intersects viewport `[x,x+320)` iff left < x+320 and right > x. Four roots suffice for at most four intersections. At x566 indices 3/4/5/6 intersect by 10/132/132/10px. The pool retains overlapping index/session/revision/ID/state in the same root and generation/timer; only vacated roots recycle. Styling remains Level 0 and content replacement bounded Level 1; no new scene/scroll/root allocation is introduced in displacement callbacks.

Presentation has one latest pending focus with a 20ms submission interval and immediate END flush; session remains domain focus writer. Animation displacement does not overwrite its intended target. Session separately retains one latest unadmitted focused page, prioritizes it before speculative neighbors, and retries zero-timeout admission at most once per 20ms even while locked; admitted/materialized/failed/superseded pages stop this admission path. This retry is admission recovery, not F006 failed-fetch policy. Loops are bounded by four roots; no I/O waits/sleeps were added to callbacks. Existing synchronous trace output still requires target timing measurement and was already an explicit residual risk.

## Independent commands and results

Exact commands are in `commands.json`; runtime CMake/compiler/executable commands and source manifests are in `normal/commands.json`, `slow/commands.json` and associated runtime files.

1. Re-executed preserved pre-C3b baseline binary from `/tmp/lab02-c3b-fix/baseline/runtime.json`: fast-boundary PASS characterization, original `drag20` frame contains **zero** card-background pixels. `baseline/reproduced.log` and PPMs retain independent execution. Its input manifest and before snapshots identify the old controller/pool; old behavior was not inferred from current tests.
2. `python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c3b-verify/normal`: all **11 scenarios PASS**, including local traversal, missing multi-page jump, held-input reversal, final END focus20, four-partial x566, page during actual animation, mutation deferral, stale delivery, failed neighbor/traversal and 8/16/64/256 pool sweep. Current normal `drag20` contains **37,861** card-background pixels.
3. Same command with `--slow-bind-ms 120` and slow artifact directory: all **12 scenarios PASS** including slow-bind generation/session rejection and surviving binding. Every animation advance tick checks viewport intersections; card roots stay four, card nodes stay 20 and scroll children stay 5 (spacer plus roots), independent of inbox size. Scene and scroll handles remain unchanged.
4. `LAB02_TERMINAL_ARTIFACT_DIR=/tmp/lab02-c3b-verify/native LAB02_HTTP_STARTUP_ARTIFACT_DIR=/tmp/lab02-c3b-verify/startup python3 -m unittest scripts.tests.test_lab02_model_native scripts.tests.test_lab02_architecture -v`: **seven gates PASS, no skips**. Inspected full/busy admission cases: owner final focus 176 remains during lock; only latest page 11 is retained and admitted after capacity/mutex availability returns, without duplicate request after ten further pumps. Terminal/startup/mutation regressions also pass.
5. Independent adversarial temporary harness, compiled against frozen production code and actual LVGL with 120ms delayed binding: forward and reverse **every integer pixel** over full allowed scroll extent for counts1/2/3/4/8/64/256. It checks half-open coverage plus actual LVGL root coordinates, four fixed root pointers and generation/timer pointer identity for surviving indices. **93,414 displacements and 291,756 preserved-survivor checks PASS**. Thus exact card-left/card-right boundaries, narrow short inboxes, one-pixel four-card intersections and reverse traversal are independently tested beyond the fixer's sampled offsets. Commands/log/source: `adversarial/`, `adversarial-results.log`, `adversarial-configure.log`, `adversarial-build.log`. This is deterministic adapter/runtime evidence, not target callback timing.
6. Reviewed frozen final HTTP scripted ESP32-S3 build log `/tmp/lab02-c3b-fix/firmware-build-final.log`: successful final build includes final source refinement. Per assignment, did not repeat firmware build without a new concern. Native/runtime compilation independently passed.

Visually inspected actual rendered `normal/four-partial.png`: four surfaces, narrow outer parts and correct local sender content. Inspected `slow/drag20.png`: continuous loading surfaces and no stale prior preview. Raw PPM frames retain renderer evidence. Selected frames alone are supplemented by the intersection/coordinate assertions at every tested displacement.

## Provenance and preservation

`provenance-result.json` records all **15** fixer after hashes matching frozen current source, **1,005** verification-boundary files unchanged before/after, all **6,629** production/runtime input hashes still matching, identical git status and byte-identical tracked WIP diff throughout verification. Independently compared original fixer before-WIP diff segments outside unit files to current diff: all 13 pre-existing segments identical, including Make/host/Wi-Fi/harness/provider/prior card-view and session contracts. `before-sha256.json`, `after-sha256.json`, `before-status.txt`, `before-working-tree.diff` retain source provenance. The temporary adversarial variant changes only the test harness and CMake source path under `/tmp`; production source remains frozen.

## Evidence levels and limits

| Claim | Outcome | Evidence |
|---|---|---|
| Compilation/static ownership gates | L0 PASS | Independent native/runtime builds; assessed final firmware build |
| Focus/latest-page full/busy admission, terminal and owner boundaries | L1 PASS | Seven native/architecture gates and raw terminal logs |
| Original blank-viewport mechanism, geometry/window/preservation, animation/page/mutation regressions | L2 PASS | Actual production paths with vendored LVGL, normal/120ms runs, baseline and independent pixel-boundary harness |
| Physical touch, device visual continuity, callback p50/p95/p99/worst, stack/heap/fragmentation and FreeRTOS scheduling | L3 NOT ACHIEVED | No target run, flash, synchronized physical capture or owner observation |

HTTP provider initialization and transport, RTOS queues/semaphores/tasks and timer clock use explicit host adapters. The renderer and production controller/pool/card/snap/session/model are actual C. Continuous assertions validate dispatched displacement and sampled animation ticks, not unseen physical input frames. No live-server/target/eager parity claim is made. C5/C6 still need representative BOX-3 input, visual and resource/timing evidence. Original F003 reversal continues governing old snap target in its separate characterization; F006 failed-page retry behavior remains separate. These outcomes do not close either finding or the whole carousel plan.
