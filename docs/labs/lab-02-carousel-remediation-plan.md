# LAB-02 carousel application remediation plan

**Status:** Draft implementation plan; application-code work not yet authorized by this document alone.

**Scope:** The LAB-02 client carousel and its model/session/paging boundary. This plan is linked to the [LAB-02 workflow remediation plan](lab-02-workflow-remediation-plan.md), which owns server orchestration, kit selection, capture, and run evidence. The workflow plan measures the scenarios below; this plan defines the client changes needed to make them correct and usable.

**Standards authority:** [Client application coding standards](../standards/client-application-coding-standards.md), especially architectural boundaries, bounded presentation work, single-writer session state, asynchronous operation envelopes, UI update levels, operational contracts, resource budgets, observability, and the L0–L3 verification ladder.

## 1. Problem statement

The original LAB-02 report described a carousel that can expose a blank section during fast traversal, then display cards after the gesture or appear to snap back. The current implementation has several plausible contributors that must be proven with traces rather than assumed:

- the controller rebinds the three-card pool primarily at snap completion;
- page results may be deferred while scrolling is locked;
- a missing item hides a card view instead of presenting a bounded loading state;
- focus, message identity, page revision, and binding generation are not yet proven as one atomic visual update;
- push/removal during or near snap needs identity-preserving reconciliation;
- the native test gate does not yet cover the rendering behavior during fast gestures and delayed paging.

This plan treats blank content, wrong content, apparent snap-back, stale asynchronous results, and lost focus as correctness defects. Visual polish is not the acceptance criterion.

## 2. Goals and non-goals

### Goals

1. Keep the carousel visually represented throughout a gesture, including when the focused page is not yet available.
2. Keep input and animation responsive independently of network, storage, parsing, or worker latency.
3. Preserve stable message identity and the intended focus across page arrival, push, removal, refresh, reversal, and snap completion.
4. Recycle a bounded number of card views without showing stale content or rebuilding the scene.
5. Make every asynchronous fetch/apply/rebind path observable, cancellable or stale-safe, and terminal.
6. Add model, session, controller, and target tests that can distinguish a real fix from a faster version of the same blank-viewport behavior.

### Non-goals

- Changing the LAB-02 host workflow, server lifecycle, kit resolver, or evidence-run ownership.
- Introducing a product-wide inbox paging policy; unresolved product policy remains **OPEN**.
- Adding audio/sketch decode or the LAB-03 media experience.
- Replacing the bounded three-view design with an eager all-card implementation. The eager build remains a comparison control.
- Claiming the carousel is fixed from host tests or a clean build alone.

## 3. Normative implementation requirements

The following requirements are actionable and MUST be satisfied unless an approved decision record changes them.

### CAR-ARCH — ownership and boundaries

1. `carousel_controller` and card views MUST own only presentation operations: input dispatch, animation, focus visualization, bounded card property/subtree updates, and trace emission.
2. `lab02_session` MUST remain the single writer for user-visible focus, message identity, inbox revision, page state, and session-stage transitions.
3. `page_provider_http` and other workers MUST NOT mutate LVGL objects, card-view handles, focus fields, or presentation-owned buffers.
4. Presentation callbacks and animation ticks MUST NOT wait on a queue, semaphore, HTTP request, filesystem operation, parser, or worker completion. Any lock used by presentation MUST cover only bounded presentation state.
5. The client MUST use Level 0 property updates or bounded Level 1 card-subtree updates for page arrival and rebind. Page arrival MUST NOT replace the scene or recreate the scroll tree.

### CAR-GESTURE — continuous, bounded gesture behavior

1. During a user gesture, the visible window MUST remain represented by card surfaces. A card whose data is unavailable MUST show an explicit loading/skeleton state, not an empty hole or stale previous message.
2. A page fetch, parse, merge, or retry MUST NOT block, cancel, or wait for the gesture or snap animation.
3. Focus estimation during traversal MUST be updated from the gesture without requiring a completed remote fetch. Snap target calculation MUST use the current metadata count and local geometry only.
4. Repeated focus changes during one gesture MUST be coalesced to a bounded update rate. Queue submission failure MUST emit an observable event and MUST NOT silently discard the final focus intent.
5. Reversal before snap completion MUST cancel or supersede the prior snap through an explicit operation identity. The old completion callback MUST become a terminal `stale` outcome and MUST NOT rebind the pool to the old target.
6. The scroll/snap path MUST have a measured time budget. It MUST record p50, p95, p99, and worst callback/animation/update duration; averages alone are insufficient.

### CAR-BIND — bounded pool and stale-content prevention

1. The recycle variant MUST keep a declared maximum number of live card widgets. The maximum MUST be independent of total inbox count and recorded in the resource budget.
2. Every bind/unbind MUST carry a monotonically increasing binding generation and the bound message ID, index, and inbox revision.
3. A card view MUST transition through explicit states: `unbound`, `loading`, `bound`, `error`, or `stale`. Hidden/unbound MUST NOT be used as the only loading representation for an in-range item.
4. A card MUST be cleared or replaced atomically before it can display a different message. Old text, loading indicators, media placeholders, and asynchronous completions MUST NOT survive a generation change.
5. An asynchronous result MUST be applied only when its operation ID, session generation, page revision, and binding generation are still current. Otherwise it MUST produce a `stale`/ignored terminal trace with a normalized reason.
6. Rebinding the pool MUST preserve the current focus message ID where that ID still exists. Index-only continuity is insufficient when insertion or removal changes indices.

### CAR-PAGING — page arrival and loading presentation

1. Page fetch operations MUST use an explicit envelope containing operation ID, page index, submitted timestamp, immutable request revision, attempt number, cancellation/stale state, and exactly one terminal outcome.
2. Page arrival during scrolling or snapping MUST be merged into the model by the session owner and published as a bounded presentation update. It MUST NOT force a whole-carousel rebuild or move the viewport unexpectedly.
3. If the focused item is not materialized, the focused card MUST remain in position and display the loading state until success, failure, or cancellation is terminal.
4. A page failure MUST preserve already-valid neighboring content, mark the affected item/page unavailable, and expose retryable state without hiding unrelated cards.
5. A refresh with a newer inbox revision MUST reconcile by stable message ID. It MUST preserve the viewed message when present; if it was removed, it MUST select the nearest remaining item according to the existing owner decision O-04.
6. Older page results, results from a superseded revision, and results from a different session MUST be rejected as stale and MUST NOT mutate visible content.
7. Fetch retries MUST have a bounded attempt timeout, retry count/backoff, stopping condition, and terminal user-visible state. No indefinite loading state is permitted.

### CAR-INSERT-REMOVE — push and removal semantics

1. A push MUST preserve the current viewed message ID when that message remains in the model. It MUST NOT silently reset focus to index zero or to the newly pushed item.
2. Removing the viewed message MUST select the nearest remaining message, preserve the viewport when possible, and emit before/after focus IDs plus the applied revision.
3. Insert/remove reconciliation MUST be atomic from the presentation’s perspective: the session publishes one coherent snapshot containing model revision, focus index, focus message ID, visible-window identities, and loading/error states.
4. A push or removal received during a snap MUST either be applied to the pending snap’s immutable reconciliation input or defer the visual rebind until the snap reaches a terminal outcome. It MUST NOT apply half of the update and leave the snap callback with an obsolete index.
5. The client MUST distinguish accepted remote mutation, applied model revision, and visible presentation update. A server receipt alone MUST NOT be reported as a visible carousel effect.

### CAR-CONTRACT — user-visible operational behavior

For loading, refresh, page failure, and stale-result cases, the implementation MUST define and test:

- entry trigger;
- success state;
- failure classes;
- timeout and retry behavior;
- cancellation/supersession behavior;
- exact loading/error copy or an explicit decision that only a visual state appears;
- preserved focus/message identity;
- permitted background activity;
- never-acceptable outcomes.

At minimum, the carousel MUST NOT present a false-ready blank card, an indefinite spinner/loading state, another message’s content, a snap to an obsolete index, or a silent loss of the viewed item.

### CAR-BUDGET — resource envelope

The recycle and eager comparison variants MUST each declare and measure:

- maximum live card nodes;
- maximum transient bytes during rebind and page merge;
- maximum metadata/page bytes for 8, 16, 64, and 256 items;
- maximum concurrent page operations;
- queue capacities and overflow behavior;
- stack high-water for session, HTTP worker, and presentation owners;
- largest free block at steady state and during page apply/rebind.

Allocation failure MUST produce an observable terminal outcome and defined UI state. Increasing the inbox size MUST NOT increase live card-widget count in the recycle variant.

### CAR-OBS — required trace fields

Each gesture, snap, fetch, page apply, model revision, and bind MUST include, where applicable:

- session ID and operation ID;
- snap ID and binding generation;
- stage before/after;
- page index and inbox revision;
- focus index and before/after focus message ID;
- card slot, bound message ID, and visible/loading/error state;
- enqueue/start/terminal timestamps and normalized terminal reason;
- stale/cancelled/rejected classification;
- queue depth/overflow and resource high-water values.

Logs MUST remain free of credentials, raw private URLs, and message content beyond the stable test identifier needed for correlation.

## 4. Required work units

Each work unit requires a focused implementation, a fresh independent verifier, exact source hashes, and appended evidence. A failed verifier permits one focused repair and a new verifier; a second failure leaves the unit not fixed and blocks dependent units.

### C0 — baseline and reproduction

Freeze the current source/build/fixture revisions. Add deterministic reproductions for:

- fast traversal across a page boundary;
- delayed focused-page response;
- reversal before snap completion;
- page arrival during snap;
- push during snap;
- removal of the focused item during snap;
- stale result after focus/revision change.

Acceptance: each reproduction emits enough trace to identify the visible-window IDs, focus ID, snap ID, page revision, and terminal operation outcomes. No fix claim is made at C0.

### C1 — operation and session contract

Define the operation envelopes, single-writer transitions, queue capacities, stale-result rules, and loading/error states in headers and tests before changing rendering behavior.

Acceptance: L1 tests cover allowed/rejected transitions, queue-full behavior, cancellation/supersession, stale results, timeout, and exactly-one terminal outcome.

### C2 — continuous card presentation

Change card-pool and controller behavior so all in-range visible slots have a stable surface. Replace empty hidden holes with explicit loading/error card states. Keep the pool bounded and update only the affected card subtree.

Acceptance: L1/L2 tests show no blank slot during delayed page arrival, no stale prior-message content, no whole-scene rebuild, and constant live-card count for 8/16/64/256 fixtures.

### C3 — gesture/snap reconciliation

Make focus tracking and snap completion generation-aware. Coalesce rapid focus changes, supersede old snaps, and reconcile page/model changes against stable IDs rather than stale indices.

Acceptance: rapid reversal, delayed response, page arrival during snap, and stale snap completion all end with one coherent snapshot and no obsolete rebind. Input/animation timing stays within the declared budget in L2 scripted runs.

### C4 — push/removal identity preservation

Implement and test the owner decisions for viewed-ID preservation and nearest-neighbor selection after removal. Define the behavior when the focused item is outside the currently materialized page.

Acceptance: L1 model tests and L2 managed-server scenarios prove before/after focus IDs, applied revision, visible-window identities, and terminal outcomes. A remote receipt without a matching applied/visible update is not a PASS.

### C5 — observability and resource gates

Complete structured traces, metrics parsing, screenshots/screen video for visual continuity, and resource-budget measurements. Extend `metrics.v1` only through a reviewed schema change.

Acceptance: the workflow plan can correlate a client run with the exact scenario, firmware, server/fixture revision, and comparison run. Missing visual or timing evidence yields `INCOMPLETE`, not PASS.

### C6 — target confirmation

Run the canonical managed workflow on a representative registered kit for the recycle build and the eager comparison. Run `S-fix-8`, `S-fix-64`, `S-fix-256`, `S-trav-20`, `S-rev`, `S-delay`, `S-page-snap`, `S-rm-focus`, and `S-push` as applicable.

Acceptance: timing, memory, input, and lifecycle claims reach L3 with serial trace plus visual evidence. Any remaining blank viewport, wrong content, focus discontinuity, unbounded allocation, or terminal-outcome defect remains an open finding.

## 5. Verification matrix

| Requirement area | Minimum evidence | Required level |
|---|---|---|
| Ownership, transitions, stale results, terminal outcomes | Native/model tests with injected queue, timeout, cancellation, and revision cases | L1 |
| Bounded pool and no stale card content | Host/simulator scripted delayed-page and rebind tests | L2 |
| Gesture responsiveness and snap continuity | Scripted traces with p95/p99/worst callback and snap timing | L2, then L3 |
| Push/removal viewed-ID behavior | Managed server scenario plus before/after identity trace | L2, then L3 |
| Blank viewport and loading presentation | Screen capture synchronized with trace during delayed paging and reversal | L3 |
| Memory, stack, fragmentation, and queue limits | Representative hardware high-water/largest-block evidence | L3 |
| Eager versus recycle comparison | Same fixture/server revision and linked comparison run | L2/L3 |

Build success is only L0. Host/model success is not sufficient to close timing, memory, input, or visual defects.

## 6. Explicit open decisions

The implementation MUST NOT invent these product policies. Record a decision or preserve **OPEN** status before relying on them:

1. Exact loading-card copy and whether loading is skeleton, placeholder, or text-only.
2. Whether a failed focused page permits traversal past it and how retry is initiated.
3. Behavior when focus moves outside the materialized page window.
4. Whether a push should be visually inserted immediately or queued until the current snap completes.
5. Final numeric frame, snap, input-to-visible-content, memory, and blank-duration gates. The existing `p95 < 33 ms` reference is a draft lab target, not an approved product budget.
6. Whether the current three-view window is sufficient for the intended device geometry or requires a measured alternative.

## 7. Relationship to the workflow plan

The existing workflow plan remains responsible for the managed run and evidence plumbing. Its W8 hardware validation MUST invoke these carousel scenarios when this application plan is adopted, but W8 alone MUST NOT be described as a carousel fix. This plan may consume W7/W8 capture infrastructure; it does not authorize changes to host orchestration or private configuration.

The final report MUST keep these outcomes separate:

- workflow status;
- client application verification status;
- experiment verdict;
- physical target evidence;
- unresolved/open product decisions.

