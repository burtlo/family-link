# Plan: Show a trustworthy send outcome

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `draft` — not implemented |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | None — leave an evidence link when implemented |
| **Depends on** | 01 operation ownership; 00 baseline |
| **Implementation readiness** | Ready after listed dependencies |

## At a glance

Keep Send responsive and show a clear, honest result before returning to the inbox.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Show a trustworthy send outcome | todo |

This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Keep Send responsive and show a clear, honest result before returning to the inbox.

## Why

A stopped recording is not a delivered message. A static or disappearing screen leaves the child unsure whether anything happened.

## Current behavior

Four send phases already exist in `v1_record.c`. `post_wav` returns any 2xx even when perform failed, ignores response content, and sequential recipient sends collapse into a final status. The 25s `v1_record_tick_send` escape changes screens without proving worker cancellation; a success waits for an inbox reload before painting Sent; receipts disappear after 2s.

## Target behavior

Reuse ST_SEND and existing visual language. Show Finishing while closing media, Sending with an animated activity indicator while awaiting acceptance, Sent to the frozen names on verified acceptance, and distinct known-failure/uncertain/partial copy when appropriate. Recommended success receipt waits for an explicit Done tap; no automatic two-second disappearance. Failure also requires an explicit action. In this interim legacy adapter, success requires transport success, complete bounded JSON response, HTTP 200 and a validated nonempty `messages` array covering each frozen target once with positive seq and correct sender/recipient. A header alone or send-progress 100% is never success. Generic 5xx/timeout after transmission is uncertain. Only a clear pre-commit rejection is a definite failure. New receipt schema from plan 04 replaces this legacy check later.

## Scope

Remove the independent 25s navigate-away behavior. Use operation-owned bounded timeout/cancellation: close transport in the owner, retain borrow until cleanup, then emit one result. During normal attempts keep animation and safe controls responsive; never allow a new capture while transport still owns media. Do not reload inbox on the critical receipt path; refresh afterward as a separate operation. Preserve interim per-recipient accepted/rejected/unknown facts, not a blanket all-failed label. Update the relevant BOX-UI send contract and timing source if auto-return is removed.

## Non-goals

No automatic retries, persistent queue, server durability claim, recall-after-send promise, new codec, or full UI restyling. Before plans 04/05, do not offer unsafe retry of ambiguous legacy POSTs.

## Relevant implementation surfaces

`firmware/v1/v1_record.c`: `post_wav`, `record_task_fn`, `v1_record_tick_send`, `paint_send`; `v1_state`/message controller from 01; `shared/v1/timing.yaml` and generated timing files; `docs/BOX-UI.md`. Browser parity is plan 16, not hardware evidence.

## State and transitions

`closed → finishing → sending → sent → Done → prior inbox`. Definite rejection → Couldn't send. Ambiguous acceptance → Couldn't confirm delivery. Partial legacy send names known accepted targets and uncertainty. Before 05, leaving a failed receipt explicitly discards the RAM draft and warns that it will not be kept.

## Edge cases

HTTP 200 headers then truncated body/error; wrong server returning a different JSON schema; stalled upload; repeated Done/stop; final UI paint delayed; server accepted but response lost; recipient 1 succeeds/recipient 2 fails; inbox refresh fails after acceptance.

## Acceptance criteria

- No Sent for transport error, malformed/truncated JSON, wrong identity or incomplete target coverage.
- A delayed 30s fixture never causes a silent 25s return or overlapping new capture.
- Activity continues and tested button feedback is within 200ms while sending; no network call runs in the paint callback.
- Verified success is painted before any inbox reload and remains until Done.
- An inbox reload failure cannot downgrade a sent receipt.
- Failure/partial/uncertain text never falsely asserts nobody received it.

## Verification

Host/fake transport: 200+error, early EOF, wrong schema, explicit reject, response loss, 30s delay, partial recipients and stale results. Verify actual response parser and controller, not string grep alone. Device/manual: film/render the full transition with a delayed fixture and Done, check animation/input responsiveness and preserved inbox focus. No card required.

## Evidence to leave behind

`docs/evidence/product-no-storage/02-receipt/`: outcome matrix, sanitized response examples, fake-clock timing and screen sequence. Label a build-only result as such. Keep raw server URLs/status details off the child screen.

## Stop conditions

Stop rather than enabling blind retry on an ambiguous legacy response. Stop if cleanup cannot end a transport borrow safely, if unknown delivery is collapsed into not-sent, or if the change requires concurrent HTTP-handle use without documented safety. No SD compatibility work.
