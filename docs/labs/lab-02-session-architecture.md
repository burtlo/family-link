# LAB-02 session architecture

LAB-02 is a lab implementation of the presentation → session → I/O boundary in
the [client coding standards](../standards/client-application-coding-standards.md).
Its paging and focus fallback policies are experimental, not approved product
behavior.

## Ownership and waits

| Path | Owner | Waits / bound |
|---|---|---|
| Scroll, snap, four card views, status label | LVGL presentation | No network or sleep; one session completion and at most four changed cards per pump |
| Focus ID/index, revision, load stage, page state | `lab02_session` | Single writer; queue operations are nonblocking from presentation |
| Wi-Fi join and HTTP | Boot and fetch workers | Wi-Fi join 15 s; HTTP call timeout 3 s and response-read deadline 12 s; response cap 16 KiB |
| JSON parse | HTTP worker | Parses at most one 16-item page into a bounded typed result |
| Page merge | Session | Copies at most 16 items; ordinary pages apply during motion; one newer page-zero revision snapshot waits for motion terminal |

The boot worker posts connecting, fetching, and terminal events. The session
checks permitted stage transitions and applies the initial page. A successful
boot worker creates the fetch worker before delivering the initial page, outside
presentation/LVGL locks. No normal fetch is admitted until the session pump has
applied the bounded startup availability report.

## Fetch operation envelope

Each queued request freezes session generation, monotonically increasing operation
ID, page index/offset/size, inbox revision, attempt identity and monotonic submission
time. Every retry has a new operation ID (attempt field is 1 for each admission).
The immutable response-read timeout budget is 12 seconds; the existing HTTP call
timeout remains 3 seconds. The read deadline starts after headers, preserving the
existing policy: queue wait and initial Wi-Fi join do not consume that budget.
A whole-attempt deadline covering queue wait and all HTTP phases remains an explicit
C1/C5 gap; this unit does not claim that the read deadline bounds the whole attempt.

Terminal delivery uses eight normal admission mailboxes plus one dedicated boot
mailbox. A normal request reserves one mailbox and one fetch admission before it
is queued. Its admission is released only after the session owner reconciles and
emits exactly one `fetch_terminal`, including failure, timeout or stale. A successful mutation accepted during motion releases its reservation; one separate
immutable owner-held page-zero snapshot retains its reconciliation input. Producers fill only the matching
admitted session/operation/page record; duplicate and unadmitted completions emit
`fetch_delivery_rejected` and cannot overwrite stored terminals or domain state.
The owner terminalizes superseded session/revision/operation admissions as stale,
even if the producer has not returned; a late producer is then rejected.

Transport uses one coalesced freshness mailbox. Startup stages use a bounded bitset
that preserves CONNECTING then FETCHING order before applying a boot terminal;
invalid transitions remain checked and counted. These reports cannot consume fetch
terminal capacity. Producers serialize bounded delivery copies with a mutex; the
presentation/session pump and admission use zero-timeout try-locks and retry pending
owner reconciliation on a later pump. Each pump accepts at most one fetch terminal OR applies one pending mutation
(maximum 16-item copy/apply), with pending reconciliation taking priority after
unlock. Stage and transport reports continue during motion and pending mutation. The legacy `fetch_event_dropped` metric remains for compatibility and is
zero for admitted terminal delivery. Full admission rejects new work observably
rather than dropping accepted results. The fetch request queue remains eight slots.

The session still retries a missing focused page on a one second cadence. A failed
focused page shows “page unavailable; retrying”. If background page-zero refresh
fails, existing items are labeled “cached”. STA disconnect sets transport
unavailable copy (“reconnecting…”) without erasing the load stage. Retry UX is unchanged by C3a; F006 remains separate work.

The initial fetch uses the same event path. Its failure transitions to an
unavailable stage; automatic boot retry and product copy remain **OPEN**.
Repeated page failures remain visible and retry while that page is focused.

## Inbox revision and focus

The session refreshes page zero after five seconds of no active fetch, drag, or snap.
When a later page announces a newer revision, the session requests page zero
before accepting it. When page zero announces a newer revision, the session
invalidates old pages and outstanding operation IDs. A known, already materialized
viewed message ID remains the anchor until found in the new revision or all its
pages have been completely materialized. An adjacent-page miss is not absence.
One search page is admitted at a time using existing fetch envelopes; each pump
performs at most three bounded identity lookups (256 comparisons each) and
three page-ledger scans (16 bits each), with one admission attempt per 20ms.
During unresolved identity, four card surfaces and the header expose loading;
failure or timeout exposes Message unavailable / tap card to retry, with a tap
mapped to the actual failed search page. An incomplete page cannot establish
absence and terminalizes as failure requiring an owner tap. Admission contention
retains the search for a later pump. The ordinary five-second refresh is suppressed
while resolution is pending or failed; explicit refresh may supersede it with a
newer revision. Existing operation timeout coverage remains unchanged (the whole
attempt deadline gap above remains OPEN).

Resolution uses the existing static 256-entry model: loaded pages are retained,
not evicted, so the loaded-page ledger denotes currently materialized pages for
this revision. The search may materialize the entire bounded lab inbox without
adding a model, item, page, widget, queue or task. A found survivor stops search
and resumes focused/neighbor prefetch. A different-index owner intent cancels the
old anchor, even when its new ID is unmaterialized; a repeated same-index intent
cannot adopt a different loaded ID while reconciliation is unresolved. Session
reset and newer page-zero revisions supersede old fetch identities. Search may
merge ordinary results during motion, but commits relocated focus only after
unlock; APP-O-02 mutation deferral remains in force.

Before discarding the old revision, the owner retains only the exact immediate
known next/previous IDs (48 bytes) and whether the viewed item was last. After
complete absence proof, APP-O-03 chooses that known next survivor, or previous
when originally last. APP-O-06 (owner instruction during F009) authorizes a
limited LAB fallback: if the next surviving identity cannot be determined from
the old page window, select the prior index clamped to the new last item. This
fallback is applied only after proving the viewed anchor absent; it makes no
claim to globally nearest old order. Unknown initial focus remains an index
intent and does not invent a stable anchor. Product-wide paging/removal policy
remains OPEN. An older revision is discarded.

## Presentation update levels and resource envelope

The scene is created once. Status changes are Level 0; a changed visible card
window is a Level 1 rebind keyed by message ID, focus, and revision. Routine
120 ms pump wakeups perform no card updates when the session has not changed.
The `S-delay` injection uses a timer and drops stale delayed bindings without
sleeping under the LVGL lock. No Level 2 scene replacement occurs after init.

Design limits: four live cards; at most 256 metadata entries, eight queued
fetches, eight admitted normal terminals plus one boot terminal, one active HTTP fetch, a 16 KiB response buffer, and 16 items copied
per page apply. Target measurements for live node count, cJSON transient heap,
largest free block, frame p95/p99, and input-to-focus latency remain **OPEN**
until a representative BOX-3 run. A build or host test does not establish those
measurements.

## Verification

The host suite compiles the actual C page parser and session reducer with test
queue primitives. It checks page replacement, malformed page rejection, result
ownership across two completions, ordinary page application during held motion,
and bounded mutation deferral/supersession during a snap. The Python queue
model tests remain supplemental. Phase B still requires scripted device evidence
for delayed HTTP, queue pressure, focused removal, push, repeated traversal,
and allocation/timing budgets. Record L0, L1, L2, and L3 separately in the lab
evidence report.

C1a reliability authority: [carousel requirements CAR-ARCH-2/3 and CAR-PAGING-1](lab-02-carousel-remediation-plan.md), with scope/owner decisions in the
[carousel execution record](evidence/lab-02/reviews/carousel-remediation/execution.md).
C1b/F007 startup uses one first-report availability mailbox per session, independent
of fetch terminal capacity. `io_start_admitted` / `io_start_terminal` identify the
HTTP startup operation by session generation and kind. Boot-task creation failure
posts the reserved initial fetch failure; SHELL is a permitted ERROR source.
Background-task creation failure retains a valid initial-page success, then applies
ERROR instead of claiming HTTP readiness. Its initial data merge can finish even
while input is held so allocation failure cannot remain an indefinite loading wait;
ordinary successful pages now apply during motion under C3a. Admission remains closed with
no queued work when no consumer was created. Existing valid initial-page contents
remain traversable. Startup failure is terminal; no automatic restart/retry is added.

Production `page_provider_http.c` is compiled by `test_http_startup_native.c` with
RTOS task-allocation and HTTP adapters. Boot failure, background failure with input
held, and success cover exactly one startup/initial terminal, first-report duplicate
and stale rejection, valid initial neighbors, no undrainable queue, and readiness
truth. These deterministic native failures are L1/scripted adapter evidence; no
FreeRTOS scheduling, target resource-pressure or stack confirmation is inferred.

## C3a page acceptance and deferred revision

CAR-PAGING-2/3 and APP-O-02 govern distinct paths. Ordinary same-revision pages
merge in the owner pump during motion and publish bounded card updates without
moving the viewport. Newer page-zero push/removal results terminalize their fetch
as accepted success but retain one immutable `lab02_sess_event_t` in static owner
storage (one page, at most 16 items), independent of the nine delivery mailboxes.
No page-sized automatic stack buffer is added. `pending_inbox_revision` denotes
received/accepted mutation; `inbox_revision` remains the applied model revision.
`model_pending` and `model_pending_stale` report normalized pending/supersession
reasons; `page_apply` records model application, while card `bind` traces establish
presentation publication. A fetch terminal alone does not prove visibility.

The active provider continues pointing to the current model until motion ends;
borrowed item pointers are consumed before the next pump. Pending snapshots own
all bytes and never reference provider, mailbox, or card-view storage. A newer
pending revision replaces the previous snapshot; a session reset discards it.
On unlock, reconciliation uses the latest owner focus ID/index, preserving the
viewed ID where present and the existing next/previous-if-last removal policy.
The three-view geometry is unchanged in C3a; APP-O-05's four-view implementation
belongs to C3b. Initial failure APP-O-04 and retry changes belong to F006.

## C3b continuous bounded viewport

APP-O-05 keeps 320px viewport, 132px cards and 12px gaps with four roots /
20 card nodes. `layout.h` supplies geometry to snap and window math. The first
card is selected by half-open viewport intersection, not a focus offset; four
roots cover the narrow outer intersections near half-stride. SCROLL performs
bounded presentation recycling during gesture and custom animation. Roots that
retain index/session/revision/ID/state keep their binding and pending timer;
focus styling is Level 0, vacated root content replacement is bounded Level 1.
No scene/scroll tree is rebuilt and no automatic item copy is added to the UI stack.

The session remains domain-focus writer. The presentation holds one latest focus
intent with a 20ms submission interval, flushed on SCROLL_END; animation preserves
the session's target focus while presenting the actual viewport. Focus fetch
admission precedes speculative neighbors. One latest not-yet-admitted page retries
nonblocking admission at most once per 20ms while motion continues; admission,
materialization, failure or focus supersession stops it. Queue-full/busy and
`focus_admission_pending` are observable. This does not implement F006 failed-page
retry or F003 snap cancellation. The interval is an engineering bound; numeric
product gates and target stack/memory/input timings remain OPEN in C5/C6.

## C3c snap operation and reversal

CAR-GESTURE-5, CAR-BIND-5 and standard section 4 govern the snap. One static
presentation-owned envelope freezes ID, session generation, applied revision,
focus/message ID, submitted time, target, callback and presentation handles.
Status becomes terminal before callback or LVGL deletion. The LVGL animation
copies the ID into its user data; archived ticks/completions compare that ID
before reading current handles or moving the viewport. Worker current-ID access
remains telemetry only. Each acceptance emits snap_start and exactly one snap_end,
including no-motion; terminals retain the original immutable operation input.
Outcomes/reasons are success/completed or no_motion, stale/reversed, superseded,
scroller_deleted or session_changed, and cancelled/animation_deleted or
animation_allocation_failed. A stale archived callback after terminal adds no
second terminal. Model/bind traces establish reconciliation and final focus.

A tick marks only its synchronous scroll_to-generated BEGIN/END as programmatic;
SCROLL still recycles the actual viewport on every displacement. Real BEGIN
supersedes active snap nonblockingly. Controller replacement captures owner focus
before unlock can apply a pending mutation, relocks motion, and uses the reconciled
focus/count for the new target. Four bounded roots and the single latest 20ms
focus-intent slot are retained. An immediate focus replacement also cancels the
old animation. Scroller DELETE invalidates the controller handle before terminal;
reset rejects old-generation animation writes on the next tick. External LVGL
animation deletion terminalizes cancellation and unlocks owner reconciliation.

Resource limit: one static snap envelope plus one LVGL animation, no per-tick
allocation by snap/controller, no per-callback item/page copy, and bounded small
trace buffers (240 bytes maximum). LVGL animation allocation failure terminalizes
the accepted ID. Numeric callback/animation budgets and representative target
stack/input/memory measurements remain OPEN. Host tests use real LVGL pointer
read/hit-test/gesture recognition with synthetic pointer samples; board touch,
physical driver scheduling and target arbitration remain unverified.

## F008 initial retry admission and lifecycle dispatch

APP-O-04 governs initial-error tap retry without automatic failure retry. The
status callback retains one coalesced owner intent and performs no worker wait or
task allocation. The owner pump uses a zero-timeout delivery-mutex admission;
contention retains the intent for a later pump. Successful admission increments
the attempt once, freezes a new boot request, and moves ERROR to SHELL. One
mutex-protected immutable restart command is consumed by the lifecycle broker
only after it releases the LVGL lock. Boot task allocation and failure reporting
run there, outside presentation. No new task/queue or page-sized stack buffer is
added; pending storage is two flags plus the existing reserved boot envelope.

An atomic boot-running guard retains the command until a prior boot has finished
using its frozen request/scratch. Reset clears an unadmitted intent; the broker
compares the admitted command against atomic session generation before dispatch.
Late prior completions use the existing delivery identity checks. An existing
background worker is retained and its availability is reported for the new boot.
Task creation failure still produces the reserved initial terminal and truthful
inbox-unavailable stage, with another owner tap required.

`tests/c0_runtime/test_initial_retry_runtime.c` exercises actual LVGL status
clicks, the actual session and HTTP provider, and explicit task/HTTP adapters.
It holds the admission mutex through clicks/pump/broker, checks one admission on
release, enforces task allocation outside presentation context, and covers boot
and background allocation failures, reset, late completion and retained worker.
Normal and 120ms delayed-binding configurations provide host L2 evidence;
FreeRTOS/device input timing, allocation pressure and stack confirmation remain
L3 requirements. Earlier automatic retry descriptions above are superseded by
APP-O-01/04 and the C3d owner-only retry implementation.

F009 resource delta: actual production HTTP target linker maps retain the same
37,940-byte model, 23,040-byte terminal array and 2,544-byte pending mutation
record. Added owner symbols total 62 bytes (48 ID bytes, 8-byte admission time,
4-byte failure page, two flags); linked DRAM BSS grows 64 bytes including padding,
with DRAM data unchanged. No new dynamic allocation or item/page-sized automatic
buffer is introduced. Each identity lookup visits at most 256 fixed metadata
entries; the absence ledger visits at most 16 pages at the existing 16-item lab
stride. Four roots / 20 nodes, eight normal terminal admissions and one boot
admission remain the limits. Model/page/operation sizes do not grow with this fix;
whole-inbox materialization uses the existing fixed storage only. Height 50 is
preserved. Linker sizes establish static L0 bounds, not runtime startup DMA heap,
stack high-water, fragmentation, latency or physical target confirmation; those
remain C5/C6 L3 work.
