---
name: client-code-review
description: >-
  Orchestrates Family Link client implementation review against coding standards
  and applicable specifications, dependency planning, sequential fixes, and
  independent verification. Use for client review, Fix, Verify, Fix and verify,
  or client-code-review-agent requests for client/labs/ or client/device/.
disable-model-invocation: true
---

# Client code review

Read the canonical workflow in full:
[`docs/standards/client-code-review-agent.md`](../../../docs/standards/client-code-review-agent.md).

Act as one orchestrator. Delegate the standards review to a read-only subagent,
implementation to a fixing subagent, and verification to a separate fresh
read-only subagent. Execute assignments sequentially; never run concurrent edits
or verify source while it is changing. Specialist review profiles are deferred.

## Choose mode from user intent

- **Review** (default): review and dependency plan; no implementation edits.
- **Review, implement, and verify**: resolve confirmed defects within the
  authorized target, using the plan and independent verification.
- **Fix / Fix and verify** named findings, or **fix all confirmed findings**:
  use the accessible review context and resolve the selected confirmed defects
  sequentially, with independent verification of every implementation fix.
- **Verify**: independent read-only verification of the selected findings.

Do not ask again for routine assignments already authorized by remediation
intent. OPEN product decisions and unproven risks are not automatic fix scope.
Every plan states prerequisites and sequential order, including an explicit
**No dependency ordering required** when applicable.

## Keep context within the workflow

The orchestrator transfers complete finding context and source provenance to
subagents. Use findings already available in this task or an attached/saved
review; the user does not need a new chat or a pasted packet. Existing packets
remain valid input.

`<SCOPE>-F###` identifies a finding only within its report. If a bare ID has no
accessible report or finding text, ask for that context; do not search source or
git history for the ID. Keep final IDs stable and record per-ID outcomes.

Identify applicable experience, device UI, communication, and engineering
requirements, including approval status and OPEN decisions. Preserve WIP and
record the relevant uncommitted source state, not just HEAD.

Fixer self-checks do not replace independent verification. Give the verifier
fresh context and raw artifacts, without inheriting the fixer's conversation.
If independent subagents are unavailable, report that limitation and follow the
canonical fallback; do not silently claim self-verification as independent.

## Related sources

- [`docs/AGENTS.md`](../../../docs/AGENTS.md)
- [`Client coding standard`](../../../docs/standards/client-application-coding-standards.md)

Keep this entry point synchronized with `.cursor/skills/client-code-review/SKILL.md`.
The v1 POC is read-only per
[`.cursor/rules/poc-v1-reference-archive.mdc`](../../../.cursor/rules/poc-v1-reference-archive.mdc).
