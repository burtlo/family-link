---
name: orchestrate-build
description: >-
  Orchestrate implementation packages from a project-independent implementation
  map. Delegate one package at a time to an implementation agent and then a fresh,
  independent read-only verification agent. Use when a map is ready for execution.
disable-model-invocation: true
---

# Orchestrate implementation packages

Run a package-based implementation map as an **orchestrator only**. Delegate each package to an implementation role, then require an independent verifier in a fresh context. Do not change product files in the orchestrator session. Keep package order, dependency gates, user checkpoints, repair limits, evidence, and progress state explicit.

This skill does not assume a particular repository layout, specification format, agent framework, slash command, model name, or background-execution feature. The map may have been created by another skill or process. Treat paths and workflow names in the map as inputs, not conventions.

## Inputs and defaults

Resolve these values from the user request, map metadata, repository instructions, and available agent/tool interfaces. Ask only for values that cannot safely be inferred and are needed to proceed.

| Input | Meaning | Default / resolution |
|---|---|---|
| `implementation_path` | Path to the implementation map | Accept a file path or a directory containing the map. If a directory contains multiple plausible maps, ask which to use. Do not assume a filename; discover it from context or ask. |
| `spec_path` | Optional source specification | Use the path referenced by the map or user. The map may be self-contained; do not require a file named `spec.md`. |
| `repository_root` | Root of the codebase being changed | Discover from the active workspace and repository metadata. Confirm ambiguity before delegating. |
| `packages` | Optional package-ID subset | Default to all packages in the map's declared build order. |
| `from_package` | Resume point | Start at this package; earlier packages are excluded unless explicitly selected. Re-evaluate dependencies and state first. |
| `repair_limit` | Maximum implementation-agent reruns after a verifier failure | Default `1` repair rerun per package. It is a cap, not a requirement to repair. |
| `auto_continue` | Whether to cross package checkpoints without another user response | Default `false`. |
| `progress_path` | Orchestration state file | Resolve from user/map configuration. If absent, propose a path beside the map named `orchestration-progress.md`; show it in the agenda. Create it only after the agenda is approved. |
| `progress_write_mode` | How to update progress | Default `append-owned-sections`: preserve all existing content and add/update only this skill's clearly delimited state section. Never truncate or replace unrelated content. |
| `implementer_role`, `verifier_role` | Agent role names and instructions | Discover from map metadata, repository guidance, or available agent definitions. If names are absent, use the environment's generic agent capability with explicit role prompts and any discovered role instructions. |
| `agent_instruction_paths` | Optional role instruction files | Discover and read when referenced or available. Do not assume `.cursor`, `agents/`, or particular filenames. |
| `agent_capabilities` | Available delegation, isolation, read-only, and result-return features | Inspect tool capability/schema. Do not assume a tool named `Task`, a particular `subagent_type`, model labels such as `fast`, or background controls. |

If no mechanism exists to launch separate agents, **stop** and report that orchestration cannot safely proceed. Do not implement the package yourself or substitute the orchestrator for the verifier. If a verifier cannot be made read-only, stop before verification and ask for a suitable capability or operator action.

## Required map content and readiness

Read the entire implementation map and any referenced specification excerpts or supporting documents needed for the selected packages. The map must provide, directly or by resolvable references:

- An ordered package list with stable IDs, titles, and dependency relationships.
- An implementation brief for each selected package, including scope and expected outcomes.
- A distinct verification brief or concrete, independently checkable acceptance conditions for each selected package.
- Enough requirement/specification context to evaluate the package.
- File or subsystem scope, when known, and explicit exclusions where needed.

Stop and report precise gaps if selected packages lack implementable briefs, dependencies are ambiguous/cyclic, or there are no independently checkable verification conditions. Do not silently invent missing scope or use another mapping workflow as a substitute.

## Orchestrator contract

1. **No product edits.** The orchestrator may inspect files, repository state, agent definitions, and tool capabilities. It may write only its own configured progress state, following the safe-write rules below. It must not edit source, tests, generated product documentation, the map, specification, or indexes. A user request to edit one of those artifacts is a separate task, not an implied exception to package orchestration.
2. **One package per implementation handoff.** Never bundle multiple packages into one implementation-agent assignment. Do not begin a package until every dependency has an accepted `pass` verdict, unless the user explicitly waives a named dependency in the conversation. Record any waiver and its rationale.
3. **Independent verification.** Launch a separate verifier invocation after implementation, in fresh context. Do not resume the implementer session as verifier. Provide the verifier with the frozen brief, relevant requirements, implementation report, and scoped change/evidence references; do not provide the implementer's private reasoning or tool transcript. The verifier must inspect the actual repository state and independently test the claims. It cannot edit files.
4. **No assumed agent features.** Use only delegation features confirmed available. A generic agent is acceptable when the environment has no named role, provided the full role contract and any discovered role instructions are included. Do not claim background execution, model selection, context isolation, or read-only enforcement unless the interface supports it. If fresh context or read-only verification cannot be ensured, stop.
5. **No implicit commits.** Neither the orchestrator nor its agents may commit, push, merge, publish, or create a pull request unless the user explicitly authorizes that action. Package completion means implementation and verification only.
6. **Preexisting changes are protected.** Before any implementation handoff, record the repository's baseline status and changed-file set. Treat every preexisting modified, staged, untracked, ignored, or otherwise user-owned file as protected. Give agents explicit exclusions and require them not to overwrite or “clean up” those files. If package scope overlaps a preexisting change, stop and ask the user how to proceed. Never reset, stash, discard, or restore user changes.
7. **Detect stale completion.** A prior `pass` in the progress file is not sufficient by itself. Before skipping or relying on it, compare the current map/package brief fingerprint (or captured revision/hash when available), repository baseline/current state, package file scope, and recorded verifier evidence. If the map changed, relevant files changed after verification, evidence cannot be reproduced, or completion cannot be tied to the current package definition, mark the prior result `stale` and re-run implementation and verification as appropriate. Never treat unknown state as current completion.
8. **Evidence over assertion.** Require reports to distinguish commands/checks actually run from suggested checks, include outcomes and relevant paths, and cite reproducible observations. A claim without evidence is not a passing verification.

## Safe progress-file handling

The progress file belongs to this orchestration run; it is not a general scratch file.

- Resolve and display `progress_path` before execution. Prefer a path beside the map unless configured otherwise.
- If it does not exist, create it only after agenda approval.
- If it exists and contains this skill's recognizable owned section/run ID, update only that section while preserving surrounding bytes/content.
- If it exists but has no recognizable owned section, do not overwrite, truncate, or append ambiguously. Choose an unused sibling filename or ask the user which path to use; show the choice in the agenda.
- Use a unique run ID and retain prior run records. Do not erase history to make the current run look clean.
- After writing, re-read the file and confirm unrelated content remains intact. If safe targeted updating is unavailable, stop progress-file mutation and report the limitation rather than risking data loss.

## Agent handoff contracts

### Implementation agent

Send one package per invocation. Include:

- Run ID, package ID/title, repository root, map path, and relevant specification path/excerpt.
- The complete frozen package brief and its explicit in-scope and out-of-scope boundaries.
- Dependency verdicts and any recorded user-approved waiver.
- Baseline changed-file list and protected paths; instruct the agent not to alter preexisting changes.
- Required implementation outcome and verification-relevant acceptance conditions.
- A request for a structured report only, with actual changed paths and checks/evidence.
- Explicit prohibition on commits, pushes, merges, PR creation, and work outside the package scope unless separately authorized.

If an implementation attempt is repaired, include the verifier findings and repair count. Do not let a repair expand scope; ask the user if it would.

### Independent verifier

Use a new agent invocation with no continuation/resume of the implementer. Enforce read-only behavior using available capabilities; state the prohibition in the prompt as well. Include:

- Run ID, package ID/title, repository root, map/specification context, full verifier brief, and current package definition fingerprint.
- Implementer structured report, changed-path list, baseline and current repository state, and a bounded package `change_scope` (or equivalent evidence references).
- Instructions to inspect actual files and run relevant checks where available, without editing files.
- A request for a structured `pass`, `fail`, or `blocked` verdict, each finding linked to evidence and acceptance conditions.

Do not send implementer tool traces, chain-of-thought, or unrelated conversation history. The verifier must not merely restate the implementer report.

### Required structured reports

Use the schema below as the common minimum. If an agent role defines a richer schema, accept it when these fields remain recoverable. Treat malformed, incomplete, or unparseable reports as `blocked`; request a corrected structured report once without granting a code change by the verifier.

Implementation report:

```json
{
  "run_id": "...",
  "package_id": "...",
  "status": "completed | blocked | failed",
  "summary": "...",
  "changed_paths": ["..."],
  "checks": [{"name": "...", "command_or_method": "...", "result": "passed | failed | not_run", "evidence": "..."}],
  "limitations": ["..."],
  "commit_created": false
}
```

Verifier report:

```json
{
  "run_id": "...",
  "package_id": "...",
  "verdict": "pass | fail | blocked",
  "findings": [{"severity": "...", "acceptance_condition": "...", "evidence": "...", "reproduction": "..."}],
  "checks": [{"name": "...", "method": "...", "result": "passed | failed | not_run", "evidence": "..."}],
  "scope_review": "...",
  "commit_created": false
}
```

## Phases

### 1. Resolve and ingest

1. Resolve `implementation_path` from a file or directory. If a directory is provided, discover the intended map; if more than one candidate exists, ask the user.
2. Resolve the repository root, optional `spec_path`, package-role instructions, agent/tool capabilities, and progress path. Record unresolved optional context; do not fabricate it.
3. Parse package IDs, titles, declared order, dependencies, briefs, verification conditions, and references. Confirm unique IDs and acyclic dependencies. Use the map's declared order when valid; otherwise use a stable topological order that respects dependencies and report the derivation.
4. Apply `packages` and `from_package`. Reject unknown IDs and report exclusions. A selected package whose dependency is omitted may proceed only if that dependency already has a current accepted pass or the user explicitly waives it. Selecting a dependent package does not silently select/skip its prerequisites.
5. Read existing progress and determine which records remain current under the stale-completion safeguards. Re-running a previously passing package requires explicit user intent unless its record is stale or invalid.
6. Inspect repository status before scheduling work. Capture staged, unstaged, untracked, and other visible changes as baseline. Check for overlap with selected package scope. If overlap exists or ownership is unclear, stop before agent launch.
7. If any selected package or required role cannot be resolved, stop with the exact missing information.

### 2. Agenda and approval gate

Present a concise agenda in chat with:

- Map path, repository root, optional spec path, and progress path.
- Selected package IDs/titles in execution order and omitted packages.
- Dependencies and the status/evidence supporting each prerequisite.
- Baseline dirty paths that will be protected, or a clear statement that no visible preexisting changes were found.
- Role names or generic-agent fallback, and confirmed relevant delegation capabilities.
- Per-package flow: implement → independent read-only verify → capped repair if needed → progress update.
- Whether `auto_continue` is enabled; default is no, so the user will be asked to continue after each package.

**Stop after the agenda.** Do not launch an implementation agent until the user approves the agenda or explicitly authorizes execution in the same request (for example, “run now”). A single selected package does not itself waive this gate. Honor an explicit “skip agenda” instruction, but still state the resolved plan briefly before acting.

### 3. Execute one package

For the next eligible package:

1. Recheck dependencies, current package/map definition, repository status, baseline-protected paths, and any completion record immediately before dispatch. If scope or state has materially changed since agenda approval, pause and present the change for approval.
2. Freeze the package brief and capture a package-definition fingerprint (for example, a hash of the brief plus dependency and verification sections) when practical. Record the repository revision and changed-file baseline.
3. Dispatch one implementation agent with the contract above. Wait for its report using supported tool behavior; do not assume synchronous/background settings.
4. If the report is `blocked`, `failed`, malformed after one correction request, or shows out-of-scope/preexisting-file changes, record the state and stop for the user. Do not verify unsafe or ambiguous changes as if normal.
5. Re-read current repository status and changed paths. Compare them with baseline and package scope. Preserve unrelated changes. If unexpected paths changed, stop and report them; do not revert them.
6. Dispatch a fresh, independent read-only verifier. Provide the implementer report and evidence references, but no tool trace. Verify the actual current package definition matches the frozen fingerprint.
7. On verifier `pass`, record a current passing result with evidence. On `blocked`, record and stop for operator input. On `fail`, apply the repair cycle below.
8. Update the progress file safely, re-read to confirm the write, and summarize package outcome and evidence in chat.
9. Unless `auto_continue` is explicitly true, stop and ask the user whether to continue to the next eligible package. A new user response is required before the next package begins.

### 4. Capped repair cycle

A verifier `fail` allows at most `repair_limit` implementation reruns (default one). Each rerun handles only the same package, uses a fresh implementation invocation where supported, includes specific verifier findings, and is followed by another fresh independent verifier. Do not increase the limit automatically or convert a blocked verdict into a failure to trigger repair.

When the repair cap is exhausted, the verifier remains `fail`, or required evidence cannot be produced, record the package as failed/blocked and stop. Do not advance to dependent packages. A user may authorize a new run or revised repair limit later; retain this run's history.

### 5. Resume and completion

On resume, reread the map and progress file and repeat the relevant baseline, dependency, and staleness checks. Resume means continue from the first requested eligible package; it does not mean trust every historical `pass`. `from_package` skips earlier packages only for queue selection; it does not waive their dependency requirements. A previous pass can satisfy a dependency only when its map fingerprint, repository evidence, and verifier result remain current.

When the queue finishes or stops, report packages as `pass`, `fail`, `blocked`, `stale`, `skipped`, or `not started`, and identify unmet dependencies. Mark the run `complete` only when every selected package has a current verifier pass. The orchestrator does not commit or create a PR unless separately authorized.

## Progress format

Use Markdown or another configured human-readable format. Preserve prior runs and surrounding content. Each run records enough information to resume and audit decisions.

```markdown
<!-- orchestrate-build:begin run_id=<unique-id> -->
# Orchestration progress

- Run ID: <unique-id>
- Status: planned | in_progress | blocked | complete
- Map: <implementation_path>
- Map/package fingerprint: <value or unavailable>
- Specification: <spec_path or self-contained/unspecified>
- Repository root: <path>
- Baseline revision: <revision or unavailable>
- Baseline protected paths: <paths or none visible>
- Started: <timestamp>
- Updated: <timestamp>

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| <ID> | <title> | <IDs or none> | not_started | not_started | 0 | <note> |

## Package <ID>: <title>

- Definition fingerprint: <value>
- Dependency evidence / waivers: <details>
- Implementer role and invocation: <role, invocation reference>
- Implementer report: <structured report or faithful summary>
- Baseline and changed paths: <details>
- Verifier role and fresh invocation: <role, invocation reference>
- Verifier verdict: pass | fail | blocked
- Findings and reproducible evidence: <details>
- Repair attempts: <count and findings addressed>
- Commit/PR/publish actions: none unless explicitly authorized; record authorization if applicable
- Final state: pass | fail | blocked | stale | skipped | not_started

<!-- orchestrate-build:end run_id=<unique-id> -->
```

If a progress path cannot safely retain multiple runs, do not overwrite it. Use a new path or ask the user to choose. Keep structured reports concise enough to audit while retaining the evidence needed to reproduce decisions.

## Stop conditions

Stop before or during execution when any of the following occurs: required agent capability is unavailable; role separation/fresh context/read-only verification cannot be ensured; map readiness fails; dependencies are unresolved; package scope overlaps protected preexisting work; repository or map state changes materially after agenda approval; an agent exceeds scope; a report is blocked or unusable; a verifier fails beyond the repair cap; or progress cannot be safely recorded. Record the observed state, avoid destructive cleanup, and tell the user what input or capability is needed.

## Anti-patterns

- The orchestrator changes product files to “save time.”
- The orchestrator implements work because a subagent feature is missing.
- The verifier resumes the implementer context, edits files, or merely repeats the implementer's claims.
- Several packages are assigned in one implementation handoff.
- A dependency is skipped because it was omitted from the selected subset.
- A historical pass is trusted after the map or relevant repository state changed.
- Preexisting work is reset, stashed, discarded, or overwritten.
- A progress file is truncated or repurposed without confirming ownership.
- Tool names, agent types, model labels, asynchronous behavior, or repository conventions are assumed without checking.
- A package is marked complete without reproducible, independently checked evidence.
- A commit, push, merge, publication, or PR is created without explicit authorization.
