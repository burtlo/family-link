# LAB-02 carousel evaluation and remediation execution

## Boundary

Requested target: the client carousel and model/session/paging boundary in
`client/labs/lab-02-bounded-inbox/`, evaluated against
[the carousel plan](../../../../lab-02-carousel-remediation-plan.md).
Current branch `main`, HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9`.
No comparison base requested; committed code and current relevant WIP are included.
[Baseline source hashes](baseline-source.json) and
[working-tree diff](baseline-working-tree.diff) identify the reviewed state.
Existing modifications to Wi-Fi, harness, Makefile, and host scripts belong to the
shared workspace and must be preserved. Private configuration and the v1 archive
are outside the edit boundary. No commits are requested.

## Method and authority

The owner asked to evaluate the implementation and use a strategy similar to chat
`01a1329d-c488-7732-8117-6d8655334067`. That chat's saved
[execution record](../workflow-remediation/execution.md) documents sequential
focused implementers, fresh independent verifiers, one focused retry, explicit
owner extensions, source hashes, and separate host/device verdicts.
Its final host suite passed 146 tests; manual physical workflow was PASS/UNMEASURED
and scripted workflow FAIL/UNMEASURED at `inbox_not_ready`. Those historical results
do not prove the current application works or establish its failure cause.

This run follows the
[client review workflow](../../../../../standards/client-code-review-agent.md)
and [coding standard](../../../../../standards/client-application-coding-standards.md).
The carousel plan supplies normative engineering requirements and C0–C6 work units,
while explicitly retaining product decisions as OPEN. Workflow O-03 authorizes
manual traversal of loading cards; O-04 requires push to preserve viewed identity
and removal to select the nearest remaining message. The old workflow-only O-07
boundary describes the earlier assignment, not approval of this application work.
The old workflow's Luna model selection was specific to that assignment; this
run initially uses the current chat model.

## Review assignment

Fresh read-only reviewer `/root/carousel_review` dispatched with no inherited
conversation, baseline provenance, governing-source instructions, and no expected
finding list. It may run focused existing host checks and write temporary artifacts;
it may not edit maintained documents, source, or flash hardware. Final IDs and the
execution dependency plan will be assigned after consolidation.

Owner answered the scope clarification: **Review, implement, and verify**.
Confirmed application defects within the target are authorized for sequential
implementation and fresh independent verification. No application edits have been
made yet. OPEN product choices remain owner decisions.

## Outcomes

Read-only review complete. [Consolidated report](review.md) assigns stable IDs;
[original review artifacts](initial-review/review.md) preserve the initial labels.
Twenty client/native/architecture checks and three allowed loopback server checks
passed. Seven source-proven defects were found; no L2/L3 carousel confirmation or
fix claim is made. Exact source remained unchanged during review.

## Execution order and prerequisites

| Unit | Scope / findings | Prerequisite and acceptance |
|---|---|---|
| C0 | Characterization and deterministic reproductions, no behavior fixes | Freeze current state; actual production-path tests/traces for fast traversal, delayed page, reversal, page/snap, push/removal and stale completion. Explicitly distinguish headless/model evidence from target visuals. |
| C1a | Reliable owner-terminal reconciliation, F005 | C0 ownership characterization. Queue-full completions must reach owner once, never clear newer work from a worker. |
| C1b | Allocation/startup terminal state, F007 | C0 startup characterization. Independent of C1a; chosen next for failure impact. Failed worker creation cannot leave undrainable requests. |
| C2 | Explicit surfaces and atomic content clearing, F001 | C0 card reproduction. In-range loading/error/bound surfaces retain three roots and no previous message labels. |
| C3a | Page application during motion, F004 | C1a terminal reliability; consume events while preserving deferred visual mutation contract. |
| C3b | Continuous viewport recycling, F002 | C2 truthful surfaces and C3a streaming snapshots. Bounded scroll intents preserve final focus; no tree rebuild. |
| C3c | Snap supersession, F003 | C0 snap characterization; operation identity/one terminal. Independent of C3b in cause, integrated afterward for shared gesture coverage. |
| C3d | Explicit failed-page retry, F006 | C2 error surfaces, C1 operation rules. Finite terminal failures; nonblocking tap retry and continued traversal. |
| C4 | Push/removal identity and deferred snapshot tests | Verified gesture/snapshot units; prove viewed-ID and nearest remaining behavior. Diagnose new defects if proven, not inferred from evidence gaps. |
| C5 | Structured correlation, reviewed metrics schema, resource/timing and visual gates | Stable implementation and reviewed schema changes; record measurements separately from OPEN numeric product gates. |
| C6 | Managed target scenarios and linked eager comparison | Integrated checks, usable registered target, synchronized visual evidence; no host-only closure of input/memory/visual claims. |

No single dependency chain is required. Independent units use **No dependency
ordering required** and run sequentially in the chosen reliability/impact order.
Each focused implementation receives a different fresh read-only verifier. One
focused repair and fresh verifier are permitted after a demonstrated failure;
a second failure blocks dependent units. Missing evidence is collected without
consuming a code repair. Original findings remain intact; outcomes append here.

## Owner decisions during this run

- APP-O-01: LAB-02 keeps a card surface with **Loading message…** while its
  content is pending. Failure shows **Message unavailable** with tap-to-retry.
  Traversal past loading or failed cards is permitted; the new focus causes its
  page to be fetched. Owner explicitly answered **Approve this lab behavior**.
- APP-O-02: Push/removal accepted during a snap defers visual reconciliation until
  the snap finishes or is superseded, preserving viewed identity where possible.
  Owner explicitly answered **Defer visual reconciliation**. Workflow O-04 still
  governs viewed-ID preservation and nearest remaining selection.
- APP-O-03: If both neighbors are equally near after removing the viewed message,
  select the next message; use the previous message when the removed message was
  last. Owner answered **Next message, previous if last**.
- APP-O-04: Initial inbox failure before messages are known shows **Inbox
  unavailable** with tap-to-retry and no automatic retry. Owner answered **Approve
  initial-load tap retry**. Implement with the upcoming retry work.
- APP-O-05: Keep the current 320px viewport / 132px cards / 12px gaps and use
  **four recycled views** for continuous coverage. Owner answered **Keep geometry;
  use four views**. This is the approved bounded alternative to the plan's initial
  three-view premise, justified by window-geometry.json; widget count remains
  independent of inbox size. Apply in C3b and update resource/documentation gates.
- These are lab-only decisions closing carousel OPEN items 1–4 and choosing the
  bounded four-view alternative for item 6. Product-wide inbox policy and numeric
  performance gates remain OPEN; four-view target confirmation still requires
  measurement.
- Owner requested advice on simplifying lab Make tasks. Root recommended retaining
  manual/scripted/eager managed runs, generic build/clean, parse/help and host tests;
  public phaseb aliases, duplicate variant helpers and standalone flash/capture can
  be removed while preserving internal runner operations/configuration variants.
  This was advice only; no Make cleanup has been performed or separately assigned.

Owner clarified that another agent addressed `make lab.lab02.scripted` before
this chat began. That work is part of the preserved baseline WIP, not a concurrent
host edit by this remediation run.

## C0 implementation — independent verification pending

Fixer `/root/c0_baseline` stopped editing. Five new characterization/support files
compile the actual vendored LVGL software renderer and production controller,
card pool, card view, snap, session and model paths; transport and RTOS remain
explicit host adapters. Eight scenarios reproduced baseline behavior; the
characterization PASS includes known failures and makes no fix claim. Two existing
native gates also passed, giving three unittest gates with no skips.

[Handoff and limitations](c0-implementation/outcomes.md),
[exact C0 patch](c0-implementation/c0-only.patch),
[changed-file hashes](c0-implementation/changed-files-sha256.json), and
[input/runtime provenance](c0-implementation/runtime.json) are retained.
Root visually inspected the actual rendered
[blank traversal](c0-implementation/fast-boundary-drag20.png) and
[stale content during loading](c0-implementation/delayed-focus-missing.png).
Existing tracked WIP diff remained byte-identical. No hardware was used.
Fresh read-only `/root/c0_verify` dispatched with raw artifacts, original
findings, source provenance and approved lab decisions.

### C0 independent verification — accepted baseline

Fresh `/root/c0_verify` accepted C0: eight actual-path scenarios and two native
checks pass independently; source and pre-existing WIP hashes stayed unchanged.
An extra no-motion snap reproduces F003's terminal without a new operation/start.
No C0 regression was found. [Report](c0-verification/verification.md),
[independent raw evidence](c0-verification/commands.json).
Achieved L2 headless rendering and L1 injected session/queue cases, no L3 or fixes.
F001–F007 remain not fixed. F006/F007 still require focused runtime fault cases.
The synthetic removal page changes ID/sender but retains its preview; that supplied
input is not evidence of production stale preview. C1a may now proceed.

## C1a terminal delivery — independent verification pending

Fixer `/root/c1a_terminal_fix` stopped edits; fresh read-only
`/root/c1a_terminal_verify` dispatched with original F005, exact diff/source state,
raw native/headless checks, and firmware build evidence.
[Handoff](c1a-implementation/outcomes.md),
[patch](c1a-implementation/c1a-only.patch),
[source hashes](c1a-implementation/after-sha256.json),
[preservation check](c1a-implementation/preservation.json).

Eight normal admissions and one boot admission retain bounded terminal capacity
until owner reconciliation. Producers cannot clear domain inflight state or
overwrite accepted delivery; owner terminalizes superseded admissions before late
delivery. Transport freshness and ordered startup events use separate bounded
storage. Request fields freeze revision, offset, size, attempt, session/op and the
existing read-phase timeout budget. Wi-Fi failure reports an initial fetch failure.

Self-checks: six native/architecture gates, six terminal scenarios with 29 unique
owner terminals, all eight headless scenarios, and actual scripted HTTP firmware
build pass. No flash or target run. Original queue-full C0 evidence is preserved;
current acceptance expects retained completion. Pre-existing tracked WIP is
byte-identical. F004 motion deferral, F006 retry, F007 task allocation, whole-attempt
deadline and target confirmation remain open; this is not full C1 completion.

### C1a independent verification — F005 mechanism verified L1/L2

Fresh `/root/c1a_terminal_verify` independently verified F005's mechanism at
L1/L2. Original headless queue pressure retains/applies the accepted page with zero
dropped terminals. Six native/architecture gates, six terminal scenarios with 29
unique outcomes, and all eight headless scenarios pass. Added mutex-busy pump,
reservation-release contention, reset/pending-boot and Wi-Fi-style initial-failure
boundaries pass without duplicate outcomes or loss of newer work.
[Report](c1a-verification/verification.md). Source/WIP unchanged, no regressions
found. L3 FreeRTOS/target concurrency remains unverified; no waiver. C1b follows.

## C1b allocation/startup — independent verification pending

Fixer `/root/c1b_allocation_fix` froze source; fresh `/root/c1b_allocation_verify`
received the original F007, requirements, exact source/patch and raw evidence.
[Handoff](c1b-implementation/implementation.md),
[patch](c1b-implementation/unit.patch),
[hashes](c1b-implementation/after-hashes.json).
Boot allocation failure now posts an unavailable startup result and reserved fetch
failure, permitting owner ERROR from SHELL. Background creation moved to the boot
I/O worker; normal admission stays closed until owner-applied worker availability.
A valid initial page retains truthful fetch success while separate startup failure
produces ERROR, retains page zero and suppresses HTTP_READY. Resource failure
terminalizes even with held input; ordinary motion deferral remains F004.

Self-checks: seven native/model/architecture gates, six terminal scenarios, three
actual-provider fault scenarios, eight headless scenarios and HTTP firmware compile
pass. No hardware or automatic retry. Target resource-pressure evidence remains
unverified. Existing tracked WIP is byte-identical. A preliminary host-change alert
was a CRLF normalization comparison error, corrected before handoff; root also
confirmed the current host source SHA-256 matches the original baseline. The owner
attribution question based on that alert was explicitly withdrawn in commentary.

### C1b independent verification — F007 implemented, L1 verified, overall unverified

Fresh `/root/c1b_allocation_verify` independently reproduced both provider faults
and extra reset/stale availability boundaries. Seven focused gates and eight
headless regressions pass; 85 source/WIP hashes and raw diff remain unchanged.
[Report](c1b-verification/verification.md). F007's mechanism has actual-provider
injected L1 evidence. The headless regression runtime substitutes the provider,
so it does not supply F007-specific rendered startup L2; target allocation/stack
L3 also remains missing. No demonstrated failure or repair is indicated, and no
retry is consumed. F007 is **unverified overall**, not closed. Collect the missing
startup runtime/target evidence in C5/C6. C2 is causally independent of this evidence
gap and may proceed sequentially.

## C2 surfaces — independent verification pending

Fixer `/root/c2_surfaces_fix` froze source. Fresh `/root/c2_surfaces_verify` receives
original F001, exact patch/hashes, actual render frames, normal/slow conditional
checks and firmware compile. [Handoff](c2-implementation/implementation.md),
[patch](c2-implementation/unit.patch),
[hashes](c2-implementation/after-sha256.json).
Compact session slot snapshots drive five explicit presentation states. In-range
missing/failed/delayed content retains surfaces and clears prior text/indicators;
delayed binds reject stale generation/session/revision/ID. Existing geometry and
three roots/four children per card remain. Ellipsis rendering has a bounded font
fallback. No retry control or continuous traversal fix is claimed.
Self-checks: ten normal and eleven slow-bind actual-LVGL scenarios, seven native/
architecture gates and HTTP firmware compile pass. Prior non-C2 source preserved.

### Geometry decision pending for C3b

[Current geometry calculation](window-geometry.json) finds four visible cards at
scroll x=566: indices 3/4/5/6 expose 10/132/132/10 pixels in a 320-pixel viewport.
Three surfaces cannot cover all four intersections. Owner was asked to choose
existing geometry with four recycled views or three views with cards widened to
at least 142 pixels. This closes an explicit carousel OPEN item only after an
answer; C2 preserves current geometry and makes no sufficiency claim. C3b depends
on the answer; other independent units may continue while it is pending.

### C2 independent verification — F001 mechanism verified L1/L2, L3 pending

Fresh `/root/c2_surfaces_verify` confirmed F001's mechanism at L1/L2. Ten normal
and eleven slow-bind actual-LVGL scenarios and seven native/architecture gates
pass. Extra frozen-input, revision-only stale rejection and unbind-timer boundaries
pass. PNG inspection confirms complete copy/ellipsis with no stale content or
missing glyphs. Eleven C2 hashes and 6,629 runtime input hashes stayed stable.
[Report](c2-verification/report.md). No C2 regression. Overall F001 closure remains
unverified pending target L3; no waiver. F002/F003/F004 still reproduce, F006 is
separate. C3a may proceed; geometry choice remains pending for C3b.

## C3a page application — resumed implementation

Fixer `/root/c3a_page_apply_fix` was assigned F004 with accepted C1a terminal
delivery and C2 surface prerequisites. It captured the old deferral baseline and
implemented immediate ordinary page acceptance with a separate bounded pending
mutation; native checks passed and further supersession/session/LVGL gates were
in progress. A usage-limit interruption stopped its turn before a frozen handoff
or independent verification. No F004 fix claim is made from those partial checks.

Owner explicitly requested **please continue**, supplied APP-O-04/05, and current
usage became available. The same fixing context resumes the existing focused unit
and must finish/freeze before a different fresh read-only verifier. No baseline
work is restarted. The geometry prerequisite for C3b is now satisfied by APP-O-05.

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

### C3a independent verification — F004 mechanism verified L1/L2, L3 pending

Fresh `/root/c3a_page_apply_verify` verified F004's mechanism at L1/L2. Seven
native/architecture gates and eleven normal / twelve 120ms slow-bind LVGL
scenarios pass. Original held-input deferral is independently reproduced in the
before reducer; current ordinary pages apply/terminalize in place during held
input and snap. Added all-eight-admission, lower pending revision, one-work-unit
unlock, late stale completion, immutable pending data and m6→index7 viewed-ID
preservation probes pass. [Report](c3a-verification/report.md).
Twelve after hashes, 6,629 runtime inputs and git status unchanged; no regression.
Overall F004 closure is unverified pending L3, no waiver. C3b's C1a/C2/C3a mechanism
prerequisites and APP-O-05 geometry decision are now satisfied.

## C3b independent verification — F002 mechanism verified L1/L2, L3 pending

Fixer `/root/c3b_window_fix` implemented APP-O-05 four roots / twenty card nodes,
continuous half-open viewport intersection coverage, surviving binding/timer
preservation and bounded latest-focus admission. Exact patch/source/commands:
[c3b handoff](c3b-implementation/implementation.md). Fresh
`/root/c3b_window_verify` independently passed seven native gates, eleven normal
and twelve 120ms slow-bind LVGL scenarios. Preserved baseline reproduces zero
card pixels; current drag produces 37,861. Extra exhaustive probe passes 93,414
displacements and 291,756 surviving-binding checks. Fifteen after hashes and
6,629 runtime inputs match; source and unrelated WIP stayed stable.
[Verification report](c3b-verification/report.md). No regression found.
Overall F002 closure remains unverified pending L3, no waiver. F003 snap
supersession and F006 retry remain separate; C3c may proceed sequentially.

## C3c independent verification — F003 mechanism verified L1/L2; paused

Fixer `/root/c3c_snap_fix` implemented immutable snap identity, exactly-one
terminal outcomes including no-motion, reversal/supersession, stale callback
rejection and bounded lifecycle handling. Seven focused files; preserved
non-unit WIP. [Implementation handoff](c3c-implementation/implementation.md).
Fresh `/root/c3c_snap_verify` independently reproduced original reversal and
lost-terminal defects, then passed seven native checks, eleven normal and twelve
120ms slow-bind actual-LVGL scenarios. Independent ID pairing confirms 28 starts
and 28 exactly-once terminals per configuration. Added external deletion with
pending insertion, last-item removal with immediate replacement, and archived
callback replay after same-storage reinitialization all pass. No demonstrated
regression or repair retry. All 2,016 captured source files/status/full WIP diff
unchanged during verification; 1,019 implementer hashes match.
[Verification report](c3c-verification/report.md). Actual HTTP firmware build
passed; no hardware flash/run. F003 overall remains unverified pending L3 touch,
visual, timing and resource/lifecycle evidence; no waiver.

Owner requested: “When c3c snap verify finishes report the status and then pause
work there.” Verification finished and status recorded; work was **paused at C3c**
until the owner resumed C3d in-session.

## C3d owner retry — implementation and verification (paused after verify)

Resume scope: **LAB02CAR-F006** and **APP-O-04** initial inbox tap-to-retry.
Removed automatic ~1s missing-item refetch from `lab02_session_pump`. Added owner
`intent_retry_page_for_index` / `intent_retry_initial_load` with attempt tracking,
20 ms coalescing, `owner_retry_page` / `owner_retry_initial` traces, and
`fetch_retry` counting owner admissions only. Error cards and inbox-unavailable
status tap dispatch retry; HTTP firmware links
`lab02_page_provider_http_retry_initial_load` (C0 host stub is a no-op). Boot
HTTP completions now use the admitted boot `op_id`. New `test_retry_policy_native`
gate; C0 `failed-page` asserts no background retry and owner retry success; session
and HTTP startup tests updated for non-zero boot op IDs.

Self-check: `python3 -m unittest scripts.tests.test_lab02_model_native
scripts.tests.test_lab02_architecture scripts.tests.test_lab02_carousel_native
scripts.tests.test_lab02_host -v` — 24 tests OK.

Self-check report: [c3d-verification/report.md](c3d-verification/report.md).

### C3d independent subagent verification — F006 mechanism accepted L1/L2

Fresh read-only subagent re-ran source inspection and the full 24-test host gate
(`test_lab02_model_native`, architecture, carousel C0, host); exit 0, ~9.5 s.
Agrees with self-check disposition: owner retry and APP-O-04 initial re-arm,
no ~1s missing-item loop, coalescing/attempt/traces, boot `op_id` delivery,
presentation non-blocking guard PASS. **No demonstrated regression.** F006 overall
remains **unverified pending L3** (device tap/visual); no waiver. C0 does not
simulate LVGL clicks; 5 s idle page-0 refresh is unchanged and separate from F006.
[Independent report](c3d-independent-verification/report.md),
[commands](c3d-independent-verification/commands.json).

Work **paused after C3d verify** per owner. Next eligible unit on resume: **C4**
push/removal identity. C5/C6 not started.

## Resume after C3d and physical startup mitigation

Owner resumes all remaining work. Accepted other-agent C3d implementation and
independent report as current WIP, with historical hash/click-path gaps retained.
Owner reports successful build/flash and visible carousel after
`CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=50`. Fresh read-only assessment confirms single
RGB565 DMA buffer falls from 64,000 to 32,000 bytes; no measured causal Wi-Fi
allocation claim. Existing successful run plus owner observation support startup
only, not full L3 closure. Canonical new run overlay regenerates configuration;
record each run's effective value, since reused unchanged sdkconfig may retain100.
[Resume assessment](resume-memory-review/report.md), [current snapshot](resume-baseline/source-sha256.json).

New confirmed LAB02CAR-F008 (initial tap's unlimited worker mutex wait) requires
one focused fix and fresh independent verification before C4. Existing C3d
behavior/C1a delivery are prerequisites satisfied at mechanism level. C4 identity
then C5/C6 remain planned; no host cache-policy or private-config edit authorized
by this client unit. All staged/unstaged work preserved.

## F008 independent verification — mechanism verified L1/L2, L3 pending

Fixer `/root/f008_retry_fix` froze bounded owner retry intent and lifecycle
provider dispatch after LVGL unlock. Busy admission preserves one coalesced
intent; immutable restart command and atomic boot-running guard preserve
exactly-once/stale semantics. Existing 50-line draw buffer unchanged; unrelated
WIP/index preserved. [Handoff](f008-implementation/implementation.md).
Fresh `/root/f008_retry_verify` passed 23 integrated checks, actual-LVGL normal
11+6 and slow120 12+6 scenarios, original contention reproduction and an added
held-boot-exit plus busy-command probe. Intent survives ten broker calls before
old boot exit, then busy mutex, and dispatches once to READY. Source/docs/HEAD/
index/status/WIP diff remained unchanged; no regression or repair indicated.
[Report](f008-verification/report.md). Overall F008 remains unverified pending
L3 touch/timing/resource/real-RTOS evidence, no waiver. C4 may proceed sequentially.

Historical buffer investigation complete: [note](resume-memory-review/draw-buffer-history.md).
V1 used100 lines; owner selected50 without testing another height. Startup RAM
region is23,440 bytes smaller in newer image; static terminal/pending storage
strongly supports growth contribution, exact full attribution remains unknown.
C5/C6 must include DMA-largest-block and effective-config measurements; revisit
height if material, preserve working50 until evidence supports another value.

## Owner steering: loading overlay and phase commits

Owner explicitly requests removing persistent loading-inbox label (including
missing font glyph) over carousel cards. F010 is confirmed from owner observation
and separate centered board label lifetime. Fix/independently verify this small
unit before implementing C4 confirmed F009. C4 proof is frozen with unchanged
source/index/WIP: [report](c4-proof/report.md).

Owner amends workflow: after a phase finishes independent verification, create
a scoped commit with a clear message. Do not claim missing L3 closure. Preserve
unrelated staged work; current verified client prerequisites need an initial
scoped foundation commit together with the verified overlay removal, followed
by separate future phase commits. Original no-commit instructions are superseded
by this explicit owner authorization. C5/C6 include revisiting provisional50
height if material and resource/timing gates; no automatic tuning.

## F010 independent verification and phase commit

Owner committed previous foundation as21a37df4b220eabcf6b3717601753cc8cf804360
(`lab 02 remediation continues`). This supersedes the proposed foundation catch-up
commit above; subsequent units receive their own scoped commits.

F010 removes startup centered loading-inbox label call and replaces loading
header text with LAB-02. Only lab_main/controller source changed. Fixer handoff:
[f010 implementation](f010-implementation/implementation.md). Fresh verifier
passed actual-LVGL retry-success, retry-boot-fail, delayed-focus and an added
failed-page boundary; exact substitutions/call lifetime and unchanged WIP/index
proved. Final HTTP firmware compile passed. [Report](f010-verification/report.md).
No regression/repair indicated. Header mechanism hasL2; startup overlay absence
is source proof because main/board are outside headless runtime. Physical
before/after confirmation remains pendingL3, no waiver. Approved phase commit
will record source fix and evidence without claiming full target closure.
Next: focused F009/C4 surviving-ID resolution and fresh verifier.

## APP-O-06 — confirmed removed-ID ambiguity fallback

Owner explicitly answered: when the viewed message is removed and the client
cannot identify the next surviving message from its old page window, select
the prior index, clamped to the new last item. LAB-02 only; global bounded
search must establish absence before fallback. This does not permit discarding
a surviving viewed ID or guessing removal from missing pages.
[Decision](owner-app-o06.json).

F010 overlay phase committed asc4c0116192e087148f3c83de1dfe979b3184fcab
(`fix(lab02): remove persistent loading inbox overlay`), working tree clean
before F009. F009 implementation source frozen; [handoff](f009-implementation/report.md).
Native23 and normal/slow120 actualLVGL prior+13 new identity scenarios pass
in self-checks. Correlated server pages→production parser→current owner/render
ends m200/index100. DirectHTTP firmware compile passed withheight50; canonical
build preflight reported invalid client URL configuration, a separate gap for
C6. Targetstatic storage adds62 symbol bytes/64 BSS bytes, no duplicate model
or pagebuffer. Fresh independent verification follows; no L3 claim.

## F009 / C4 independent verification and phase commit

Fresh `/root/f009_identity_verify` accepts F009 mechanismL1/L2:23 native gates,
normal/slow120 allprior+13 identity cases, exact retained server receipt bytes
through productionparser/owner/LVGL, and added during-snap identity resolution
probe. Findingm200 duringmotion preserves lock/index/viewport/maskedwindow until
terminalunlock, then relocates100/m200. No duplicate terminals or source/index/
WIP drift. [Report](f009-verification/report.md). No regression/repair indicated.
Overall remains unverifiedpending representativeL3 and managedHTTP runtime
evidence, no waiver. Sourcephase ready for owner-authorized scoped commit.

C5 structured observability/schema and resource/timing gates is next. Canonical
clientURL preflight gap must be diagnosed read-only before managedtarget runs;
no private configuration or host policy changes are implied. C6 target visual
confirmation of overlay/identity/retry and linked eager comparison still required.

## C5 baseline and revised sequential subunits

C4/F009 source phase committed03b5e42d15bf8c788bf80b9b55a3c103b832c32d
(`fix(lab02): preserve viewed identity across page revisions`).
Fresh [C5 baseline](c5-plan/report.md) maps existing/missing fields and proposes
additive metrics.v1 contract with null/unobserved and loss→INCOMPLETE rules.
F011 console-wait defect is confirmed as safe-transport prerequisite.
C5a: author/review/commit trace-metrics contract; C5b: focused safe trace transport
F011 with fresh verifier/commit; C5c: instrument/parser/resource gate with fresh
verifier/commit. Keeping actual behavioral/deadline changes separate. Eager local
measurement/traversal may require a focused follow-up, not disguised HTTP parity.

Read-only preflight confirms configuredIPv4 is no longer assigned, interface
inspection works, two eligibleassigned candidates each validate via existing
per-run LABS_LAB02_CLIENT_URL override. Choose kit-reachableLAN for C6; no private
file/hostpolicy edits needed. Whole-attemptdeadline remains contract/evidencegap,
not newly confirmed numeric violation. Productbudgets remainOPEN.

## Owner-requested pause at the C5 implementation boundary — 2026-10-13

Owner asks: “When you get to the next segment, wrap up your work for now and
log what you have completed.” C5 baseline review is finished; **pause before
C5a contract authoring**. No schema, trace transport or instrumentation edit
started. All subagents finished; no build, flash or background run remains.

Completed since resume:
- Accepted other-agent C3d as current baseline; retained click/target limitations.
- Fresh startup/config assessment and historical draw-buffer note saved. Owner
  confirms current carousel visible. Fifty lines was selected without testing
  other heights; retain provisional value and revisit with resource/performance
  measurements. V1 used100; earlier LAB startup RAM region was23,440 bytes larger.
- F008 initial retry boundedness implemented and independently verifiedL1/L2.
  Actual click/provider and held-boot-exit probes pass; included in owner's
  foundation commit21a37df (`lab 02 remediation continues`).
- F010 loading overlay removed and independently source/runtime verified,
  committedc4c0116 (`fix(lab02): remove persistent loading inbox overlay`).
- C4 provedF009 lost survivingID, fixed bounded identity resolution and approved
  APP-O-06 fallback, independently verifiedL1/L2; committed03b5e42
  (`fix(lab02): preserve viewed identity across page revisions`).23 native
  gates, normal/slow prior+13 identity cases and added during-snap probe pass.
- C5 read-only baseline/schema proposal complete. NewF011 presentation console
  wait confirmed; **not fixed**. URL preflight diagnosis complete: configured
  host address no longer assigned; existing per-run override validates current
  assigned candidates. Kit-reachable interface still needs selection/verification.

Resume from C5a reviewed metrics/trace contract, then C5b F011 safe transport,
then C5c instrumentation/parser/resource gates. Each source phase requires fresh
independent verification and a clear scoped commit. Eager16 local comparison
needs truthful equivalent traversal/measurement hooks; do not claim HTTP parity.
C6 canonical managed scenarios and linked comparison remain. Preserve user
commits, working draw height50 and approved APP-O-01–06.

No new physical flash/run in this resume. Required L3 visual/touch/timing/heap/
stack/real-RTOS evidence remains unverified, including physical overlay absence,
identity/retry and target trace transport. Numeric product gates remainOPEN.
Whole-attempt deadline remains documented gap, no speculative change. Do not
claim full carousel completion from successful host tests or firmware builds.
