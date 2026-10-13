# C3c independent verification / LAB02CAR-F003

## Disposition

F003 snap supersession mechanism is **verified fixed at L1/L2** in the frozen host production paths. **Overall finding closure remains unverified pending L3** touch/timing/lifecycle confirmation. No waiver exists. No demonstrated implementation regression was found. F006 retry and R001/speculative identity concerns remain separate; no subsequent work or repair is authorized by this report.

## Boundary and authority

Fresh read-only verifier `/root/c3c_snap_verify`, distinct from fixer; target `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox`. Main HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9` plus preserved user WIP and C0/C1a/C1b/C2/C3a/C3b/C3c contents. No implementation/maintained-document edits, reset, stash, commit, flash, hardware action, host/server/config change. All verifier harness files are under `/tmp/lab02-c3c-verify`.

Read full skill and canonical workflow, docs/AGENTS/index/full client coding standard, original complete F003 at review.md lines 68–80, execution.md, carousel plan, target README/manifest, session architecture and v2 implementation practices. Inspected full exact unit-byte.diff and equivalent normalized patch, snap/controller source, affected nearby callers and actual vendored LVGL animation implementation. Requirements: CAR-GESTURE-5 (reversal/supersession), CAR-BIND-5 (stale rejection), CAR-OBS (identity/terminal correlation), standards §§2/4/5/8/11. Plan remains draft as product policy; explicit APP-O-01/02/03/05 govern truthful loading/error traversal, deferred mutation until terminal/supersession, next/previous-if-last removal, and four views with 320/132/12 geometry. Numeric product gates remain OPEN. C1a/C2/C3a/C3b previous independent L1/L2 evidence is recorded in execution.md; their L3 gaps are preserved.

## Provenance and preservation

`before-sha256.json` and `after-sha256.json` identify 2,016 tracked/untracked repository files: zero changed. Git status and full HEAD diff are byte-identical before/after. All 1,019 implementer frozen after hashes match current bytes. `preservation.json` records these comparisons. Actual normal/slow runtime source manifests and compiler/build identities are saved separately. Production code remained frozen throughout verification. Temporary baseline reconstructed from exact preserved pre-C3c snap.c/controller.c/snap.h/harness; baseline used the same actual LVGL library independently built for adversarial verification and current unaffected production components. CMake/include adapters are explicit under baseline/ and adversarial/.

## Original failure reproduction

Independently rebuilt preserved pre-C3c production paths, then ran reversal. `baseline/run.log` reproduces original defect: raw reverse displacement reaches index2/x206 while owner focus remains m8; old completion restores m8/x1070. Replacement produces snap starts IDs1/2/3 but only terminals1/3 (three starts, two terminals). Characterization PASS deliberately asserts old failures, and is not a fix verdict.

## Commands and independent results

Exact primary commands are in commands.json; actual normal/slow compiler/configuration/scenario commands are in each runtime folder. Seven `scripts.tests.test_lab02_model_native` + `scripts.tests.test_lab02_architecture` gates pass, no skips. Native terminal/startup evidence retained in native/ and startup/.

`python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c3c-verify/normal`: all 11 scenarios pass.

`python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c3c-verify/slow --slow-bind-ms 120`: all 12 scenarios pass with actual delayed bind timers.

These compile/use production controller/pool/card/snap/session/model and actual LVGL software renderer. Transport, RTOS and delivery are explicit host adapters; synthetic reads are an actual LVGL POINTER indev with read callback, hit testing and gesture recognition, not the board touch driver. Scenarios include raw and pointer reversal, two animated replacements, fresh no-motion identity/start/terminal, animate=false replacement, archived actual lv_anim_t custom_exec/completed/deleted callbacks, external animation deletion, active scene deletion and session reset, ordinary page during snap, pending insertion/removal/reversal, held pages, queue pressure, failure/loading surfaces, stale revision, and pool bounds at 8/16/64/256. Repeated advance ticks retain four-root viewport coverage and binding identity checks. Every card root still has four children, yielding four roots / twenty card nodes.

Independent `snap-id-outcomes.json` pairs raw trace records by ID rather than order: 28 accepted IDs / 28 exactly-once terminals in each normal and slow configuration. Each pair preserves frozen session, focus, message ID, revision and target. Reversal alone exercises 12 IDs and completed/no_motion/reversed/superseded/animation_deleted/scroller_deleted/session_changed reasons. Archived callbacks assert unchanged viewport, owner focus, all four binding generations and terminal counts.

Existing final actual scripted HTTP ESP32-S3 firmware build evidence was assessed as L0 PASS after final source freeze. It was not repeated because no new source concern arose. Deprecation warnings are documented existing LVGL APIs. Build success is not target execution.

## Independent adversarial boundaries

Temporary actual-LVGL `adversarial/test.c` adds one compound lifecycle/latest-intent/pending-mutation scenario, independently compiled from current production files:

1. External animation deletion while insertion revision2 is pending: cancellation terminal occurs once, owner unlock applies revision2, preserves m10 at relocated index11, clears both locks and centers x for index11. Archived old callbacks remain inert.
2. Shrink to eight materialized messages, start snap toward last m7, receive pending revision4 removing m7, replace with animate=false: m6/index6 is selected as previous-if-last, revision4 is applied, viewport reaches its final target and old callbacks cannot displace/rebind it.
3. Delete active scene, reinitialize the same carousel storage in session2, start fresh snap12, then invoke old actual animation exec/completed/deleted callbacks: fresh operation remains current, viewport/focus/generations unchanged, and snap12 completes normally.

All pass with four exactly-once immutable start/terminal pairs. `adversarial/run.log` and rendered checkpoints retain evidence. Initial verifier fixture attempts failed because shrink already centered the selected target (no animation to archive) and refresh was requested after locking despite the existing contract rejecting that admission. Harness ordering was corrected to create actual motion and submit poll before input; no production code changed. A temporary configure typo used an unavailable cJSON path and was corrected to the path recorded by actual runtime commands. These are harness/setup failures, not production defects.

`pointer-reversal.png` inspection shows consistent final m0 at the edge; `remove-last.png` shows m6/index6 selected with matching previous neighbor and seven-message count. These are host renderer observations, not target visual continuity proof.

## Ownership, bounds and allocation/lifecycle assessment

Presentation input emits bounded session focus/lock intents; session remains the sole writer. A real SCROLL_BEGIN cancels old snap as stale before owner reconciliation; custom tick suppresses only synchronous generated BEGIN/END through driving_scroll, while SCROLL still renders actual geometry. Replacement intent is captured before unlock can apply pending mutation, and final target comes from reconciled focus/count. C3b latest-intent slot and 20ms admission bound remain unchanged.

One static snap envelope and one LVGL animation carry frozen identity/input. Terminal status and snap-state unlock precede terminal callback; stale terminal callback cannot rebind. Actual LVGL deletion/completion callbacks and archived callbacks are ID checked. External delete terminal callback may synchronously reconcile pending model state; the independent boundary confirms this lifecycle path. No blocking wait, polling loop, queue wait, per-tick application allocation or unbounded callback copy was introduced. Existing bounded session pump/pool work remains. Snap trace buffers are bounded at 240/160 bytes. Actual target duration/resource budgets remain unmeasured.

Allocation failure is handled in source: NULL lv_anim_start result terminalizes the accepted ID as cancelled/animation_allocation_failed and current controller completion clears session lock/reconciles/rebinds. LVGL implementation returns NULL after failed animation allocation; supplied sdkconfig disables malloc assertions. No actual allocator exhaustion injection or target pressure test was performed, and this inspection is not a new allocation-L3 claim. Existing viewport on failed motion plus unlocked owner state needs representative failure observation if allocation behavior is claimed as target verified. No undefined retry/product UX was invented.

## Evidence levels and missing closure

- L0: independent native/runtime compilation, architecture checks and assessed final actual HTTP firmware build PASS.
- L1: seven native/model/architecture gates and independent immutable terminal pairing PASS.
- L2: current 11 normal + 12 slow actual-LVGL scenarios and extra lifecycle/latest-intent/mutation boundary PASS; old original failure independently reproduced. Frozen raw logs, manifests and rendered frames retained.
- L3: **missing** representative board touch velocity/gesture arbitration, physical reversal visual continuity, driver/RTOS scheduling and lifecycle interactions, presentation p50/p95/p99/worst, stack/heap/high-water/fragmentation/resource pressure and synchronized target trace/visual evidence. Host pointer reads and headless timers do not supply these. No user-authorized waiver.

Recommendation to root: accept C3c mechanism at L1/L2, retain F003 overall unverified pending C5/C6 L3; report status then honor owner-requested pause. No repair retry is indicated by these tests. F006 and speculative concerns remain separate.
