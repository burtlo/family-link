---
name: translate-spec-to-implementation
description:
  Turn a specification into a repository-grounded implementation map that an engineering lead can use to assign implementation work and independent verification. The map explains what the specification requires, what the repository already does, what remains to change, and how the work can be delivered in a safe order.
disable-model-invocation: true
---

# Specification to Implementation Map

## Purpose

Turn a specification into a repository-grounded implementation map that an engineering lead can use to assign implementation work and independent verification. The map explains what the specification requires, what the repository already does, what remains to change, and how the work can be delivered in a safe order.

This skill investigates and plans. It does not implement product changes unless the user separately asks for implementation. Treat the supplied specification as normative. Repository behavior, tests, and supporting documents are evidence; they do not silently change the specification.

## Inputs

Accept these inputs in any combination supported by the request:

- `spec_path` — required path to the specification. It may live anywhere and does not need a prescribed filename or directory layout.
- `output_path` — optional destination for the completed map. It may be absolute or relative. Resolve a relative path from `repo_root`, when supplied, or otherwise from the current working directory.
- `focus` — optional requirement, decision, topic, or workstream to prioritize. Unless the user requests a partial map, still cover the full in-scope specification.
- `repo_root` — optional repository root. If omitted, identify the repository root from the current workspace. If no repository is available, explain the limitation and produce only the portion of the map supported by available evidence.

Read the full specification and any materials it explicitly references that are available and relevant. A prior map or other planning artifact can help, but is not ground truth: verify its claims against the current specification and repository. Do not assume a particular interview, retrospective, workflow, command, source tree, framework, or documentation convention exists.

## Output location and collision handling

When `output_path` is supplied, use it if it does not exist or if inspection shows it is the existing map for this same specification and the user has authorized updating it. Do not replace an unrelated file. If the requested path is occupied by an unrelated artifact, choose the next available sibling name by adding `-2`, `-3`, and so on before the extension, and report the actual path used.

When `output_path` is omitted, place `implementation-map.md` beside the specification. If that name is already used by an unrelated artifact, choose the next available numbered sibling name using the same rule. If the specification cannot be written beside because of permissions, report the issue and ask for or choose a writable destination appropriate to the user’s request.

Do not create a second co-equal map when a clearly related map already exists. Update that map only after the user authorizes publication; otherwise prepare the proposed revision for review. Do not delete, rename, archive, or supersede other planning files unless the user explicitly requests that action.

## Working principles

### Start with the specification

- Extract scope, goals, exclusions, constraints, user-visible behavior, interfaces, material decisions, dependencies, and verification expectations before exploring implementation details.
- Keep requirements and implementation suggestions distinct. Label a suggestion that is not mandated by the specification.
- Quote or paraphrase only what is needed to trace a requirement; do not duplicate the entire specification.
- If the specification points to other normative documents, identify which source governs when they disagree. Surface unresolved conflicts instead of choosing silently.

### Establish traceability

- Preserve explicit acceptance-criteria, requirement, and decision IDs exactly as written.
- For each testable requirement or decision without an ID, assign a map-only stable requirement ID. Use a compact form such as `R-<section-slug>-001`, scoped to its source section and numbered in document order. Do not edit the source specification to add IDs.
- When revising a prior map, preserve its derived IDs for requirements that still correspond, even if nearby requirements were added or reordered. Assign new IDs only to new requirements. If correspondence is uncertain, record that uncertainty instead of silently reusing or renumbering an ID.
- Make every in-scope requirement traceable to repository evidence, a documented gap or unknown, and at least one implementation package when work is required. Requirements already fully satisfied may be marked as such without a package.
- Link to the relevant heading or page in the specification when possible. For unnumbered requirements, include the derived ID and a concise description so the reference remains understandable.

### Ground findings in repository evidence

- Explore the code, configuration, interfaces, tests, build and deployment setup, and documentation that are relevant to the requirements. Follow actual repository structure; do not impose a fixed list of path types or surfaces.
- Cite repository paths for claims about current behavior. Use links relative to the map when practical, and use stable path or heading references for other formats.
- Distinguish verified behavior from inference, assumptions, and unknowns. Do not infer that a behavior is implemented merely because a file or symbol exists.
- Name relevant entry points, modules, components, configuration, tests, and operational resources. Include a concise repository map of the most useful path anchors.
- Record prerequisites discovered in the repository even when the specification does not call them out.

### Describe the delta explicitly

For each material gap or clarification, provide all three parts:

| Present state | Required state | Suggested resolution |
|---|---|---|
| What repository evidence shows today, or what is unknown | What the specification requires, with a traceable ID | A concrete way to close the gap, clearly identified as a proposal where not specified |

Mark requirements as `present`, `partial`, `missing`, or `unknown` (or an equally clear status). Use `partial` when existing behavior does not fully satisfy the requirement. Never hide an unresolved product or technical decision inside a package brief.

### Make work assignable and verifiable

- Group changes into implementer-sized packages (`P1`, `P2`, …). A package should have a clear outcome, bounded deliverables, known touchpoints, dependencies, and a way to verify completion.
- Prefer packages that can be assigned independently when repository boundaries allow it. State dependencies where they do not.
- Map every package to requirement IDs and relevant tests or verification hooks. Workstream prose alone is not sufficient traceability.
- Write an implementer brief and a verifier brief for every package. Verifier briefs state what must be proven, the most useful tests or probes, and the regressions to watch for. If exact commands cannot be established, say what must be run or inspected and why the command is unknown; do not invent commands.
- Order packages around real dependencies and any sequencing constraints in the specification. Explain meaningful ordering choices, especially when multiple valid sequences exist.

### Preserve unknowns and risks

- Record missing information, conflicting sources, and assumptions as explicit unknowns or clarifications, with impact and the decision or evidence needed to resolve them.
- Assess relevant risks based on the specification and repository, such as compatibility, data handling, security, reliability, supported environments, release process, or test availability. Include only categories that apply.
- Describe validation limits honestly, including unavailable services, credentials, hardware, environments, or fixtures. Do not claim verification that was not performed.

## Readiness assessment

Before producing a full implementation map, check whether the specification gives enough information to plan safely. Assess:

1. Whether the goal, boundaries, and exclusions are understandable.
2. Whether user or system behavior is described in testable terms, through explicit acceptance criteria or equivalent observable outcomes.
3. Whether material choices and interfaces are decided, or can be represented as bounded, clearly labeled questions without making the plan misleading.
4. Whether dependencies and constraints that could change package boundaries or build order are known or explicitly called out.
5. Whether a verifier could determine what evidence would demonstrate completion.

Classify readiness as one of:

- **Ready** — scope and observable outcomes are clear enough to map.
- **Ready with explicit unknowns** — planning can proceed, and remaining questions are bounded, visible, and do not invalidate package boundaries.
- **Preview only** — material gaps prevent a reliable implementation plan, but the user requested an exploratory preview.
- **Not ready** — a missing or conflicting decision makes the plan materially unreliable.

For **Not ready**, do not publish a full map. Give a concise readiness note naming the blocking questions and their impact. For **Preview only**, label every preview artifact clearly and keep tentative choices separate from requirements. Do not require a particular upstream workflow to resolve questions; point to the exact spec section or decision that needs clarification.

## Workflow

### 1. Ingest and set the boundary

- Confirm `spec_path`, locate `repo_root` if possible, resolve the output destination, and note the requested `focus`.
- Read the specification end to end. Inventory explicit IDs and derive stable map-only IDs for unnumbered requirements or decisions that need traceability.
- Inspect relevant referenced documents and any existing map. Check whether the existing map is for this same specification and note stale claims for re-verification.
- Establish whether the user wants a full map, a focused update, or a preview. Do not broaden the requested scope based on repository opportunities.

### 2. Assess readiness and build the requirement inventory

- Record scope, constraints, decisions, exclusions, and all acceptance criteria or equivalent outcomes.
- Create a traceability inventory before deep repository exploration. Include each requirement ID, source location, concise outcome, and any verification condition stated by the specification.
- Surface ambiguities, conflicting requirements, and missing decisions. Continue with independent exploration where useful, but do not treat unresolved matters as decided.
- If the spec is not ready, follow the readiness rules above. A preview is permitted only when requested.

### 3. Map the repository

- Trace each in-scope requirement to the current code and supporting resources.
- Record evidence for present behavior and distinguish it from inference.
- Identify strengths in the specification (for example, clear outcomes, useful constraints, or explicit verification) and strengths in the repository (for example, reusable components, existing coverage, or compatible interfaces). Keep these as two separate views.
- Complete the present/required/resolution gap analysis. Record prerequisites and relevant cross-cutting risks.

### 4. Form packages, verifier briefs, and build order

- Create packages only for work needed to meet the specification or a clearly labeled prerequisite.
- Give each package an ID, title, outcome, requirements covered, dependencies, likely touchpoints, deliverables, exclusions, and verification hooks.
- Add implementer and verifier briefs for every package. Keep verification independent and evidence-based.
- Check the traceability inventory: every requirement must be present, partial, missing, or unknown; every item needing work must map to one or more packages; every package must map back to requirements or an explicitly labeled prerequisite.
- Set a suggested implementation sequence and explain important ordering constraints.

### 5. Present for review

Render the complete proposed map in chat or another reviewable form first. State the readiness status and intended output path. Ask for approval before publishing the map to the repository or another supplied destination, unless the user explicitly directed publication without a review step. A supplied `output_path` by itself identifies a destination; it does not waive this review gate.

### 6. Publish after authorization

- Write the approved map at the resolved destination. Recheck that the target has not become occupied by an unrelated artifact since review; if it has, use a safe numbered sibling and report the new path.
- Preserve the agreed scope and content. Do not update status files, indexes, specifications, or other companion artifacts unless the user asks.
- Report the path written, readiness status, major unresolved items, and what repository evidence was inspected. Do not claim tests were run unless they were.

## Output template

Adapt headings to the project and specification, but keep the core traceability, gaps, packages, verifier briefs, and implementation order in every full map.

```markdown
# <Specification or feature name> — implementation map

**Status:** Ready / Ready with explicit unknowns / Preview only
**Specification:** [<name>](<link or path>)
**Repository:** <root or unavailable>
**Map scope:** <full scope or requested focus>
**Evidence date:** <date inspected>

## Summary

<What the spec asks for, the main repository delta, and the most important dependencies or unknowns.>

## Specification alignment

### Strengths in the specification

- <Specific quality that supports implementation or verification.>

### Strengths in the repository

- <Existing behavior, structure, or tests that align with the specification, with path evidence.>

### Gaps and clarifications

| Requirement ID / topic | Present state | Required state | Suggested resolution / question |
|---|---|---|---|
| <ID> | <Evidence-based current behavior or unknown> | <Spec outcome> | <Proposal or precise question> |

## Requirement traceability

| Requirement / AC / decision | Source | Status | Present behavior and evidence | Delta / package |
|---|---|---|---|---|
| <explicit ID or derived map-only ID> | <section, page, or anchor> | <present / partial / missing / unknown> | <summary and path links> | <P1, P2, or no work needed> |

## Repository map

<Short description of the relevant architecture and how the mapped areas connect.>

| Path | Role and relevant behavior |
|---|---|
| [<path>](<relative link>) | <Why it matters; evidence found> |

## Workstreams and decisions

### <Decision, requirement group, or workstream>

- **Present:** <Verified behavior and evidence.>
- **Required:** <Traceable spec outcome.>
- **Unknowns / risks:** <Open matters, or none identified.>
- **Relevant resources:** <Paths, tests, interfaces, and docs.>

## Implementation packages

| ID | Outcome | Requirements | Depends on | Main touchpoints | Verification |
|---|---|---|---|---|---|
| P1 | <Outcome> | <IDs> | — | <Observed paths or areas> | <ACs, tests, or probes> |

### Implementer briefs

#### P1 — <title>

- **Goal:** <Outcome to implement.>
- **Deliverables:** <Concrete changes.>
- **Out of scope:** <Nearby work excluded.>
- **Touchpoints:** <Relevant paths or interfaces.>
- **Dependencies and assumptions:** <Explicit details.>

### Verifier briefs

#### P1 — verification

- **Must prove:** <Observable outcomes and requirement IDs.>
- **Tests / probes:** <Known commands, tests, or manual checks; label anything not yet confirmed.>
- **Regression watch:** <Relevant existing behavior and failure modes.>
- **Limits:** <Unavailable environment or evidence, if any.>

## Suggested implementation order

**Package sequence:** P1 → P2 → …

### Order rationale

<Explain dependency and sequencing choices, or state that no special ordering constraints were found.>

## Validation resources and limits

| Resource | Use or limitation |
|---|---|
| <test, fixture, environment, or service> | <What it can validate; access or coverage limits> |

## Cross-cutting risks

| Risk | Impact | Mitigation or decision needed |
|---|---|---|
| <risk> | <impact> | <mitigation, owner, or open question> |

## Related materials

- <Relevant specification and supporting references>
```

## Final quality check

Before presenting the map, confirm:

- The specification is the source of scope, and repository claims cite evidence.
- Explicit IDs are preserved; derived IDs are clear, map-only, and stable across this map’s revisions.
- Every in-scope requirement has a status and a traceable disposition.
- Every material gap has present, required, and resolution/question fields.
- Repository strengths are grounded in exploration and kept distinct from specification strengths.
- Every package is assignable, has dependencies and deliverables, and maps to requirements.
- Every package has a verifier brief with realistic evidence and stated limits.
- The proposed build order respects dependencies and spec constraints.
- Unknowns, assumptions, and proposals are labeled; no project-specific workflow or directory convention has been assumed.
- The output target is safe to use and no unrelated artifact will be overwritten.
- The review and approval step has been observed before publication.

## Anti-patterns

- Assuming a fixed directory layout, bundle structure, repository architecture, tool, workflow, command, or surface taxonomy.
- Requiring a retro, interview, scenario file, or other upstream artifact that the specification does not reference.
- Treating a prior map or a file’s existence as proof of current behavior.
- Listing paths without saying what they show about present behavior.
- Writing a gap as a vague issue without stating present state, required state, and resolution.
- Omitting requirements because the spec has no explicit AC or decision IDs.
- Creating derived IDs without a repeatable scheme or changing existing IDs unnecessarily.
- Listing workstreams without implementer-sized packages, requirement coverage, and verifier briefs.
- Claiming tests, commands, or validation were performed when they were not.
- Hiding assumptions, product decisions, or unresolved conflicts inside package descriptions.
- Duplicating the full specification or making unrequested edits to companion artifacts.
- Publishing the map before review when the user has not authorized skipping that step.
