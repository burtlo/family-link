# Client implementation review orchestrator

Use this workflow for one implementation under `client/labs/<lab>/` or
`client/device/`. Supply a target and, when relevant, a comparison base and
specification links. Invoke through the **client-code-review** skill in Codex
or Cursor. This document is a reusable workflow, not an automatically loaded
`AGENTS.md` file.

One orchestrator owns the review, dependency plan, assignments, and final
finding status. It delegates the existing standards review to a read-only
subagent, implementation to a fixing subagent, and verification to a separate
read-only subagent. All work is sequential: finish an assignment before starting
the next. Do not run parallel edits, reviews, or verification against a changing
source state. Additional specialist reviewers are outside this workflow's
current scope.

## Modes and authority

| User request | Orchestrator behavior |
|-------------|-----------------------|
| Review (default) | Review and plan; do not change implementation |
| Review, implement, and verify | Review, plan, then resolve confirmed defects within the authorized target |
| Fix / Fix and verify named findings | Use the accessible review context; plan and implement selected findings, with independent verification |
| Fix all / resolve confirmed findings after a review | Resolve confirmed defects from that review sequentially; exclude unproven risks and OPEN product choices |
| Verify named findings | Dispatch independent verification only; do not edit implementation |

A review request alone does not authorize implementation. An authorized
remediation request does not require permission again for routine assignments
or each confirmed defect. Product decisions remain with the owner; do not
silently expand the target, invent policy, or turn draft proposals into approved
behavior.

The orchestrator may save review and workflow evidence. Reviewer and verifier
subagents do not edit source or maintained documents; existing checks may
produce normal build/test artifacts. Only the active fixing subagent edits the
implementation. Preserve unrelated staged, unstaged, and untracked work.

If subagents are unavailable, report the limitation. A read-only review and
plan may still be produced directly, identified as such. Do not substitute the
fixer's self-check for mandatory independent verification or start remediation
that cannot receive it. Use an available separate agent context when possible;
manual cross-chat handoff is a fallback, not the normal user workflow.

## Orchestrator workflow

### 1. Establish the boundary and governing sources

Record target, scope slug, branch, HEAD, requested base, mode, and whether
committed, staged, unstaged, and untracked files are included. Without a base,
review the current implementation and related working-tree changes. Capture
an identifiable source snapshot, including relevant WIP: HEAD alone does not
identify reviewed uncommitted contents. Use a saved diff plus relevant
untracked contents or content hashes where needed. Do not commit, reset, stash,
or discard user work merely to establish provenance.

Read `docs/AGENTS.md` and the full
[client coding standard](client-application-coding-standards.md). Locate the
target's README, manifest, design notes, lab definition or product experience,
device UI specifications, communication contracts, and evidence report.
Follow relevant source links and verify their applicability; follow code into
shared components, host scripts, fixtures, servers, and tests that affect the
target.

The implementation author should make governing specifications findable in
the target's README or manifest. The reviewer must check those links and locate
applicable active sources, rather than assuming the coding standard supplies
all product requirements. Missing traceability is an evidence gap, not permission
to invent behavior.

Build an **applicable sources** table with document path and revision, relevant
sections or requirement IDs, authority/approval status, and affected paths or
scenarios. Use these connections within the existing review:

| Concern | Governing sources and connections |
|---------|----------------------------------|
| Engineering invariants | Coding standard; ownership map, operation envelopes, queues, and tests |
| Experience conformance | Applicable experience specification and owner decisions; stages, interruptions, failure UX, and preserved work |
| Presentation and lifecycle | Experience plus applicable screen, layout, component, foundation, and theme specifications; rendering paths and lifecycle evidence |
| Communication and persistence | Applicable communication contracts plus experience success/failure rules; client, fixtures/server, retry and side-effect paths |
| Resources and runtime | Applicable target budgets, lab plan, and runtime evidence; timing, allocation, input, and queue bounds |

These are source connections, not separate review profiles or new standards.
One document may govern multiple concerns. For example, an endpoint-login
experience can govern both stage behavior and UI continuity while delegating
geometry to a screen specification and wire details to a communication contract.

Approved active specifications and explicit owner-decided requirements govern
behavior within their recorded scope. A draft containing accepted decisions does
not imply approval of every statement or implementation readiness. Record OPEN,
proposed, and superseded material separately. Standards govern implementation;
plans and assessments supply scope and evidence. State source conflicts and the
required decision; do not silently choose the easiest implementation.

### 2. Dispatch the standards reviewer

Give a fresh read-only subagent the **Review assignment** below, the source
boundary, applicable sources, and relevant raw artifacts. Request one review
against the full standard and applicable specifications. Do not give it an
expected finding list. Wait for completion before changing source.

### 3. Consolidate findings and plan the work

Check each finding's trigger, effect, requirement, classification, and evidence.
Resolve duplicates by root cause, preserve counterevidence and unresolved
conflicts, and assign final finding IDs once. Do not turn unsupported suspicions
into confirmed defects. Keep the original finding text available throughout
remediation; append status and evidence rather than rewriting its history.

Produce an ordered **execution plan** for actionable findings with:

- finding IDs and one coherent change per work unit;
- true prerequisites, including product decisions or proof needed first;
- the reason each dependency exists and the condition that satisfies it;
- coupled findings that require one change and per-finding verification;
- verification scenarios and required evidence level;
- proposed order and rationale for independent work units.

If no dependency ordering is required, state **No dependency ordering required**
and explain the chosen sequential order (normally severity, then impact).
Shared files alone are not a prerequisite. Severity is not a dependency: an
underlying ownership fix may precede a higher-severity symptom when the causal
relationship requires it. Do not force all findings into one dependency chain.

Normally use one confirmed defect per work unit. Group findings only when the
same root cause or tightly coupled implementation requires one coherent change;
retain individual outcomes. A later fix may render a finding obsolete: recheck
its trigger before editing and record counterevidence instead of making an
unnecessary change.

In review-only mode, return the review and plan here. If there are no confirmed
defects, say so; do not manufacture a recommended fix or empty assignment.

### 4. Execute authorized work sequentially

For each eligible work unit, dispatch one fixing subagent with the **Fix
assignment** and complete context below. Wait for it to finish and stop editing.
Record its diff, relevant source state, tests, and per-ID explanation. Then
dispatch a different fresh subagent for **Verification assignment**. Do not reuse
the fixing agent as verifier or fork its full conversation into verification.
No source edits may run during verification.

The orchestrator transfers context itself. The user does not need to copy a
packet, open another chat, or reattach a report already available in this task.
Retain finding text, source provenance, assignments, and results in an accessible
review artifact or this task's context. For labs, saved review artifacts belong
under `docs/labs/evidence/<lab-slug>/reviews/`; for product work use a
user-designated evidence path or a task artifact. Saved artifacts must contain
complete findings, the plan, and appended outcomes, not just IDs.

When verification reports **not fixed** or demonstrates a regression, allow
one additional focused fix attempt by default, followed by a new independent
verification pass; honor a user-specified retry limit. Do not broaden the fix
or repeat indefinitely. If the limit is reached, record the failure and leave
dependent work blocked. **Unverified** due solely to missing evidence requires
evidence collection, not another code change or a consumed fix retry.
Independently executable authorized work may continue sequentially. An
unverified prerequisite remains unsatisfied. If a product decision is needed,
surface it without blocking unrelated work.

After all completed changes, run the relevant integrated checks once to detect
interactions. Reverify affected earlier findings if later changes invalidate
their evidence. Report any resulting failures; do not preserve a verified status
whose source state or supporting scenario is no longer valid.

### 5. Close with evidence and disposition

Return the review, execution order/dependencies, changes, commands/results, and
per-finding disposition with evidence level and source provenance. Distinguish
**verified fixed**, **not fixed**, **unverified**, **disproven**, **deferred**, and
**blocked** (state the prerequisite). OPEN choices and unproven risks remain
explicit. Report verification failures even when implementation tests passed.

Do not claim full completion while required evidence or dependent work remains
open. Device confirmation may require owner observations or target access;
state exactly what evidence is missing without calling a host build a device
verification.

## Finding IDs and assignment context

Use `<SCOPE>-F###`, for example `LAB02-F001`. The uppercase scope identifies the
target; the sequence is assigned once per report, initially in severity order.
IDs remain stable as status or execution order changes. One root cause gets one
ID. A new review has its own provenance; an ID is unique only with its report.

IDs are labels in a report, not lookup keys in source, issues, or git. When the
finding is available in this task or an attached/saved report, read that context
directly. For a bare ID with no accessible report, ask for the report or finding
text; do not search the codebase or git history for the ID. Existing pasted
packets remain accepted for compatibility, but are not required for normal
orchestration.

Each implementation or verification assignment must include:

```text
Finding: <report identity> / <SCOPE>-F00N
Target and scope: <target> / <scope>
Review provenance: <artifact or accessible context> / <HEAD and WIP snapshot>
Classification and priority: <confirmed defect | plausible risk | OPEN product> / Pn
Location: <absolute path>:<tight line range at reviewed state>
Trigger: <reachable steps>
Effect: <user-visible or invariant failure>
Requirement: <applicable standard/spec section or requirement ID and authority>
Smallest fix: <one coherent change class; advisory, confirm against current code>
Verify scenario: <original failure and relevant boundary case>
Required evidence: <L0-L3 and reason>
Dependencies: <IDs/decisions and satisfaction criteria, or none>
Current source state and permitted edits: <state, WIP ownership, assigned boundary>
```

For verification, also supply the exact fix diff/source state, changed files,
and implementation test artifacts. The verifier must inspect the source and
run or assess evidence independently, rather than accepting the fixer's summary.

## Review assignment (read-only subagent)

Review the assigned target for actionable defects and compliance with the full
client coding standard and applicable active specifications. Do not implement,
refactor, write implementation tests, or spawn fixing agents.

1. Confirm the supplied boundary and governing sources. Identify conflicts,
   OPEN decisions, and evidence gaps.
2. Use `docs/v1-assessments/v2-implementation-practices.md` to identify useful
   artifacts and verification gates. Read focused concept guides when the path
   needs them: async operation envelope, UI update taxonomy, session stage versus
   background poller, operational contract, and resource budget envelope. V1
   assessments are historical failure examples, not required architecture or
   measured results for this target.
3. Trace an ownership and wait map: input/timer → presentation → session →
   I/O/worker → completion → model → UI. Inspect actual calls and ownership,
   bounded presentation work, single-writer transitions, immutable operation
   input, exactly one terminal outcome, stale-result rejection, cancellation,
   timeout, queue-full behavior, UI handle lifetime, update level, preserved
   identity, resource bounds, persistence/remote side effects, and failure UX.
   Component names do not prove the rules are satisfied.
4. Derive nominal, interruption, failure, race, and boundary scenarios from
   applicable requirements and code. Establish a reachable trigger and violated
   invariant for each suspected defect. Run the smallest relevant existing test
   or static check when feasible. Distinguish defects, risks, evidence gaps,
   and OPEN choices; avoid style-only findings.
5. Return the report below to the orchestrator. Use temporary finding labels
   until the orchestrator assigns final IDs; include all required fields.

### Review report

Preserve these sections in the consolidated report:

1. **Review boundary** — target, scope, branch/HEAD, base, WIP snapshot, mode,
   edit policy.
2. **Applicable sources** — requirement connections, authority, conflicts, OPEN
   decisions.
3. **Ownership / wait map** — short diagram or call/ownership chain.
4. **Findings** — confirmed defects first, then plausible risks and OPEN product
   choices; severity within each group. Each finding has priority P0–P3,
   classification, absolute path/tight line range, trigger, effect, requirement,
   smallest coherent fix, verification scenario, and required evidence level.
5. **Residual risks** — tradeoffs and unresolved concerns, with finding references
   where applicable.
6. **Evidence gaps** — missing artifacts and what would close each; do not assign
   defect IDs to evidence gaps or git hygiene.
7. **Commands and results** — checks actually run and pass/fail.
8. **Evidence levels** — claim → achieved L0–L3 and supporting artifact.
9. **Execution plan** — prerequisites, sequential order, coupling, verification
   gates; explicitly state when no dependency ordering is required.
10. **Outcomes** — per-ID disposition and evidence, or “Review only; no fixes
    attempted.” Optional repository notes may record WIP constraints.

If there are no actionable findings, say so. A reviewer supplies findings and
observed relationships; the orchestrator owns the consolidated plan and outcomes.

## Fix assignment (one active fixing subagent)

Reproduce or trace the original trigger and confirm it still exists in the
current working tree. Read the cited requirement and applicable specifications.
Counterevidence may invalidate a finding; report it instead of editing needlessly.

- **Confirmed defect:** implement within authorized remediation scope.
- **Plausible risk:** do not implement a speculative fix. If authorized, gather
  focused proof first, using a test or trace where useful. The orchestrator may
  reclassify it as confirmed only with a reachable trigger and violated requirement.
- **OPEN product choice:** leave unresolved until the owner supplies the decision.

Make the smallest coherent change for the assigned failure class and preserve
unrelated WIP. Add or update focused tests when they prove a missing failure or
race boundary. Ownership/concurrency refactors must first establish existing
behavior through characterization tests or traces as required by the standard.
Do not expand features or silently settle product policy.

Return changed files, exact diff/source state, causal explanation per finding,
commands/results, evidence actually reached, and any remaining limitations.
Implementation self-checks are useful but do not replace independent verification.
Do not dispatch the next fix or verify your own change as the final judge.

## Verification assignment (independent read-only subagent)

Use a fresh agent distinct from the fixer, with the original finding,
requirements, source provenance, exact fix diff, and relevant raw evidence.
Inspect nearby callers and affected paths yourself. Attempt the original failure
and at least one relevant boundary case. Run focused checks and assess for new
blocking paths, stale handles/results, duplicate terminal events, lost work,
and unbounded resource use as applicable. Assess each finding in a coupled unit.

Do not edit implementation or maintained documents. Report **fixed**,
**not fixed**, or **unverified** per finding, with commands/results, exact source
state, evidence level, and missing evidence. If the original finding is invalid,
provide counterevidence for the orchestrator's **disproven** disposition. Report
regressions with their trigger and requirement; the orchestrator decides their
scope and subsequent assignment.

Apply the standard's verification ladder: build success is only L0; a
device-specific defect must not be called fixed below L2 without an explicit,
recorded verification waiver. Timing, memory, input, and lifecycle defects
normally require L3. Do not infer target confirmation from host tests. Only an
explicit user/owner instruction can authorize a waiver; record the finding,
waived evidence requirement, authorizing instruction, and remaining limitation
in the final outcome. The orchestrator, fixer, and verifier cannot grant
themselves a waiver, and inability to access hardware does not imply one.
Return failure evidence or evidence gaps to the orchestrator for the appropriate
retry or evidence-collection path, rather than repairing the code yourself.
