# Authoring endpoint communication contracts

Status: reusable authoring guide; it does not approve a protocol or authorize implementation.

## Purpose and boundary

Use a communication contract when a client and server must agree on messages, timing, identity, or recovery across an asynchronous operation. Experience specifications own user-visible behavior; device UI specifications own presentation; communication contracts define the interoperable exchange and the state each side must maintain. The [operational-contract concept guide](../v1-assessments/operational-contract-shape.md) remains useful for stage and failure questions, and [client coding standards §7](../standards/client-application-coding-standards.md) applies to user-visible waits.

Do not move an owner decision out of its experience specification. Link its `DEC-` and `EXP-` IDs. Do not turn a transport choice, historical demo, or example payload into approved product policy. Extract historical facts into active docs without linking to the v1 archive.

Place contracts in `docs/product/communication-contracts/<scope>.md`. Give each contract a stable `COM-<NAME>` ID, each operation a stable `OP-<NAME>` ID, each checkable rule a `COM-###` ID, each example a `COM-SCN-###` ID, and each unresolved issue a `COM-OPEN-###` ID. Retain IDs when editing; supersede rather than repurpose them.

## Required contract sections

1. **Identity and authority:** scope, participants, status, owner, revision, sources with their authority, approval state, and linked experience decisions and requirements. When migrating from v1, capture each as-built exchange beside an explicit **retain / adapt / replace** judgment and its reason.
2. **Vocabulary and state ownership:** distinguish device health, user presence, session, connectivity, readiness, and roster truth. Name which side observes, owns, and changes each state.
3. **Operation inventory:** for each operation, name initiator, trigger, request/response or event direction, preconditions, independent schedule, and relationship to active user work.
4. **Message contract:** transport and endpoint when decided; request/response fields, types, requiredness, units, ranges, privacy and authentication, versioning, and validation. Mark undecided fields `OPEN`; examples are illustrative until accepted.
5. **Timing and recovery:** clock owner, cadence, per-attempt timeout, retries and delays, total deadline, reconnect behavior, overlap/coalescing, and cancellation. Distinguish a failed delivery from an absent device.
6. **Result and side-effect semantics:** typed outcomes, idempotency scope and lifetime, concurrent duplicates, mismatched reuse, lost responses, late results, and state after expiration or restart.
7. **Acceptance matrix:** normal, malformed, unavailable, delayed, duplicated, reordered, restarted, and recovery cases. For each, state expected client state, server state, user-visible effect, and timing.
8. **Traceability and readiness:** source/decision → rule → scenario; blocking `OPEN` items and their decision owner; analysis, approval, implementation, and verification readiness separately.

For each operation, explicitly say whether it can run while muted or signed out, whether it can interrupt a PIN submission, and which result is allowed to change the visible stage. A background response must not become an implicit login or replace current user work.

## Review questions

- Can two implementations agree on the same request, result, and retry after a lost response?
- What exact observation makes a server mark a device stale, and what clears it?
- If a boot or connectivity event occurs near a scheduled beat, is another message sent or coalesced?
- If Wi-Fi is down for several intervals, are messages queued, skipped, or replayed?
- Can a malformed response ever be mistaken for readiness, wrong credentials, or success?
- Does every accepted operation have one terminal result even when the client has stopped waiting?
- Are product decisions, engineering proposals, and unapproved wire details visibly distinct?

Start from the smallest contract that resolves an actual interoperability boundary. Draft around `OPEN` items when the owner has not decided them; do not claim implementation readiness until blocking wire and behavior choices are settled.
