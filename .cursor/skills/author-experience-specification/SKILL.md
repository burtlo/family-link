---
name: author-experience-specification
description: Interviews the product owner and produces or revises an implementation-ready experience specification with intent ledger, traceability, and explicit OPEN items. Use when the user wants to author, draft, interview for, or refine a product experience spec, journey, or behavioral contract, or invokes /author-experience-specification.
disable-model-invocation: true
---

# Author experience specification

Produce an experience specification detailed enough for future agents to analyze,
design, implement, and verify behavior without inventing product policy. This
task interviews the product owner and writes documentation. It does not approve
product behavior or implement code.

## Canonical workflow

Read and follow
[`docs/product/experience-specification-authoring.md`](../../docs/product/experience-specification-authoring.md)
for the full method, question bank, required document structure, review
criteria, and analysis handoff. That file is the source of truth; this skill adds
execution order and stop conditions.

If the user supplies only a fragment (idea, scenario, sketch), treat it as
input to the workflow — not as an approved spec.

## Before you begin

1. Read [`docs/README.md`](../../docs/README.md) and
   [`docs/AGENTS.md`](../../docs/AGENTS.md) for documentation boundaries
   (product truth versus as-built code).
2. Scope one coherent experience or journey. If the input spans several,
   propose boundaries and confirm before deep interviewing.
3. Gather applicable sources per the authoring doc (vision, requirements,
   BOX-UI as examples only, v1 assessments, standards). Record conflicts. Do
   not adopt V1 or code behavior as owner intent without confirmation.
4. Decide the deliverable: new spec versus revision of an owner draft. Use a path
   the user names, or default to `docs/product/experiences/<slug>.md` when
   creating a new file.

## Execution order

### 1. Reconstruct and start the intent ledger

Summarize person, setting, goal, start, flow, and completion. Preserve original
owner text or a faithful summary at the top of the eventual spec.

List assumptions, gaps, source conflicts, and known decisions. Ask the owner to
correct this before treating reconstruction as settled (reconstruction
checkpoint).

Open the intent ledger (`DEC-xxx`) for every consequential decision. Use status
`PROPOSED` until the owner accepts.

### 2. Interview in small rounds

- Ordinary flow first, then interruptions, failures, limits, continuity.
- Two to four related questions per round; use concrete situations.
- Recommendations stay `PROPOSED` until accepted; silence is not approval.
- After each round, summarize decisions for correction.
- Run checkpoints explicitly: reconstruction → ordinary flow → exceptions →
  contract.

Do not dump the full question bank in one message.

### 3. Draft incrementally

While awaiting answers, draft independent sections. Do not invent behavior that
depends on unresolved owner decisions.

Add as they become available:

- Experience model (stages or screens, entry and exit, surviving operations)
- Ordinary journey
- Stage and operation contracts where relevant
- Requirements with stable IDs (`EXP-xxx`)
- Acceptance scenarios (`SCN-xxx`) linked to requirements
- Bidirectional traceability (source or decision → requirement → stage →
  scenario)

Apply experience classes from the authoring doc to keep detail proportionate.

### 4. Challenge pass

Walk ordinary use, interruption, failure, and a boundary case. Apply the review
criteria in the authoring doc; for every non-`CLEAR` rating, name a concrete
scenario and fix or mark `OPEN`.

Run a final owner walkthrough on decisions: locked behavior, bounded variation,
delegated choices, and blocking `OPEN` items. Update the intent ledger and
change record from corrections.

### 5. Deliver

Provide:

1. The specification document (or revision)
2. A short review summary (criteria ratings, blocking gaps)
3. Readiness: analysis-ready versus implementation-ready scope
4. Unresolved `OPEN` items with consequences

Do not call the interview complete until the authoring doc's completion gates are
met, or state which gate remains open and deliver a clearly labeled draft.

## Hard boundaries

- Do not implement firmware, server, or UI code as part of this skill.
- Do not refactor unrelated docs or retire code.
- Do not fill product gaps silently; mark `OPEN` or `PROPOSED`.
- Do not claim owner approval without recorded checkpoint review and status.
- Code and as-built features are evidence, not automatic requirements.

## Suggested invocation

```text
/author-experience-specification

[Experience description, link to draft, or "revise docs/product/experiences/foo.md"]
```

## Follow-on (separate task)

Solution analysis against code and standards uses the handoff prompt in the
authoring doc. Run that only when the user explicitly requests it.
