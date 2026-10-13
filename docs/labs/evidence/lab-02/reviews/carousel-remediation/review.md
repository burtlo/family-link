# LAB-02 carousel standards review (temporary labels)

## 1. Review boundary

Read-only fresh standards review of `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox`, following worker calls into the fixture server and host checks. Branch `main`; HEAD/base `7ecc594861c1e6fbc9ad647ac591bb44df3acde9`. Current tracked, unstaged, and relevant untracked contents are included. `source-sha256.json`, `working-tree.diff`, `status.txt`, `head.txt`, and `branch.txt` identify the actual reviewed source. `end-source-check.json` confirms no hashed source changed during review. The supplied `docs/labs/evidence/lab-02/reviews/carousel-remediation/baseline-source.json` and `baseline-working-tree.diff` are additional parent baseline evidence, not substitutes for this review's hashes. No source or maintained documentation was edited. Existing `wifi.c` and `lab_harness.c` WIP was preserved. No hardware or flash was run. Labels below are temporary; root assigns final IDs.

## 2. Applicable sources

All paths are under `/Users/lynnfrank/src/family-link`; exact SHA-256 revisions for files inspected are in `source-sha256.json` (HEAD alone does not identify WIP).

| Source | Authority and requirements | Connection |
|---|---|---|
| `.agents/skills/client-code-review/SKILL.md`, `docs/standards/client-code-review-agent.md` | Canonical review workflow; full standard, independent reviewer, complete finding fields, L0–L3 | This review and artifacts |
| `docs/AGENTS.md`, `docs/README.md` | Active sources and archive boundary | Product authority, findable specifications |
| `docs/standards/client-application-coding-standards.md` | Normative §§1–15 | Presentation, session, I/O, operations, resource and lifecycle paths |
| `docs/labs/lab-02-carousel-remediation-plan.md` | Draft implementation plan; parent says owner authorized review/implement/verify. CAR requirements are review criteria within assigned lab scope | CAR-ARCH/GESTURE/BIND/PAGING/INSERT-REMOVE/CONTRACT/BUDGET/OBS; C0–C6 |
| `docs/labs/lab-02-workflow-remediation-plan.md` | Recorded explicit O-01–09 owner decisions | Managed HTTP default, O-03 free/loading traversal, O-04 viewed-ID preservation and nearest-neighbor removal |
| Owner decisions relayed directly by parent during review | LAB-02 only: loading `Loading message…`; error `Message unavailable`, tap to retry; swipes may traverse loading/failed cards and fetch new focus; mutation visual reconciliation defers to snap completion/supersession | Closes carousel OPEN 1–4 only; no product-wide approval |
| `docs/labs/lab-02-implementation-outline.md`, `docs/labs/inbox-carousel-playback-evaluation.md`, `docs/labs/assistant-guide.md` | Lab plan, scenarios and run workflow; outline's planning-only status is stale relative to existing code | 3 recycled views; local and HTTP pages; eager 16 comparison; scenario ladder |
| `docs/labs/lab-02-session-architecture.md` | As-built/explanatory; experimental policies are not product approval | Single writer, bounded resources, supposed 1-second deferral; conflict with actual unbounded deferral and new owner retry decision |
| Target README, manifest, tests README, fixtures README; server README/manifest | Implementation navigation and experimental fixture subset | Firmware/test/server provenance; README related-doc links resolve incorrectly; CAR/session/owner links are not findable there |
| `docs/v1-assessments/v2-implementation-practices.md` and focused async-operation/UI-update/session-poller/operational-contract/resource-budget guides | Implementation guidance; historical v1 failures are not target measurements or mandatory architecture | Reviewed envelope, stage policy, identity, bounded work and evidence gates |
| `docs/product/communication-contracts/inbox-and-media.md` | V1 evidence plus proposed v2 schema; explicitly OPEN | Stable identity, revision/page validity guidance; lab numeric cursor/16-item subset does not approve product cursor policy |
| Prior `application-followup.md` and `execution.md` under workflow-remediation evidence | Historical physical workflow observations | Manual PASS/UNMEASURED, scripted FAIL/UNMEASURED/inbox_not_ready; no root cause or current-WIP failure inferred |

No approved message-card experience or device screen specification was located through target links. The lab outline explicitly uses stub message cards, not roster user-card components. Numeric frame/input/memory/blank-duration thresholds and whether three views match device geometry remain OPEN. No geometry or performance threshold is invented here. New owner mutation decision permits deferred **visual** reconciliation; it does not justify stopping ordinary successful page merges or transport events behind a gesture.

## 3. Ownership / wait map

`LVGL scroll begin/end or harness set_focus → carousel_controller → session intent_focus/scroll_lock → session enqueue_fetch (zero-wait semaphore + queue) → HTTP worker (queue receive, network, cJSON parse) → mutex-protected event copy → session_pump under LVGL lock → model/revision/focus reconcile → controller property changes + three-card pool rebind`.

Boot worker owns Wi-Fi join and initial fetch; Wi-Fi event task reports transport events. Session reducer owns ordinary model/focus/stage writes. Presentation runs on LVGL callbacks and pump task holding board LVGL lock. It processes at most one fetch completion per pump, copies at most 16 items, and renders three views. Queues: eight requests and eight full-size completion/stage/transport events, with a counting semaphore limiting submitted requests; one normal HTTP worker, separate boot worker. Network open/read calls have 3-second call timeout and read-loop 12-second deadline; actual whole-attempt duration is not a single deadline. Parsing and network are off presentation. Static model is at most 256 summaries; response cap 16 KiB. Status updates are Level 0, card changes Level 1, initial scene creation is the only scene load.

Exceptions: queue-drop worker clears session-owned inflight state; posting serializes on an unbounded mutex from producers (including Wi-Fi system task); page success at queue head waits until `scroll_locked` clears; presentation trace calls synchronous `printf`; worker obtains page size from mutable session rather than request. Harness snapshots outside lock and retaining sender pointers after lock are plausible races, not proven target failures here. No teardown/re-entry path exists in the ordinary lab boot journey, so stale-handle risks on hypothetical repeated init are not asserted as current defects.

## 4. Findings

Confirmed source-level defects first. Runtime magnitude/physical visibility remains unverified unless noted. Every path below is absolute at the reviewed hash state.

### LAB02CAR-F001 — P1 confirmed: in-range missing data exposes previous message labels

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/card_pool.c:51–55`; associated `card_view.c:100–104,125–142`.

**Trigger:** Bind a slot to a valid message, then move focus to an index whose page is not materialized (delayed/failed HTTP, or a new revision clearing pages). `card_view_bind(NULL)` calls unbind, which clears bound identity but keeps sender/preview/duration labels; pool then removes HIDDEN because pending_bind is false and positions it at the new index.

**Effect:** An in-range unavailable message can visibly display the previous message's content. Fresh slots show generic empty/zero-duration placeholder rather than declared loading/error state. No bind trace identifies the visible old labels.

**Requirement:** CAR-GESTURE-1, CAR-BIND-3/4, CAR-PAGING-3/4, CAR-CONTRACT; standards §§5/7; new owner loading/error text.

**Smallest coherent fix:** Explicit card presentation state and atomic content clearing on generation change. Distinguish out-of-range unbound from in-range loading/error; pool visibility follows state, not pending_bind. Keep stable surfaces during slow binds (`card_view.c:109–115` currently deliberately hides them).

**Verify:** Rebind populated slot to unavailable page, delayed result, failure, retry and late prior-generation completion; assert no old labels and stable surfaces with fixed view count. Required L1 state tests, L2 rendered path, L3 synchronized screen/trace for visual closure.

### LAB02CAR-F002 — P1 confirmed: traversal leaves the viewport outside the static three-card window

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/carousel_controller.c:137–151` (event registration `193–194`).

**Trigger:** Start at focus 0, drag or fling several card widths across an inbox of 8/16/64/256. Only SCROLL_BEGIN and SCROLL_END are handled; begin sets lock, but no SCROLL update estimates focus or moves/rebinds the pool. The three surfaces remain at the previous focus and neighbors until end/snap reconciliation.

**Effect:** The logical spacer exposes a region with no card surface during fast traversal, even with fully local data. Fetch prefetch also waits for end focus. Pool size by itself does not virtualize the visible window continuously.

**Requirement:** CAR-GESTURE-1/3/4, CAR-BIND-1; C2/C3, standards §§2/5.

**Smallest coherent fix:** Bounded/coalesced scroll focus intent and viewport-window recycling throughout the gesture, keyed to a coherent session snapshot; no I/O wait or scene rebuild. Geometry sufficiency remains owner OPEN and needs measurement.

**Verify:** Fast multi-page traversal while dragging with local and delayed HTTP; check surfaces and focus/window trace continuously, final focus intent preserved, constant live-view count. L1 geometry/window tests, L2 scripted rendering, L3 input/visual/timing closure. LAB02CAR-F001 is a practical prerequisite to truthful missing-data surfaces.

### LAB02CAR-F003 — P1 confirmed: reversal is discarded and superseded snaps lack terminal outcomes

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/carousel_controller.c:133–135`; `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/snap.c:65–76`.

**Trigger:** Reverse touch while the custom snap is active, or programmatically start a second animated focus before the first completes. Controller drops all scroll events while snap lock is set. A replacement deletes the old LVGL animation and overwrites global callback/target without a stale/cancel terminal for the old snap; no completion identity comparison exists.

**Effect:** Reversal cannot supersede the old target through the controller; old target continues to govern reconciliation. Programmatic replacement leaves the first accepted snap unterminated in telemetry. Current ID is global, not immutable callback context. Same-position path calls done without assigning/start tracing a new identity.

**Requirement:** CAR-GESTURE-5, CAR-BIND-5, CAR-OBS; standards §4.

**Smallest coherent fix:** Explicit snap envelope and immutable completion context; reverse intent cancels/supersedes current snap nonblockingly, emits one stale terminal, and completion rejects obsolete identity.

**Verify:** Reverse before completion, replace twice, no-motion snap, and invoke stale completion; original operation terminal exactly once, latest focus survives. L1 operation tests, L2 actual animation path, L3 touch/lifecycle closure. Coordinate with LAB02CAR-F002, but shared controller file alone is not a dependency.

### LAB02CAR-F004 — P1 confirmed: ordinary page success and following events wait indefinitely for gesture unlock

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/lab02_session.c:537–545`.

**Trigger:** Deliver a successful fetch while scroll lock is true; keep gesture active. Peek leaves that event at head, and pump breaks on every call. Transport/failure events behind it cannot be consumed. No elapsed-time bound implements the architecture note's claimed one second.

**Effect:** Materialization, terminal apply and loading presentation wait for drag/snap end. Transport truth may stay stale; producer bursts fill the same event queue. Existing native test explicitly expects this behavior, so its pass is counterevidence to a current merge-during-snap claim.

**Requirement:** CAR-GESTURE-2, CAR-PAGING-2/3; standards §§3/4/6. Owner permits mutation visual reconciliation at snap terminal, not ordinary page-result head-of-line blocking.

**Smallest coherent fix:** Consume and merge ordinary pages in bounded session steps during motion; defer only identity-changing visual mutation reconciliation using a separate immutable pending snapshot, rather than blocking the event queue.

**Verify:** Hold drag past one second, success then transport failure and another completion; model updates and ordinary terminals proceed while viewport/focus stay stable. Mutation arrives during snap and visibly reconciles only at terminal/supersession. L1 reducer plus L2 animation path; L3 timing/visual closure. Enables safe LAB02CAR-F002 streaming; queue reliability LAB02CAR-F005 should be established first.

### LAB02CAR-F005 — P1 confirmed: event overflow drops accepted terminal results and mutates session state from worker

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/lab02_session.c:667–672` (also 650–655; common event drop 92–99).

**Trigger:** Saturate eight event slots using transport/stage/completion burst, then post an accepted fetch terminal. Particularly reachable while LAB02CAR-F004 pins a success at head. Posting fails, increments a generic counter, and worker writes inflight entry to zero then releases request slot.

**Effect:** No terminal reaches session/UI for that operation; failed-page state/copy is not applied. Initial terminal drop can leave initial stage fetching forever. Worker violates single-writer ownership and can clear a newer operation after concurrent revision invalidation/re-enqueue because it never checks current op identity at this write.

**Requirement:** CAR-ARCH-2/3, CAR-PAGING-1, CAR-GESTURE-4; standards §§2/3/4 (observable overflow alone is insufficient if state-changing completion is lost).

**Smallest coherent fix:** Guaranteed bounded owner reconciliation for non-droppable terminals (reserved terminal capacity/mailbox/admission), coalesce droppable transport freshness; no worker writes to session domain bookkeeping. Record terminal identity/reason when rejecting/superseding.

**Verify:** Actual C reducer queue full with initial and normal terminal, transport bursts, newer op created while older terminal posts; one owner/one terminal, no stuck stage/no newer op cleared. L1 fault/race tests minimum; L2 queue-pressure runtime and L3 concurrency closure.

### LAB02CAR-F006 — P2 confirmed: focused-page failures retry indefinitely rather than owner tap-to-retry

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/lab02_session.c:560–569`; controller error copy `81–85`.

**Trigger:** Focus an unloaded page while server repeatedly fails/timeouts. Pump retries every second indefinitely while page missing, with new op IDs and no maximum attempts/stopping rule. There is no failed-card retry input path.

**Effect:** Repeated network attempts continue without owner-directed retry; label says `page unavailable; retrying` instead of the chosen failed-card interaction. Stage failures do terminate each attempt, but the logical loading/retry policy never stops.

**Requirement:** CAR-PAGING-7, CAR-CONTRACT; standards §§6/7; new owner LAB-only failure/tap-retry decision.

**Smallest coherent fix:** Terminal page error state with owner text and nonblocking explicit tap retry; preserve valid neighbors and allow swiping/fetch of new focus. If any automatic attempts remain, declare finite attempt/backoff policy rather than inventing product policy.

**Verify:** Persistent failure, tap retry creating a fresh attempt, double tap coalescing, swipe past failed page, success after retry and stale previous completion. L1 contract tests plus L2 interaction and L3 device error rendering.

### LAB02CAR-F007 — P2 confirmed: worker-allocation failure has no usable terminal page/boot state

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/page_provider_http.c:221–224,261–265`; stage eligibility in `lab02_session.c:108–127`.

**Trigger:** Boot-worker xTaskCreate fails under resource pressure. It posts ERROR from SHELL, but stage_allowed permits ERROR only from CONNECTING/FETCHING/READY. Separately, background-worker creation fails after initial page READY; notification only logs and returns, then initial prefetch queues requests without a consumer.

**Effect:** Boot stays nonterminal loading shell; subsequent pages can remain pending forever despite a terminal allocation failure. Worker-spawn log does not reconcile availability/UI.

**Requirement:** CAR-BUDGET allocation-failure clause, CAR-CONTRACT; standards §§4/7/8.

**Smallest coherent fix:** Owner-handled resource-failure event from every permitted startup stage, and submit pages only with usable worker/admission state; map failure to explicit error surface and cancel/terminal pending operations.

**Verify:** Inject both xTaskCreate failures, confirm terminal owner state, no undrainable requests, neighboring content retained and no false HTTP-ready claim. L1 failure tests then L2 runtime; L3 allocation/stack evidence.

### LAB02CAR-R001 — P2 plausible risk: incomplete frozen fetch envelope and cross-owner reads

**Location:** `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/include/lab02/lab02_session_io.h:14–19`; `page_provider_http.c:43–46,60–63,147–153`.

Request contains ID/session/page/submitted time but no immutable revision, page size/offset, attempt, cancellation or deadline. Worker rereads session page size twice. Current ordinary lab page size is effectively constant 16, and session/op/revision checks do reject many stale results; no wrong-offset failure from a reachable ordinary run was proven. Reset/re-entry is not currently an exposed user flow. Treat missing envelope guarantees as a C1 contract/evidence gap and plausible future race, not proof that current code mutates wrong content this way. Freeze request offset/size/revision/attempt and define cancellation/timeout before relying on those claims; verify supersession/revision/session cases at L1 and actual worker L2. No speculative fix scope from this risk alone.

OPEN choices: final numeric budgets and window geometry remain OPEN, as do product-wide paging/cursor/offline policy, initial boot automatic retry/copy beyond already approved lab behavior, and removal tie direction when both neighbors are equally near. O-04 governs nearest remaining but does not decide that tie.

## 5. Residual risks

Synchronous multi-call trace printf from LVGL-locked callbacks may block on serial and lines from workers may interleave; no target timing/interleaving proof collected, so do not call it a measured latency failure. Harness reads snapshot/count outside presentation lock and keeps sender pointer after unlock; this is a plausible data race under page mutation, separate from current no-hardware review. New revision clears old model before pages refetch; ordinary single insertion/removal identity search in adjacent pages has counterevidence of eventual ID preservation, but arbitrary displacements beyond neighbors and focused-unmaterialized mutation policy require deterministic scenarios. Server push appends (does not insert at head); no server receipt proves visible preservation. Eager path is local-only, max16 and no recycle instance for automated harness; comparison coverage must acknowledge this rather than assume HTTP/eager scripted parity. Worker startup lifecycle globals also are not reset on reinit, but ordinary app initializes once; no current reinit defect claimed.

## 6. Evidence gaps

* C0 lacks deterministic rendered reproductions and all required gesture/snap/window/revision/terminal trace fields. Native tests compile reducer/parser, not controller/card/LVGL paths. Neither a clean compile nor old W8 failure closes this.
* C1 lacks queue-terminal saturation/initial-drop, allowed/rejected stage, cancellation/exactly-once, real timeout/stale snap cases. Existing tests favor deferral and must change with the approved new contract; Python models are supplemental.
* C2–C4 lack rendered loading/error/reversal/page-during-snap and managed mutation visible effect proof. Page apply trace is not itself a bind/window proof. CAR-BIND has no explicit loading/error/stale state or atomic revision/binding snapshot today.
* C5 lacks declared transient/persistent bytes, real live-node counts, concurrent/queue high-water, stack high-water, largest free block, callback p50/p95/p99/worst and visual screenshots/video for 8/16/64/256. CAR-OBS missing session/stage/attempt/terminal/binding fields cannot correlate every operation. Required metrics schema change must be reviewed, not silently extended.
* C6 has no current-WIP target confirmation, no matching eager/recycle comparison, and no required scenario matrix closure. Previous L3 availability failure proves only its saved historical run.
* Target README relative links to outline/assistant-guide are broken (resolve under client/docs) and omit applicable active CAR/session/owner sources. Fix traceability while implementing; this is an evidence gap, not a defect ID.
* Initial fixture-server check could not bind loopback in sandbox (PermissionError); authorized allowed-loopback rerun passed all 3 existing paging-server checks. No hardware attempt made.

## 7. Commands and results

Exact invoked focused command and raw output: `commands.txt`, `focused-checks.txt`, `server-start.txt`. `python3 -m unittest scripts.tests.test_lab02_model_native scripts.tests.test_lab02_host scripts.tests.test_lab02_architecture scripts.tests.test_lab02_paging_server -v`: 20 individual native/host/architecture tests passed; paging-server class setUp failed, combined result exit1. Direct server startup confirmed socket bind denied by sandbox, not a demonstrated source defect. A single allowed-loopback rerun of `python3 -m unittest scripts.tests.test_lab02_paging_server -v` passed all 3 checks, exit0 (`paging-checks-allowed.txt`). Native compile uses actual parser/session/CJSON with `-Wall -Wextra -Werror`; no native tests skipped. No new implementation tests, build/flash/device runs, private config reads or edits. Inspection used cat, rg, nl and raw source hashing; all relevant source remained byte-identical through end snapshot.

## 8. Evidence levels

| Claim | Achieved evidence | Artifact / limit |
|---|---|---|
| Parser/session existing tested cases | L1 (including native compile L0 for those compiled units) | focused-checks.txt; actual C parser/reducer, not firmware build |
| Current rendering/snap/queue-drop defects LAB02CAR-F001–G | Static reachable code traces, not a reproduced L2/L3 defect | Reviewed exact file hashes/lines; actual target manifestation remains to verify |
| Fixture page/receipt/idempotent mutation cases | L2 loopback runtime for 3 existing tests | paging-checks-allowed.txt; no device visible effect inference |
| Current device timing/memory/input/visual lifecycle | No L2/L3 achieved | C2–C6 gaps; no waiver |
| Old W8 availability/manual workflow | Historical L3 only | Saved application-followup/execution; no current cause inference |

## 9. Execution plan (advisory to root)

1. Freeze contract and reproducible C0 trace/state baselines. Owner text/traversal/mutation choices now supplied; numeric budgets/geometry still require explicit targets and measurements, not approval by reviewer. Add characterization before concurrency changes (§12).
2. C1: LAB02CAR-F005 reliable owner terminal reconciliation and LAB02CAR-F007 allocation terminal state (separate coherent work units); implement request/snap envelope invariants and tests. LAB02CAR-F007 is independent of LAB02CAR-F005; order is reliability impact, not shared-file dependency.
3. C2: LAB02CAR-F001 card state/content clearing/stable surfaces. It supplies truthful placeholders for later continuous recycling.
4. C3: LAB02CAR-F004 consume ordinary pages without motion head-of-line blocking, retaining approved deferred mutation visual snapshot. LAB02CAR-F005 is prerequisite because bounded queue/terminal behavior must survive moving application timing.
5. C3: LAB02CAR-F002 continuous bounded viewport recycling uses LAB02CAR-F001 presentation states and LAB02CAR-F004 streaming snapshots. LAB02CAR-F003 snap supersession can be separate after envelope characterization; coordinate final integrated gesture tests with LAB02CAR-F002. It is not causally dependent on LAB02CAR-F002 merely because both touch controller.
6. LAB02CAR-F006 failure/tap-retry contract uses LAB02CAR-F001 explicit error surface; reducer policy work can proceed independently after C1.
7. C4 independently test viewed-ID preservation/nearest removal and coherent deferred mutation visible snapshots; no verified current push/removal defect is invented from missing L2 evidence. Any new proven failure gets its own finding.
8. C5 complete traces/schema/resource gates and integrated checks; C6 actual managed scenarios/eager comparison only after independent sequential verification of implementations and target availability. At every unit preserve WIP, freeze exact diff/hash, obtain separate fresh verifier; no L3 defect closure from L1 alone.

No single dependency chain is warranted. Dependencies above are behavioral prerequisites; unrelated units have no dependency ordering required and can be executed sequentially by severity/impact. Missing target access leads unverified status, not a reviewer-granted waiver. LAB02CAR-R001 remains proof/contract work rather than speculative defect repair.

## 10. Outcomes

Review only; no fixes attempted. LAB02CAR-F001–LAB02CAR-F007 are confirmed source-level findings awaiting root final IDs and independently verified remediation. LAB02CAR-R001 is plausible risk, not confirmed wrong-offset behavior. C0–C6 evidence gaps and owner OPEN decisions remain explicit. Existing WIP preserved and unchanged; initial socket-denied check and successful allowed-loopback rerun recorded separately from production behavior. Review artifacts are local under `/tmp/lab02-carousel-review`; root may consolidate/save them under approved lab review evidence.

## C3a / LAB02CAR-F004 implementation handoff (2026-10-13)

Implemented and awaiting fresh independent verification. The baseline native
suite and actual headless LVGL reproduced the old motion deferral expectations.
Ordinary pages now apply and fetch-terminalize during motion; one static immutable
pending page-zero mutation releases its delivery admission and reconciles on
unlock against the latest owner focus. Pending/applied revisions and normalized
pending/supersession traces are separate. APP-O-02/03 remain preserved.

Self-checks: L1 native session/terminal/startup regression PASS, including held
input >1 second, success followed by timeouts/failure/transport, newest pending
revision, stale session reset and late old producer. L2 actual LVGL normal and
120 ms delayed binding PASS, with unchanged viewport and card roots, loading
converted in place, actual animation page arrival, append/removal identity and
stale scenarios. L0 actual HTTP scripted ESP32-S3 firmware compile PASS. Baseline,
unit diff, raw commands/logs/screens and source hashes: `/tmp/lab02-c3a-fix/`.
The build-only escalated retry succeeded after an initial sandbox sysctl failure
and one approval-service usage rejection; no hardware was accessed.

F004 is not yet independently verified or target-confirmed. No L3 or waiver;
F002/F003/F006 remain separate. Three-view geometry is unchanged; approved
APP-O-05 four-view work belongs to C3b. APP-O-04 initial tap-to-retry belongs to
F006. C1a remains the prerequisite mechanism; C1b/C2 target gaps remain open.

## LAB02CAR-F008 — P2 confirmed: initial retry waits on worker mutex in presentation

Resume assessment [full finding and evidence](resume-memory-review/report.md)
contains the complete trigger, location, requirement, smallest coherent fix,
verification scenario and source provenance; temporary RETRY-BOUND is assigned
stable ID LAB02CAR-F008. Initial ERROR status tap calls session retry directly;
`lab02_session.c:642` takes worker-shared `s_post_mutex` with `portMAX_DELAY`.
Worker completion/stage posting can hold that mutex. Actual-session contention
probe confirms callback-representative invocation remains blocked until release.
This violates bounded nonblocking presentation requirements; no measured target
freeze or deadlock claim. Focused nonblocking/deferred admission with exactly-one
terminal semantics and review of the synchronous provider restart chain precedes
C4. Required L1 contention, L2 actual click/provider, L3 touch/timing closure.
No repair yet; no retry consumed. Original F006 mechanism evidence remains with
its acknowledged click/target gaps, but its mechanical no-blocking claim is
superseded for this initial-retry path.

## LAB02CAR-F009 — P1 confirmed: surviving viewed ID lost outside local page window

C4 [complete finding/proof](c4-proof/report.md) temporary C4-SURVIVING-ANCHOR
is assigned stable LAB02CAR-F009. After100 preceding removals, m200 survives at
index100 in revision101/total156. Local-neighbor reconciliation instead commits
m255/index155, then loading page6 containing m200 does not restore the anchor.
Actual production controller/session/model and vendored LVGL reproduce this.
Requires bounded stable-ID resolution without treating unmaterialized absence
as removal; CAR-PAGING5/INSERT-REMOVE1–3/BIND6, standard5. No fix attempted yet;
nominal append and adjacent/last removal remain counterevidence in full report.

## LAB02CAR-F010 — P2 confirmed: startup board status overlays ready cards

Owner observes persistent loading-inbox text/missing-glyph over cards, including
ready/cached content. Startup `lab_main.c:125` creates centered board-status
label via `board.c:43–60`; no success clear exists. Separate carousel header
also emits loading-inbox copy. Owner explicitly requests removing this message.
Smallest coherent fix removes startup loading board label and loading-inbox
header text; preserve existing retryable unavailable behavior. Verify startup
source paths and normal/error header behavior; target confirmation remains
required for physical overlay claim. Prioritize this small requested fix before
F009 identity implementation.
