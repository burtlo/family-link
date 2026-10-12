# Client implementation review agent prompt

Use this prompt for one implementation under `client/labs/<lab>/` or
`client/device/`. Replace `<TARGET>`, `<SCOPE>`, and (if relevant) `<BASE>`
before giving an assignment to an agent.

| Role | Edits code? | Typical chat |
|------|-------------|--------------|
| **Review assignment** | No | Dedicated review pass |
| **Fix assignment** | Yes | **New** Agent chat (or after you paste only the Fix block) |
| **Verification assignment** | No | **New** chat after the fix lands |

Do not run Fix or Verification in the same turn as Review unless you
explicitly abandon the review-only boundary and paste a **Fix assignment**
block. The reviewer must not implement, batch-fix every finding, or spawn fix
subagents.

This is a reusable task prompt; it is not an automatically loaded `AGENTS.md`
file. Invoke it from Cursor via the **client-code-review** skill
([`.cursor/skills/client-code-review/SKILL.md`](../../.cursor/skills/client-code-review/SKILL.md)).

## Finding IDs (`<SCOPE>-F###`)

Every finding in a review report uses one ID for the life of that report.

| Part | Rule | Example |
|------|------|---------|
| `<SCOPE>` | Short uppercase slug for the target (lab or product slice) | `LAB02` for lab-02-bounded-inbox |
| `F###` | Three-digit sequence **in this report only**, assigned once, severity order (P0 first) | `LAB02-F001`, `LAB02-F002` |

- Use **`LAB02-F001`**, not `LAB02-001` or `F001` alone.
- One root cause → one ID. Do not split duplicates; do not merge unrelated bugs.
- **Confirmed defect** IDs are the default handoff to Fix.
- **Plausible risk** and **OPEN product choice** IDs are listed for traceability;
  they are **not** fixable until the user opts in (see Fix assignment).
- A later re-review at a new SHA may publish a **new** report with new IDs; refer
  to the prior report path or commit when continuing work on an older ID.

**IDs are not in the codebase.** `LAB02-F001` is a label inside one review
report only. A new Agent chat that receives bare `Fix and verify LAB02-F001`
has **no** finding text. Agents must **not** search the repo, git history, or
transcripts for `F001` strings. The human must attach context (see **Fix and
verify packet** below).

## Fix and verify packet (required context for Fix / Verify)

Every fix or verify pass needs a **packet**: the finding fields for one ID,
plus target and review provenance. The review **Handoff** must emit a filled
packet for the recommended ID. The human copies **the whole packet** into the
fix chat (and adds `@client-code-review`), or saves the full review under
`docs/labs/evidence/<lab-slug>/reviews/` and `@`-mentions that file.

```text
Fix and verify packet — <SCOPE>-F00N

Target: <TARGET>
Scope: <SCOPE>
Review provenance: <review chat URL or path to saved review.md> @ <HEAD sha>
Classification: <confirmed defect | plausible risk | OPEN product>
Priority: Pn

Location: <absolute path>:<line range>
Trigger: <reachable steps>
Effect: <user-visible or invariant failure>
Requirement: <standard § or spec>
Smallest fix: <one coherent change class>
Verify scenario: <how to prove fixed; L1 command or L3 note>

Command (optional): Fix and verify <SCOPE>-F00N
```

Fix/verify agents: if the message has only an ID or one line (`Fix and verify
LAB02-F001`) with **no** packet fields and **no** `@` review file or review
chat, **stop immediately**. Ask the user to paste the **Fix and verify packet**
from Handoff or `@` the saved review. Do not implement from an ID lookup.

## User commands (what to type next)

After a review, prefer **one confirmed defect** per fix cycle, then verify.
**Always bring the packet** (paste or `@` file); the ID alone is not enough.

| You bring | Agent uses |
|-----------|------------|
| **Fix and verify packet** (from Handoff) + `@client-code-review` | **Fix and verify assignment** |
| Saved review `.md` + `Fix and verify <SCOPE>-F00N` + skill | Read that file’s finding subsection for `<ID>` |
| `@` link to review Agent chat + `Fix and verify <SCOPE>-F00N` | Read that chat’s finding for `<ID>` (if accessible) |
| Packet + `Fix <SCOPE>-F00N` only | **Fix assignment** |
| Packet + `Verify <SCOPE>-F00N` only | **Verification assignment** |
| Named IDs + packets for each | **Fix batch** / **Fix and verify batch** |

Avoid **`fix all`**, **`Fix assignment all`**, or fixing every ID in the report
in one pass. That bypasses smallest-fix discipline, mixes defects with risks
and OPEN product items, and skips independent verification.

If you truly need multiple fixes, name IDs explicitly (`Fix batch LAB02-F001,LAB02-F002`)
and expect the agent to confirm scope and order before editing.

## Review assignment

Review `<TARGET>` (`<SCOPE>`) for actionable defects and compliance with
`docs/standards/client-application-coding-standards.md`. **Do not edit files.**

### Reviewer must not

- Implement fixes, write tests for implementation, or refactor production code.
- Offer to “turn findings into fixes” without emitting the **Handoff** section below.
- Start Fix or Verification work in this chat when the user says “fix all” or
  similar; reply with the **Handoff** recommendation and tell them to open a new
  Agent chat with one Fix block (or a named `Fix batch`).
- Spawn subagents to fix or verify unless the user explicitly requests
  orchestration outside this review prompt.

### Review steps

1. Establish the review boundary. Report the branch and HEAD, the requested
   base (`<BASE>` if supplied), and whether the review includes committed,
   staged, unstaged, and untracked files. If no base is supplied, review the
   current implementation and all related working-tree changes. Follow code
   paths into shared components, host scripts, fixtures, servers, and tests when
   they affect the target. Preserve all work in progress.
2. Read `docs/AGENTS.md` and the full client coding standard. Locate the
   target's README, manifest, design or architecture notes, applicable lab
   definition or product experience, device UI specification, communication
   contract, and evidence report. State which sources are applicable and any
   conflict or OPEN decision. Product behavior comes from approved active
   specifications; standards govern implementation; plans and assessments
   supply scope and evidence. Check each document's claims against current
   code and recorded results.
3. Use `docs/v1-assessments/v2-implementation-practices.md` to identify useful
   artifacts and verification gates. Read focused concept guides only when the
   reviewed path needs them: async operation envelope, UI update taxonomy,
   session stage versus background poller, operational contract, and resource
   budget envelope. Treat v1 feature assessments as historical examples of
   failure modes, never as required architecture or measured results for the
   current implementation.
4. Build a short ownership and wait map for the affected paths: input or timer
   callback → presentation → session → I/O or worker → completion → model → UI.
   Trace real calls and data ownership. Check bounded presentation work,
   single-writer session transitions, immutable operation input, exactly one
   terminal outcome, stale-result rejection, cancellation, timeout, queue-full
   behavior, UI handle lifetime, update level, preserved identity, resource
   bounds, and failure UX where applicable. Inspect persistence and remote
   side effects when the target has them. Do not assume a rule is satisfied
   merely because a component has the expected name.
5. Derive scenarios from the target's active plan and code. Exercise nominal,
   interrupted, failure, race, and boundary cases relevant to its actual
   features. For each suspected defect, establish a reachable trigger and the
   violated invariant. Run the smallest relevant existing test or static check
   when feasible. Do not write tests or change implementation during review.
   Distinguish a code defect from missing evidence or an OPEN product choice.
6. Report using the **Review report format** below. Assign `<SCOPE>-F###` IDs.
   Each finding needs priority P0–P3, classification (**confirmed defect**,
   **plausible risk needing a test**, or **OPEN product choice**), absolute path
   and tight line range, trigger, failure effect, governing requirement,
   smallest useful fix, and a verification scenario. Avoid style-only findings.
   If there are no actionable findings, say so under Findings.

### Review report format (required section order)

Use these headings in order. Do not replace them with a single “bottom line”
paragraph.

1. **Review boundary** — table: target, scope, branch/HEAD, base, WIP scope, edit policy.
2. **Applicable sources** — what governed this review; OPEN product called out.
3. **Ownership / wait map** — short diagram or bullet chain for the reviewed path.
4. **Findings** — by severity; subsections `### <SCOPE>-F### — Pn — classification`.
   List **confirmed defects** before **plausible risks**. Do not file evidence
   gaps or git hygiene as F-IDs.
5. **Residual risks** — OPEN product, design tradeoffs, and plausible risks you
   did not recommend for fix this cycle (reference F-IDs).
6. **Evidence gaps** — L3 / checklist items; what artifact would close each.
7. **Repository notes** (optional) — uncommitted WIP, “pin SHA before device run”;
   not defects.
8. **Commands and results** — what you ran and pass/fail.
9. **Evidence levels** — table: claim → L0–L3.
10. **Handoff** — required; see below.

### Handoff (required; end every review with this)

**Recommended next:** exactly one `<SCOPE>-F###` to fix first (prefer the
highest-severity **confirmed defect**), one sentence why, and what you are
**not** doing this cycle (e.g. plausible risks, L3 evidence, OPEN product).

Emit a complete **Fix and verify packet** for that ID (template above) with
every field filled from the finding subsection. Include **Review provenance:**
this chat and `HEAD` sha from the review boundary.

**Next step for the human:** open a **new** Agent chat, `@client-code-review`,
**paste the entire Fix and verify packet** (do not paste only the one-line
command). Optional: save the full review to
`docs/labs/evidence/<lab-slug>/reviews/` and `@` that file instead of pasting.

Do not tell the user to type `Fix and verify LAB02-F001` without the packet.
Do not end with “I can fix … if you want” or “fix all” without the warnings in
**User commands**.

## Fix assignment

**Prerequisite:** a **Fix and verify packet** for `<ID>`, or a saved review /
review chat the agent can read. If missing, stop and ask — do not search the
repo for `<ID>`.

Address finding `<ID>` for `<TARGET>`. `<ID>` must be a single `<SCOPE>-F###`
from a review report unless **Fix batch** rules apply.

Reproduce or trace its trigger first and confirm it still exists in the current
working tree. Read the cited standard and applicable active specification.

| Classification in report | Fix without extra user OK? |
|--------------------------|----------------------------|
| **Confirmed defect** | Yes |
| **Plausible risk needing a test** | No — stop and ask, or add test-first proof |
| **OPEN product choice** | No — stop; needs owner/spec decision |

Make the smallest coherent fix for **that** failure class, preserving unrelated
staged, unstaged, and untracked work. Add or update a focused test when it
proves a failure or race boundary missing from existing coverage. Do not
silently settle an OPEN product decision.

If the user requests **fix all** or an unnamed set of IDs: refuse the blanket
request; list **confirmed defect** IDs in priority order; recommend fixing **one**
ID, then Verification, then the next.

### Fix batch (optional)

Only when the user names IDs explicitly, e.g. `Fix batch LAB02-F001,LAB02-F002`:

- Confirm each ID’s classification and that fixes are the same failure class
  or tightly coupled (e.g. two bootstrap paths to the same terminal stage).
- Fix in priority order; one coherent commit worth of change if possible.
- Report per-ID what changed and what was deferred.

Report changed files, causal explanation, commands and results, and evidence
level actually reached. If the finding is invalid, give counterevidence with
exact paths and lines instead of changing code.

Remind the user to run **Verification assignment** for each fixed ID, or use
**Fix and verify** for the same ID in one chat.

## Fix and verify assignment (convenience, human still in the loop)

Use when the user supplies a **Fix and verify packet** (or `@` review artifact)
for exactly one `<ID>`, optionally with the line `Fix and verify <SCOPE>-F00N`.
The packet is the source of truth; the ID line is only a label.

**Do not** combine with **Review assignment** in the same task. **Do not** treat
as permission to fix every ID in a report. **Do not** grep the codebase for
`<SCOPE>-F###` when the packet is missing.

### Phase 1 — Fix

Run **Fix assignment** for `<ID>`. Complete edits and tests before Phase 2.

### Phase 2 — Verify (same chat, no further edits)

After Phase 1, stop editing code. Run **Verification assignment** for the same
`<ID>`. Re-state the original trigger and requirement from the user’s pasted
finding or an on-disk review note before judging the diff.

Output must use these headings in order:

1. `## Fix — <ID>` — files changed, causal explanation, commands and results.
2. `## Verify — <ID>` — **fixed** | **not fixed** | **unverified**, evidence,
   L0–L3.

If **not fixed**, do not start another fix loop in the same turn; tell the user
what failed and ask whether to retry **Fix** or **Fix and verify** for that ID.

### Fix and verify batch

For `Fix and verify batch LAB02-F001,LAB02-F002`: run Phase 1+2 for F001, then
Phase 1+2 for F002. Do not skip verify for an ID you fixed. Same classification
and coupling rules as **Fix batch**.

### When a separate verify chat is still better

Use a **new** chat for **Verify** only when the fix was large, touched session
architecture, or you want a cold read without the fixer’s thread context. The
combined mode is the default convenience path for a single **confirmed defect**
with a clear host/L1 check.

### Persist the review (recommended for cross-chat handoff)

Offer to save the full review under
`docs/labs/evidence/<lab-slug>/reviews/` (e.g. `YYYY-MM-DD-<scope>-review.md`).
Then the fix chat can be: `@client-code-review` `@that-file.md` `Fix and verify
LAB02-F001`. The agent reads the `### LAB02-F001` subsection from the file —
still not from a repo-wide ID search.

## Verification assignment

Independently verify finding `<ID>` (`<SCOPE>-F###`) for `<TARGET>` after the
fix. Re-read the original review text for that ID, trigger, governing
requirement, fix diff, and nearby callers. Attempt the original failure and at
least one relevant boundary case. Run focused checks and inspect for new blocking
paths, stale handles or results, duplicate terminal events, lost user work, or
unbounded resource use as applicable.

State **fixed**, **not fixed**, or **unverified**, with evidence and L0–L3 level.
Keep device-dependent claims open until target traces or observations exist.
Do not edit code in this verification pass.

If **not fixed** or regression found, say which ID failed and recommend a new
Fix assignment for that ID only — not a broad re-fix of the whole report.
