---
name: client-code-review
description: >-
  Reviews Family Link client implementations (labs under client/labs/, product
  under client/device/) against client-application-coding-standards using
  scoped finding IDs (e.g. LAB02-F001) with Fix and verify packets for
  cross-chat handoff. Use for client code review, Fix, Verify, Fix and verify,
  or client-code-review-agent. IDs are not stored in the repo; never search the
  codebase for F-IDs without a packet or @ review file.
disable-model-invocation: true
---

# Client code review

Canonical workflow:
[`docs/standards/client-code-review-agent.md`](../../../docs/standards/client-code-review-agent.md).

Read that document **in full** for the active mode.

## Critical: IDs are not lookup keys

`<SCOPE>-F###` exists only inside **one review report**. It is not in source
code, issues, or git.

If the user sends only `Fix and verify LAB02-F001` (or similar) with **no**
**Fix and verify packet**, **no** `@` saved review under
`docs/labs/evidence/.../reviews/`, and **no** accessible review chat context:

1. **Stop.** Do not grep, search, or explore the repo for `F001` / `LAB02-F001`.
2. Ask them to paste the **Fix and verify packet** from the review **Handoff**
   (whole block), or `@` the saved review file, or `@` the review Agent chat.

Then proceed in **Fix and verify** (or Fix / Verify) mode using the packet fields.

## Choose mode

| User brings | Mode | Edit code? |
|-------------|------|------------|
| Review request (default) | **Review assignment** | No |
| **Fix and verify packet** (+ optional ID line) | **Fix and verify assignment** | Phase 1 only |
| `@` review `.md` + `Fix and verify <ID>` | **Fix and verify** (read finding from file) | Phase 1 only |
| Packet + `Fix <ID>` | **Fix assignment** | Yes |
| Packet + `Verify <ID>` | **Verification assignment** | No |
| Packets + explicit batch IDs | **Fix** / **Fix and verify batch** | Per rules |

**fix all** / bare ID without packet → refuse; point at Handoff packet.

## Review mode

1. Run **Review assignment**; assign `<SCOPE>-F001`, …
2. **Handoff** must include a complete **Fix and verify packet** for the
   recommended ID (all fields + review provenance @ HEAD).
3. Tell the human: new chat, `@client-code-review`, **paste entire packet** —
   not the one-line command alone.

## Fix and verify mode

1. Confirm packet (or review file subsection for `<ID>`) is present.
2. Phase 1: **Fix assignment** — edits and tests.
3. Phase 2: **Verification assignment** — no edits; `## Fix — <ID>` then
   `## Verify — <ID>`.

## Related standards

- [`docs/AGENTS.md`](../../../docs/AGENTS.md)
- [`docs/standards/client-application-coding-standards.md`](../../../docs/standards/client-application-coding-standards.md)

v1 POC: read-only per
[`.cursor/rules/poc-v1-reference-archive.mdc`](../../../.cursor/rules/poc-v1-reference-archive.mdc).
