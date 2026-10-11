---
name: author-communication-contract
description: Create or revise a Family Link client/server communication contract for heartbeat, readiness, roster, authentication, or other asynchronous exchanges; keep experience behavior and wire decisions traceable.
---

# Author a communication contract

Read [`docs/product/communication-contract-authoring.md`](../../../docs/product/communication-contract-authoring.md) for the canonical format and review questions. Read [`docs/AGENTS.md`](../../../docs/AGENTS.md), the applicable experience specification, and [client standards](../../../docs/standards/client-application-coding-standards.md) before drafting.

Preserve owner-decided behavior under its `DEC-` and `EXP-` IDs. Interview for unresolved behavioral meanings; propose engineering protocol choices without presenting them as owner-approved. Specify both endpoint and server state, message validation, timing, duplicate and late-result semantics, outage recovery, and checkable scenarios. Label unknown fields and outcomes `COM-OPEN-###` rather than inventing a wire format.

When v1 is relevant, capture its actual request/response shape and mark each part retain, adapt, or replace with evidence. Keep that historical shape separate from a proposed v2 schema.

Use `docs/product/communication-contracts/<scope>.md` unless the user names a path. Start with one coherent interoperability boundary; link related experience and UI artifacts instead of copying them. Do not implement client or server code as part of this authoring task.
