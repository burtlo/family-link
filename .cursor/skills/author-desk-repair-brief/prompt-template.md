# Repair agent prompt template

Copy the block below into a **new** chat (or continuation). Replace `{{…}}`.

```markdown
Mode: {{Review | Review, implement, and verify | Fix and verify named findings}}

Target: client/device/

Primary governing experience: docs/product/experiences/{{experience-slug}}.md ({{EXP-ID}} rev {{N}}).

Boundaries only:
- {{other experience 1}} — {{handoff IDs or §1 scope only}}
- {{other experience 2}} — …

Wave: {{N}} — {{campaign name}}

## Desk evidence (operator)

PASS:
- {{…}}

FAIL:
- {{…}}

Workarounds observed:
- {{…}}

Server telemetry (if any):
```json
{{paste lines}}
```

## Interpretation (orchestrator)

{{Short hypothesis per FAIL; map to COM-/EXP- rows}}

## Governing sources (read)

- docs/standards/client-code-review-agent.md
- docs/standards/client-application-coding-standards.md
- docs/standards/device-target-invariants.md
- docs/product/experiences/{{primary}}.md
- docs/product/communication-contracts/{{contract}}.md
- client/device/README.md

## Workflow rules

- Attach skill **client-code-review**; standards review subagent first, **no** expected-finding list.
- One confirmed defect at a time: fixer → fresh read-only verifier.
- No parallel implementers on client/device.
- Run `make test` after each fix; cite unrelated failures, do not “fix” them unless in scope.
- Max claim class and artifact path per finding (device-target-invariants claim table).

## Tasks (ordered)

1. {{First defect — reproduction, files, acceptance}}
2. {{…}}

## Hard outs

- Do not edit poc-v1/ or use x02 as policy.
- Do not scope {{Wave N+1}} ({{list}}).
- Do not claim behavior / L3 without desk serial or explicit waiver.

## Wave exit (L3)

- {{bullet}}
- {{bullet}}

Start with {{review only | implement highest-impact confirmed defect}}.
```
