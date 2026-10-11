# Client implementation review agent prompt

Use this prompt for one implementation under `client/labs/<lab>/` or
`client/device/`. Replace `<TARGET>` and, if relevant, `<BASE>` before giving
the **Review assignment** to an agent. Give a confirmed finding to a fix agent
using **Fix assignment**, then give the result to a separate agent using
**Verification assignment**. This is a reusable task prompt; it is not an
automatically loaded `AGENTS.md` file.

## Review assignment

Review `<TARGET>` for actionable defects and compliance with
`docs/standards/client-application-coding-standards.md`. Do not edit files.

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
6. Report findings first, ordered by severity. Each finding needs a stable ID,
   priority P0–P3, classification (**confirmed defect**, **plausible risk needing
   a test**, or **OPEN product choice**), absolute path and tight line range,
   trigger, failure effect, governing requirement, smallest useful fix, and a
   verification scenario. Avoid style-only findings and duplicates of one root
   cause. If there are no actionable findings, say so and list residual risks.
   Finish with the review boundary, commands and results, and the evidence
   level L0–L3 for each material claim. A build or host unit test cannot prove
   target timing, memory, input, or lifecycle behavior; label such claims
   unverified at L3 until representative target evidence exists.

## Fix assignment

Address finding `<ID>` for `<TARGET>`. Reproduce or trace its trigger first and
confirm it still exists in the current working tree. Read the cited standard
and applicable active specification. Make the smallest coherent fix for that
failure class, preserving unrelated staged, unstaged, and untracked work. Add
or update a focused test when it proves a failure or race boundary missing from
existing coverage. Do not silently settle an OPEN product decision. Report the
changed files, causal explanation, commands and results, and evidence level
actually reached. If the finding is invalid, give counterevidence with exact
paths and lines instead of changing code.

## Verification assignment

Independently verify finding `<ID>` for `<TARGET>` after the fix. Re-read the
original trigger, governing requirement, fix diff, and nearby callers. Attempt
the original failure and at least one relevant boundary case. Run focused
checks and inspect for new blocking paths, stale handles or results, duplicate
terminal events, lost user work, or unbounded resource use as applicable.
State **fixed**, **not fixed**, or **unverified**, with evidence and L0–L3 level.
Keep device-dependent claims open until target traces or observations exist.
Do not edit code in this verification pass.
