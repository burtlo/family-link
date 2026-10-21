
---
name: author-experience-specification
description: >-
  Interviews the product owner using recommendation-first questions and
  concrete scenarios, then produces or revises an implementation-ready
  experience specification with intent ledger, traceability, and explicit
  OPEN items. Use when the user wants to author, draft, interview for,
  or refine a product experience spec, journey, or behavioral contract,
  or invokes /author-experience-specification.
disable-model-invocation: true
---

# Author experience specification

Produce an experience specification detailed enough for future agents to
analyze, design, implement, and verify behavior without inventing product
policy.

This task interviews the product owner and writes documentation. It does
not approve product behavior or implement code.

The interview must help the owner make decisions efficiently, not require
them to design every detail from scratch.

## Canonical workflow

Read and follow
[`docs/product/experience-specification-authoring.md`](../../docs/product/experience-specification-authoring.md)
for the full method, question bank, required document structure, review
criteria, and analysis handoff.

That file is the source of truth; this skill adds execution order,
interview behavior, decision triage, and stop conditions.

If the user supplies only a fragment (idea, scenario, sketch), treat it
as input to the workflow — not as an approved spec.

## Before you begin

1. Read [`docs/README.md`](../../docs/README.md) and
   [`docs/AGENTS.md`](../../docs/AGENTS.md) for documentation boundaries
   (product truth versus as-built code).
2. Obey [`.cursor/rules/poc-v1-reference-archive.mdc`](../../.cursor/rules/poc-v1-reference-archive.mdc):
   the `poc-v1/` tree is read-only reference — extract facts into active
   `docs/` without linking archive paths.
3. Scope one coherent experience or journey. If the input spans several,
   propose boundaries and confirm before deep interviewing.
4. Gather applicable sources per the authoring doc (owner input, active
   `docs/`, v1 assessments under `docs/v1-assessments/`, standards;
   vision/requirements/BOX-UI from the v1 archive only as historical
   examples). Record conflicts. Do not adopt V1 or code behavior as
   owner intent without confirmation.
5. Decide the deliverable: new spec versus revision of an owner draft.
   Use a path the user names, or default to
   `docs/product/experiences/<slug>.md` when creating a new file.

## Interview principles

### 1. Recommend before asking

For each meaningful unresolved decision:

- Analyze the existing requirements, constraints, and experience.
- Identify a sensible recommended default.
- Explain briefly why the default is appropriate.
- Present alternatives only when they have meaningful consequences.
- Ask the owner to accept, reject, or modify the recommendation.

Do not make the owner discover obvious defaults through open-ended
questions.

Prefer:

**Question:** What happens when connectivity drops during playback?

**Recommended:** Pause playback, preserve progress, and restore the
selected message when connectivity returns. Do not automatically resume.

**Why:** Avoids losing progress or unexpectedly playing audio.

**Alternatives:** Restart playback or automatically resume.

Over:

"What should happen when connectivity drops?"

Recommendations remain `PROPOSED` until accepted. Silence is not approval.

### 2. Ask three focused questions per round

Default to exactly three related questions per interview round.

Ask fewer when fewer meaningful decisions remain. Never invent filler
questions to reach three.

Each question should:

- Describe a concrete situation.
- Identify the actual decision.
- Include a recommended default.
- Give a short rationale or meaningful tradeoff.
- Be answerable with a brief response.

Keep the questions concise. Avoid long explanations of straightforward
decisions.

Make it easy for the owner to say:

- "Accept all three."
- "Accept 1 and 2, change 3."
- "Use the recommendation, except..."

Record each accepted decision separately in the intent ledger.

### 3. Use concrete people and examples

Describe behavior through realistic situations involving people,
devices, screens, and actions.

Prefer:

"Maya is listening to a message from Jordan. She selects another
message before playback finishes. What happens to the first message?"

Over:

"What is the interaction contract for selection changes during
an active operation?"

Use actual names, labels, and terminology supplied by the owner when
available. Otherwise, use natural illustrative names and explicitly
treat them as examples, not product data or approved identities.

Concrete examples should make the experience easier to visualize
without introducing new requirements.

### 4. Distinguish product decisions from engineering decisions

Before asking a question, classify it:

**Product decision**
Changes user-visible behavior, meaning, expectations, permissions,
continuity, or recovery.

Ask the owner, with a recommendation.

**Engineering decision**
Concerns implementation technique, internal representation, protocol
details, performance tuning, or styling precision that does not change
the agreed experience.

Delegate to engineering within documented constraints.

**Contract dependency**
A technical choice that may affect an agreed product guarantee,
cross-component responsibility, compatibility, or testability.

Document the dependency. Ask the owner only if it introduces a genuine
product-policy choice.

Do not ask the product owner to choose:

- Data type sizes or internal representations.
- Exact animation timings without a meaningful UX tradeoff.
- Internal retry implementation when behavior is already defined.
- Wire schemas or payload structures.
- Precise styling parameters that engineering can safely determine.

Instead, document the required behavior and delegate implementation
details to engineering.

Do not silently delegate genuinely consequential product decisions.

### 5. Respect previous decisions

Treat accepted decisions as locked requirements.

Do not repeatedly ask the owner to confirm behavior already approved.

Reopen a decision only when:

- New information reveals a contradiction.
- A new scenario exposes a missing requirement.
- Implementation constraints make the approved behavior infeasible.
- The owner explicitly requests reconsideration.

When reopening, identify the conflict and recommend the smallest
reasonable correction.

Do not expand the interview into unrelated architecture or redesign
questions.

### 6. Prioritize consequential uncertainty

Spend interview time where the answer meaningfully changes the
experience.

Priority order:

1. Ordinary user journey and expected outcomes.
2. State changes, continuity, and interruption behavior.
3. Failure, recovery, and conflicting operations.
4. Boundary conditions and important exceptions.
5. Presentation details with meaningful UX consequences.

Avoid speculative edge cases whose outcomes are already determined
by established rules.

Before asking, consider whether the answer can be derived from a
locked requirement or delegated safely to engineering.

If so, do not ask another question.

## Execution order

### 1. Reconstruct and start the intent ledger

Summarize person, setting, goal, start, flow, and completion.

Preserve original owner text or a faithful summary at the top of
the eventual spec.

List assumptions, gaps, source conflicts, and known decisions.

Ask the owner to correct the reconstruction before treating it as
settled (reconstruction checkpoint).

Open the intent ledger (`DEC-xxx`) for every consequential decision.
Use status `PROPOSED` until the owner accepts.

Distinguish existing approved behavior from newly recommended behavior.

### 2. Interview in small rounds

Interview in this order:

1. Ordinary flow.
2. Interactions and state transitions.
3. Interruptions and continuity.
4. Failures and recovery.
5. Limits and boundary conditions.
6. Remaining product contracts.

Use the interview principles above:

- Three focused questions per round.
- Recommended defaults.
- Brief rationale or tradeoffs.
- Concrete scenarios with people and actions.
- No unnecessary engineering questions.
- No repeated decisions.

Do not dump the full question bank in one message.

After each round:

1. Record accepted decisions in the intent ledger.
2. Record modifications and rejected recommendations.
3. Identify any contradictions with earlier decisions.
4. Determine which uncertainties are now resolved.
5. Ask only the next consequential questions.

Do not repeatedly summarize the entire specification between rounds.
Provide a short decision summary when useful for correction.

Run checkpoints explicitly:

reconstruction → ordinary flow → exceptions → contract.

### 3. Draft incrementally

While awaiting answers, draft independent sections.

Do not invent behavior that depends on unresolved owner decisions.

Add as they become available:

- Experience model (stages or screens, entry and exit, surviving operations).
- Ordinary journey.
- Stage and operation contracts where relevant.
- Requirements with stable IDs (`EXP-xxx`).
- Acceptance scenarios (`SCN-xxx`) linked to requirements.
- Bidirectional traceability (source or decision → requirement → stage →
  scenario).

Apply experience classes from the authoring doc to keep detail
proportionate.

Use the concrete interview scenarios as inputs to acceptance scenarios,
but rewrite them as precise, testable requirements.

Separate:

- Locked product behavior.
- Bounded implementation variation.
- Delegated engineering choices.
- Blocking `OPEN` items.

### 4. Challenge pass

Walk through:

- Ordinary successful use.
- An interrupted operation.
- A failure and recovery.
- A meaningful boundary case.

Apply the review criteria in the authoring doc.

For every non-`CLEAR` rating, name a concrete scenario and either
resolve the issue or mark it `OPEN`.

Prefer proposing a resolution rather than generating another broad
question.

Do not manufacture new edge cases indefinitely.

Run a final owner walkthrough covering:

- Locked behavior.
- Bounded variation.
- Delegated engineering choices.
- Blocking `OPEN` items.

Update the intent ledger and change record from corrections.

### 5. Stop when the product frontier is resolved

Do not continue interviewing merely because implementation details
remain undecided.

The product interview is complete when:

1. The ordinary journey is defined.
2. Consequential interruptions and failures have defined outcomes.
3. Accepted decisions are recorded.
4. Remaining choices can be safely delegated to engineering.
5. Any unresolved product decisions are explicitly marked `OPEN`.
6. The owner has completed the required review checkpoints.

Do not invent requirements to create more questions.

Do not mistake implementation uncertainty for unresolved product intent.

If a blocking product decision remains, identify its consequence
and request the smallest decision necessary.

### 6. Deliver

Provide:

1. The specification document (or revision).
2. A short review summary (criteria ratings, blocking gaps).
3. Readiness: analysis-ready versus implementation-ready scope.
4. Unresolved `OPEN` items with consequences.
5. Delegated engineering decisions and their behavioral constraints.

Do not call the interview complete until the authoring doc's completion
gates are met, or state which gate remains open and deliver a clearly
labeled draft.

## Hard boundaries

- Do not implement firmware, server, or UI code as part of this skill.
- Do not refactor unrelated docs or retire code.
- Do not fill product gaps silently; mark `OPEN` or `PROPOSED`.
- Do not claim owner approval without recorded checkpoint review and status.
- Code and as-built features are evidence, not automatic requirements.
- Do not ask technical questions merely because implementation details
  are unknown.
- Do not reopen locked requirements without a demonstrated conflict.
- Do not treat recommended defaults as accepted decisions.
- Do not expand the scope of the experience without owner approval.

## Suggested invocation

```text
/author-experience-specification

[Experience description, link to draft, or "revise docs/product/experiences/foo.md"]
```

## Follow-on (separate task)

Solution analysis against code and standards uses the handoff prompt in the
authoring doc. Run that only when the user explicitly requests it.
