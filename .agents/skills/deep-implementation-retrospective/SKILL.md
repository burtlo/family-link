---
name: deep-implementation-retrospective
description: Performs an evidence-based engineering retrospective that reconstructs failure chains, examines runtime architecture and performance paths, and converts project-specific defects into portable coding standards and automated guardrails. Use when reviewing a debugging session, repeated build/runtime failures, lockups, reboots, latency, queue races, redraw failures, or fixes that caused regressions.
disable-model-invocation: true
---

# Deep implementation retrospective

Produce a retrospective that explains not only how the agent worked, but why the
implementation failed and which reusable engineering constraints would prevent
the same class of failure in a future version, different framework, or different
hardware target.

Do not edit the project unless the user separately asks for implementation. The
retrospective itself is an analysis task.

## 1. Establish scope and evidence

If the user names a session, incident, branch, time range, or feature, use that
scope. Otherwise use the current session and the implementation it investigated.

Read primary sources before relying on summaries:

- [`docs/AGENTS.md`](../../docs/AGENTS.md) and applicable [`docs/standards/`](../../docs/standards/client-application-coding-standards.md) (v1 POC code under `poc-v1/` is read-only reference per [`.cursor/rules/poc-v1-reference-archive.mdc`](../../.cursor/rules/poc-v1-reference-archive.mdc));
- user and agent transcripts;
- terminal/build output;
- runtime logs, crash traces, reset reasons, and saved evidence;
- code before and after relevant changes, including version-control history when
  available;
- tests, build scripts, linter/check commands, CI, and pre-commit hooks;
- requirements, operational contracts, coding standards, rules, and skills.

Treat prior retrospective documents as leads, not proof. Verify their important
claims against code or primary runtime evidence where possible.

For every material claim, record:

- evidence source;
- confidence: confirmed, strongly supported, plausible, or unknown;
- whether it describes a symptom, proximate cause, systemic cause, contributing
  condition, or failed fix.

If evidence is missing, say exactly which artifact would distinguish the leading
hypotheses. Do not turn an unobserved theory into a root cause.

## 2. Reconstruct incident chains

Build a compact chronology of distinct failures and attempted fixes. Preserve the
order because a later failure may be a regression introduced by an earlier fix.

For each incident capture:

```text
Incident
  expected behavior
  observed behavior
  trigger and input
  build/runtime environment
  implementation change immediately before it
  diagnostic evidence captured
  attempted fix and its theory
  verification actually performed
  outcome: fixed | partial | unresolved | unknown
  later regressions or related incidents
```

Distinguish:

- build failure from runtime failure;
- tool-initiated reset from application crash, watchdog, assertion, power loss,
  or unknown reboot;
- responsiveness failure from rendering failure;
- logic failure from resource exhaustion;
- “build/flash succeeded” from behavior being verified.

Look explicitly for fixes that changed the failure signature without removing the
underlying cause.

## 3. Map the runtime architecture

Before recommending process or documentation changes, map the execution model of
the affected path:

```text
user input
  -> presentation callback/tick
  -> session/state owner
  -> queue or command
  -> worker, network, storage, media, or device operation
  -> progress/terminal event
  -> state transition
  -> property, subtree, or scene update
```

Identify:

- execution contexts: UI/render thread, event loop, workers, interrupts, timers,
  pollers, callbacks;
- the owner of user-visible stage and domain state;
- queues, semaphores, locks, shared flags, and queue capacities;
- every blocking, sleeping, polling, allocation-heavy, or unpredictably slow call
  reachable from presentation;
- mutable inputs read after asynchronous submission;
- result identity, cancellation, timeout, stale-result, and terminal-event rules;
- presentation handles retained across tree destruction;
- background pollers that can conflict with active user operations;
- retry loops and duplicated side effects.

If ownership cannot be stated in one sentence, treat ambiguous ownership as a
finding.

## 4. Perform a dedicated performance and liveness pass

Do not equate performance with benchmark speed. Analyze whether time, memory,
contention, or scheduling can change correctness or make the product appear
locked.

Inspect:

### Critical-path work

- callback, render, and animation duration;
- network, filesystem, peripheral, codec, join, or sleep calls on presentation;
- lock acquisition and hold time;
- synchronous retries;
- scene construction and full collection rebuilds;
- duplicate parsing, decoding, copying, or allocation.

### Queues and concurrency

- queue capacity and overflow behavior;
- ignored enqueue failures;
- event coalescing versus non-droppable terminal events;
- producer bursts and consumer starvation;
- operations that can complete out of order;
- worker results that can overwrite newer state;
- busy/dirty/stop Boolean combinations hiding an implicit state machine.

### Rendering and lifecycle

Classify updates by behavior:

- Level 0: property update;
- Level 1: identity-keyed subtree update;
- Level 2: scene replacement.

Flag higher-level updates used for progress, timers, playback completion, data
arrival, or connectivity badges unless a user-visible stage actually changed.
Check preservation of focus, selection, scroll, input, active operation identity,
animations, timers, and presentation handles.

### Resource budgets

Look for explicit or missing budgets for:

- live presentation nodes;
- persistent and transient memory;
- largest single allocation and fragmentation;
- stack/call depth;
- input and media size;
- queue depth;
- concurrent operations;
- callback, frame, scene-build, and cancellation time.

Inspect boundary behavior. Crash, reboot, white screen, silent event loss, false
success, or indefinite progress are not valid degradation strategies.

### Required output of this pass

For each important path, state:

1. the latency or resource-sensitive work;
2. who owns it;
3. what it can block or starve;
4. which user-visible symptom follows;
5. whether measurement exists;
6. the smallest structural correction.

## 5. Analyze state and operation correctness

For every operation that outlives its initiating callback, assess whether it has:

```text
Operation
  unique or monotonic identity
  immutable input snapshot
  owning session identity
  deadline and retry attempt
  non-blocking cancellation
  optional progress events
  exactly one terminal outcome:
    success | failure | cancelled | stale | timed_out
```

Check that one session owner applies outcomes and chooses the next user-visible
stage. Silent returns, clearing a busy flag without reconciliation, and directly
mutating presentation from workers are findings.

Separate:

- user session stage;
- dependency/connectivity truth;
- background poll schedule.

For each poller result during each active user operation, require one explicit
policy: ignore, defer, merge, cancel, or transition.

## 6. Evaluate the operational contract

For every user-visible wait or long-running action, determine whether the product
defines:

- entry trigger and immediate visible acknowledgement;
- success stage;
- distinct failure classes and truthful copy;
- attempt timeout, total deadline, retry, and stopping condition;
- cancellation;
- retained or discarded user work;
- permitted background activity;
- never-acceptable outcomes;
- differences across product surfaces.

Mark unresolved behavior OPEN. Do not mistake current implementation behavior for
a product decision.

## 7. Examine existing guardrails before proposing new ones

Read the repository’s actual check commands and CI/pre-commit wiring first.

Classify each proposed rule:

- **Mechanical:** fixed syntax, banned dependency direction, forbidden call site,
  direct state assignment, ignored queue result, missing operation ID, hardcoded
  generated value. Implement as a deterministic linter, static check, test, or CI
  job.
- **Judgment:** correct operation boundary, adequate preserved identity, truthful
  degraded behavior, representative resource budget. Put in review standards.
- **Navigation:** hidden ownership or source-of-truth location. Add a short pointer
  from AGENTS.md to a focused document.
- **Operational evidence:** missing logs, traces, metrics, reset reason, or saved
  runtime artifacts. Improve information capture.

An existing check that is unwired, too narrow, or green while the behavior is
broken is itself a finding.

## 8. Translate findings into portable guidance

Do not stop at file-, framework-, library-, board-, or task-name-specific advice.
For every major finding provide both:

```text
Current implementation
  concrete evidence and immediate correction

Portable rule
  failure class
  invariant
  preferred structure
  mechanical enforcement, if possible
  reviewer question when judgment is required
  runtime evidence that proves compliance
```

Good portable rules describe ownership, bounded work, operation identity, update
level, resource envelope, backpressure, or verification. Avoid rules whose only
content is “use API X” or “edit file Y.”

Test portability by asking:

> Would this still guide a rewrite using another UI framework, scheduler,
> language, networking library, and hardware class?

If not, generalize it while retaining the concrete current-project example as
evidence.

## 9. Perform a second-pass challenge

Before writing the result, challenge the initial explanation:

- What symptom was mistaken for the root cause?
- What expensive or blocking work was hidden behind a harmless-looking helper?
- Which fix treated repaint/retry/timing rather than ownership or lifecycle?
- Which missing measurement allowed confident but wrong diagnosis?
- Which check could pass while the behavior remained broken?
- Which shared flag or callback was an implicit state machine?
- Which later regression links two incidents previously treated as separate?
- Which recommendation would become useless after a rewrite?

Revise the findings if this pass exposes a deeper architectural or performance
cause.

## 10. Present findings in severity order

Use this output structure:

### Executive conclusion

State the dominant systemic failure pattern and the highest-leverage improvement.

### Incident and causal chains

Summarize the chronology and distinguish confirmed causes from unknowns.

### Implementation findings

For each finding include:

- severity and confidence;
- evidence;
- user-visible consequence;
- architectural/performance mechanism;
- immediate project correction;
- portable invariant and preferred structure;
- enforcement and verification.

Prioritize correctness-changing performance and liveness issues: presentation
blocking, queue loss, lock contention, stale outcomes, scene rebuilds, resource
exhaustion, and unbounded retries.

### Reusable standards

Provide a concise set of framework- and hardware-agnostic MUST/MUST NOT/SHOULD
rules. Avoid duplicating rules already present in the repository; identify the
existing standard and recommend a clarification when sufficient.

### Guardrail plan

Order deterministic checks, tests, review standards, navigation pointers, and
information-access improvements by expected risk reduction and implementation
cost.

### Evidence gaps

List unresolved hypotheses and the exact trace, metric, fault injection, or test
needed to resolve each.

## Quality bar

The retrospective is incomplete if it:

- focuses mainly on agent tool usage while implementation failures are present;
- recommends documentation where a deterministic check is feasible;
- reports only symptoms or local fixes;
- treats compile, flash, deploy, or test startup as runtime verification;
- omits presentation critical paths, queues, locks, operation lifecycle, render
  lifecycle, or resource budgets;
- presents framework-specific advice without a portable invariant;
- claims root cause without primary evidence;
- invents unspecified product behavior;
- produces a long list without identifying the dominant causal structure.
