# Client application coding standards

Status: normative guidance for the next client generation  
Applies to: device clients, browser twins, native clients, and simulators  
Does not require: ESP-IDF, FreeRTOS, LVGL, or any specific hardware

These standards capture the transferable lessons from the v1 client. The companion
v1 assessments are evidence for the rules, not architecture that a new client must
copy:

- [Carousel and playback](../v1-assessments/carousel-playback.md)
- [Authentication and connectivity](../v1-assessments/authentication-connectivity.md)
- [Recording and send](../v1-assessments/recording-send.md)

Normative words have their usual meanings: **MUST**, **MUST NOT**, **SHOULD**, and
**MAY**.

## 1. Architectural boundaries

Every interactive client MUST separate three responsibilities, even if a small
implementation keeps them in one process:

1. **Presentation** owns input dispatch, animation, focus, and rendering.
2. **Session** owns user-visible stage, domain state, and transition invariants.
3. **I/O and workers** own network, storage, codec, device joins, and long-running
   computation.

Dependencies flow in one direction:

```text
presentation --intent--> session --command--> I/O or worker
presentation <--snapshot-- session <--event--- I/O or worker
```

- Presentation MUST NOT perform blocking or unpredictably slow I/O.
- Workers MUST NOT mutate widgets or presentation-owned handles.
- Workers MUST report results as events; the session owner decides the resulting
  stage.
- Session transitions MUST have one owner. Other modules post intents or events.
- Code MUST NOT use the visible screen, a widget pointer, or an incidental flag as
  the source of truth for whether an operation is valid.

### Review question

For every new call, ask: “Can this wait on a network, filesystem, peripheral,
lock, queue, codec, or another task?” If yes, it does not belong in a presentation
callback or render tick.

## 2. Bounded presentation work

Input and animation latency must remain bounded independently of I/O latency.

- An input callback MUST complete without waiting for remote acknowledgement.
- A render or animation tick MUST have a documented time budget.
- Locks held by presentation MUST cover only presentation data and rendering calls.
- Presentation MUST NOT sleep, poll a worker, or spin until a flag changes.
- A callback MAY update local state optimistically and enqueue persistence or sync.
- Repeated sync caused by scrolling, scrubbing, or typing SHOULD be coalesced.
- Queue submission failure MUST be observable. Silently dropping a state-changing
  event is forbidden.

Targets SHOULD be established per product. A practical starting point is:

- input callback: less than one frame budget;
- animation/update tick: predictable and measured at the 95th and 99th percentile;
- no operation on the presentation owner with a timeout measured in seconds.

## 3. Single-writer session state

The session stage controls what the user may do. It is distinct from connectivity,
worker status, and the current visual tree.

- Exactly one component MUST apply session transitions.
- Every transition MUST identify its trigger and permitted source stages.
- Background probes MAY report connectivity; they MUST NOT directly replace the
  current user stage.
- A background result MUST NOT erase partial input or supersede an active user
  operation unless the product contract explicitly permits it.
- Direct state assignment from workers and feature modules is forbidden.
- State-event queues MUST define capacity, overflow behavior, and telemetry.

At minimum, tests MUST cover:

- allowed transitions;
- rejected transitions;
- stale worker results;
- queue-full behavior;
- a background failure while a user operation is running.

## 4. Asynchronous operation envelope

Every operation that can outlive its initiating callback MUST use an explicit
envelope:

```text
Operation
  id             monotonic generation or unique identifier
  kind           login, probe, play, upload, save, ...
  submitted_at   monotonic timestamp
  input          immutable snapshot captured at submission
  status         pending | running | terminal
  cancellation   requested flag or token

Terminal outcome (exactly one)
  success(result)
  failure(reason, retryable)
  cancelled(reason)
  stale(reason)
  timed_out(reason)
```

- Input MUST be frozen at submission. Workers MUST NOT reread mutable form, focus,
  recipient, or session fields as operation input.
- Applying a result MUST compare the operation identifier with the current
  operation.
- Every accepted operation MUST produce exactly one terminal outcome.
- `stale` and `cancelled` are outcomes, not silent returns.
- Cancellation MUST be non-blocking for the caller.
- A timeout MUST create a terminal outcome; it MUST NOT merely hide the UI.
- Retry MUST create a new attempt identity or increment an attempt counter.
- The session owner MUST define the user-visible stage after every outcome.

Boolean collections such as `busy`, `stop`, `armed`, `dirty`, and `playing` MUST
NOT substitute for the operation envelope when their combinations describe a
state machine.

## 5. UI update levels and lifecycle

Use the smallest update that satisfies the behavior:

- **Level 0 — property update:** text, progress, enabled state, opacity, icon.
- **Level 1 — subtree update:** a card, list diff, panel, or bounded collection.
- **Level 2 — scene replacement:** a user-visible stage change.

Rules:

- Level 2 MUST be caused by a session-stage transition.
- Data arrival, progress, playback completion, and connectivity badges SHOULD use
  Level 0 or Level 1.
- Level 1 MUST name the identity keys it preserves, such as message ID, focus ID,
  selection, and scroll position.
- Before Level 2 destroys a tree, modules holding presentation handles MUST
  invalidate them.
- A handle from a destroyed tree MUST never be tested or read except through a
  framework-provided validity mechanism.
- Re-entering a scene MUST derive it from a session snapshot, not from surviving
  widget state.
- Rendering MUST be idempotent for the same snapshot.

## 6. Pollers and connectivity

Connectivity truth and user session stage are separate concerns.

- Probes and heartbeats are lower priority than an in-flight user operation.
- A poller MUST declare whether its result is ignored, deferred, or merged while a
  user operation is active.
- “Transport reachable” MUST NOT be treated as “application ready” without the
  required application-level response.
- Retry policy MUST define attempt timeout, interval or backoff, stopping
  condition, and jitter where multiple clients may synchronize.
- Cached data MUST be labeled as cached and MUST NOT imply that remote actions are
  available.
- Offline behavior MUST state which local actions remain available.

## 7. Operational contract

Every user-visible wait or long-running action MUST have a checkable contract with:

- entry trigger;
- success stage;
- each distinct failure class;
- timeout and retry behavior;
- cancellation behavior;
- exact user-visible copy or an explicit decision that no copy appears;
- preserved user input or work;
- permitted background activity;
- never-acceptable outcomes.

Universal never-acceptable outcomes:

- indefinite in-flight UI with no terminal event;
- false-ready UI while required dependencies are unavailable;
- transport failure represented as a credential or user error;
- silent loss of user-created work unless the product explicitly accepts it;
- exposing hostnames, ports, secrets, stack traces, or operator commands to users.

Unresolved product behavior MUST be marked **OPEN**. Implementations MUST NOT invent
retry, outbox, cache, or failure UX to fill an unspecified contract.

## 8. Resource budgets

Each scene and long-running operation MUST declare a resource envelope appropriate
to the target:

```text
ResourceBudget
  max_live_nodes
  max_transient_bytes
  max_persistent_bytes
  max_concurrent_operations
  max_stack_or_call_depth
  maximum_input_size
```

- Budgets MUST be measured on representative hardware or runtime configurations.
- Allocation failure MUST have a terminal operation outcome and defined UX.
- Large media MUST be streamed or bounded unless a measured requirement justifies
  whole-object buffering.
- Rebuilding an entire collection MUST be justified against an incremental update.
- Measurements SHOULD include high-water marks and largest allocatable block where
  fragmentation matters.

## 9. Persistence and remote side effects

- Local domain state and remote synchronization state MUST be distinguishable.
- High-frequency side effects SHOULD be debounced or coalesced.
- Remote writes MUST be idempotent or carry an operation/idempotency key when a
  retry could duplicate user work.
- Multi-recipient or multi-step writes MUST define atomicity: all-or-nothing,
  partial success, or compensating action.
- A client MUST NOT report success until the contract’s success condition is met.
- If failed user work is discarded, that behavior and copy MUST be explicit.

## 10. Observability

Every asynchronous operation SHOULD emit structured events containing:

- operation ID and kind;
- stage before and after;
- attempt number;
- elapsed time;
- terminal outcome and normalized reason;
- resource high-water information where relevant.

Logs MUST avoid secrets and raw credentials. Reset or restart diagnostics MUST
distinguish tool-initiated reset, user reset, watchdog, assertion, and power loss
where the platform permits it.

## 11. Verification ladder

Claims use these levels:

- **L0 — build:** compile, typecheck, and static checks pass.
- **L1 — model:** transition, operation, and contract tests pass.
- **L2 — scripted runtime:** the affected path is reproduced on a simulator,
  twin, or target, with saved evidence.
- **L3 — target confirmation:** representative target behavior is confirmed with
  logs, trace, screenshot, or explicit human observation.

- Build success proves only L0.
- A device-specific defect MUST NOT be called fixed below L2 unless the report
  states the verification waiver.
- Timing, memory, input, and lifecycle defects normally require L3.
- Cross-surface parity requires the same contract scenario on each claimed
  surface; visual similarity alone is insufficient.

## 12. Change discipline

- A change set SHOULD address one failure class.
- Authentication, rendering lifecycle, connectivity, and media changes SHOULD be
  separated when any one is under active diagnosis.
- Refactors that alter ownership or concurrency MUST first add characterization
  tests or traces for the existing behavior.
- Feature scope MUST freeze while a reliability regression is unresolved.
- Generated timing or policy artifacts MUST have one source and a parity check.

## 13. Enforcement

Use deterministic checks for mechanical rules:

- blocking APIs imported or called from presentation modules;
- direct session-state assignment outside the owner;
- ignored queue-send results;
- hardcoded timing values that should come from shared policy;
- scene replacement from progress or data callbacks;
- worker access to presentation handles;
- missing operation ID in worker completion events.

Use review judgment for:

- whether an update is Level 0, 1, or 2;
- whether preserved identity keys are sufficient;
- whether an operation boundary is correctly sized;
- whether degraded behavior tells the truth;
- whether a resource budget is representative.

## 14. Reviewer checklist

1. Is blocking or unbounded work absent from presentation callbacks and ticks?
2. Is there one owner for session transitions?
3. Does each asynchronous operation freeze input and terminate exactly once?
4. Can stale, cancel, timeout, and queue-full paths reconcile the UI?
5. Is the chosen UI update level the smallest correct one?
6. Are focus, selection, scroll, and identity preserved across subtree changes?
7. Can a poller overwrite an active user operation?
8. Are retries, failures, and user-visible copy defined by the contract?
9. Are memory, input-size, queue, and concurrency limits explicit?
10. Is the claimed verification level backed by an artifact?
