# Plan: Retry one message without duplicate delivery

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `draft` — not implemented |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | None — leave an evidence link when implemented |
| **Depends on** | 03 committed server archive; 01 frozen client identity |
| **Implementation readiness** | Ready after listed dependencies |

## At a glance

Give all selected recipients one committed logical message and a receipt that safely resolves a lost response.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Retry one message without duplicate delivery | todo |

This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Give all selected recipients one committed logical message and a receipt that safely resolves a lost response.

## Why

Without idempotence, Retry can send duplicates; sequential recipients can receive different outcomes that a single Sent label conceals.

## Current behavior

Product multipart POST `/v1/messages` has no client message ID/dedup/status lookup and accepts one recipient or broadcast. Firmware/twin send selected individuals as separate POSTs. The future family-message/1 JSON create shares this URI but is not implemented by the product server. H34 auth/PCM-only request shape must not be substituted.

## Target behavior

Keep multipart compatibility. New clients supply a frozen UUID `client_message_id` and exactly one of a single target, an explicit `to_user_ids` JSON array, or the existing broadcast flag. Reject duplicate/self/unknown IDs and conflicting modes before commit. Everyone is expanded from the confirmed recipient set, not re-expanded differently on retry. Define receipt `schema=family-send-receipt/1`, `client_message_id`, `message_id`, `state=complete`, `from_user_id`, exact sorted `recipients` each with user_id/seq, `audio` bytes/sha256/duration_ms, optional `sketch` bytes/sha256/format, and `server_commit=process_restart_verified`. New commit and identical replay return 200 to preserve existing clients; replay returns the exact original receipt, not another notification. Conflict for same sender+client ID with different immutable target/media metadata is 409. Reject oversize/invalid media before commit. Add authenticated sender-only `GET /v1/outgoing/{client_message_id}`: 200 exact committed receipt or 404 when no committed message is found. A 404 racing an in-flight request is not evidence of non-delivery; idempotent retry remains safe.

## Scope

Persist the dedup mapping as part of the same canonical commit, rebuild it after restart and serialize concurrent identical requests. Add one atomic recipient-set manifest commit, retain legacy `messages` response fields as an additive adapter. Decouple notification errors from committed acceptance. Bound and validate receipt JSON on firmware; compare actual local media sizes/hashes/identities. Old clients may omit ID but receive no dedup guarantee. Document multipart-vs-future-JSON dispatch explicitly; do not accidentally replace either contract.

## Non-goals

No unattended retry, new auth/session model, chunks/Opus, server full-store import, or server power-loss guarantee. No multipart endpoint body interpreted as future JSON create. No local paths/seq IDs as global identity.

## Relevant implementation surfaces

`v1_product/server.py`, new archive from 03, `user_mailbox.py` fanout; firmware delivery adapter in `v1_record.c`/01 controller; v1 client fixtures; `v1_product/ws.py`; future protocol docs as constraints, not an existing implementation.

## State and transitions

`not accepted → committing one target set → complete receipt`. Response lost → status lookup or same-ID replay → same complete receipt. Concurrent identical requests share one commit; conflicting request → conflict, never second delivery. Runtime attempt generation can change while message ID stays stable.

## Edge cases

Commit then disconnect; notification failure; server restart before replay; two concurrent retries; changed recipient roster; one invalid target; changed sketch/hash; 404 during an in-flight request; wrong user requesting another user's receipt.

## Acceptance criteria

- Same sender/client ID/content creates exactly one canonical message and one reference per target across retries/restart.
- All recipients become visible from one committed manifest; no partial fanout acknowledgement.
- Different bytes/targets under the same ID return 409.
- Receipt lookup is sender-scoped and response identity/media matches local descriptor.
- Notification failure does not turn a committed send into a new delivery.
- Existing single-recipient/broadcast clients retain their established response fields/status.

## Verification

Host: concurrency, response-drop-after-commit, process restart, receipt replay, invalid targets, unauthorized lookup, conflict and disk-floor cases with WAV/FLSK1 fixtures. Firmware: real bounded parser fixtures reject truncated/wrong ID/hash/target receipts; repeated user retry through the adapter uses the same ID. No card.

## Evidence to leave behind

`docs/evidence/product-no-storage/04-idempotent-receipts/`: exact request/receipt schema, route content-type matrix, duplicate/reference/notification counts, restart replay evidence and legacy compatibility checks. Never include real bearer tokens or media.

## Stop conditions

Stop if acceptance requires a server contract incompatible with existing clients, if a live migration is needed without 03 safeguards, or if multi-target publication is only a series of irreversible independent POSTs. Do not add Retry UI until exact replay/status guarantees pass.
