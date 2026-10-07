# Plan: Own one volatile outgoing message

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `draft` — not implemented |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | None — leave an evidence link when implemented |
| **Depends on** | 00 baseline/PIN gate; no storage dependency |
| **Implementation readiness** | Ready after listed dependencies |

## At a glance

Give one outgoing audio/drawing message a stable owner, identity and lifecycle that can later use persistent storage.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Own one volatile outgoing message | todo |

This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Give one outgoing audio/drawing message a stable owner, identity and lifecycle that can later use persistent storage.

## Why

The child must not lose or misattribute a message because a screen, recipient array or signed-in user changed while a worker ran.

## Current behavior

`v1_record.c` owns global PCM/sketch arrays, mutable `s_session`, recipients and volatile flags; its worker calls `v1_state_apply`. The shell clears the shared session during privacy mute. `v1_state.c` has an eight-entry event queue whose send failures are ignored. There is no message/payload capability boundary.

## Target behavior

Introduce the smallest X02-local controller and RAM payload adapter, not a framework. A draft freezes a random UUID client ID, sender user ID, recipient set (Everyone expanded at confirmation), session generation, media formats/limits and stop reason. Separate message ID from network-attempt generation and per-recipient inbox seq. A closed payload exposes `read_at(track, offset, length)`/size/hash via a reader; UI never owns a filesystem or a required contiguous pointer. RAM capability says persistence=false and reboot recovery=false. One draft at most; a worker borrows media until its terminal cleanup. Only UI event consumption changes screens. Retain the closed draft until accepted or a declared discard/privacy policy releases it.

## Scope

Add a small module such as proposed `v1_message.[ch]` and `v1_payload_ram.[ch]`, or equivalent narrow boundaries. Adapt the record worker and existing state queue to frozen context and typed generation-bound capture/delivery results. Surface authoritative terminal result even if queue capacity is exhausted (e.g. one bounded completion slot reconciled by UI). Existing capture/transport remain adapters. Return typed errors on capacity/allocation; no durable claims.

## Non-goals

No retry UI, server API migration, entire-app reducer rewrite, auth redesign, SD/NVS audio, Opus or partition expansion. Do not expose store names or capabilities as child diagnostics.

## Relevant implementation surfaces

`firmware/v1/v1_record.[ch]`, `v1_state.[ch]`, `v1_types.h`, `x02_main.c`, `firmware/main/CMakeLists.txt`; proposed pure lifecycle/RAM modules and host fixtures. Keep existing auth generation machinery intact.

## State and transitions

`empty → capturing → closed → in_flight → accepted | rejected | uncertain → released`. Retry reuses the same message ID/bytes and starts a new attempt generation. A different draft gets a new UUID. Stale generations cannot navigate, mutate another sender, or release another payload. UI screen and message state are separate.

## Edge cases

Stop pressed before worker opens mic; repeated button wakes; queue full; cancellation while HTTP borrows RAM; mute/sign-out mid-send; late receipt for old operation; capture allocation failure; session re-login with same user but new generation.

## Acceptance criteria

- Sender/recipients never come from mutable globals after draft creation.
- Old events cannot change a new session/draft.
- No second draft or media reuse while an earlier borrow is live.
- No outgoing worker directly changes screen state or LVGL objects.
- RAM stage results never assert local durability; `recover()` is empty.
- A bounded reader can be replaced by a file-backed fake without changing UI/receipt logic.

## Verification

Automated: exercise reducer/owner model with fake clock, queue exhaustion, reordering, cancellation and a fake non-contiguous reader. Build X02 and run baseline login regression. Device: one short capture/send and rapid stop/cancel without task/watchdog failures. Test the production model, not a parallel test-only state machine.

## Evidence to leave behind

`docs/evidence/product-no-storage/01-lifecycle/`: event/state table, exact unit commands, stale-event/borrow-release assertions, source/build identity and explicit device-gate status. Include capability contract and proposed public interfaces in a small feature record only when shipped.

## Stop conditions

Stop if the approved session/PIN model changes, safe terminal events cannot be delivered, or the RAM budget cannot hold even the configured bounded draft. Do not erase flash or silently introduce an internal-flash outbox. Any broader auth failure follows the auth scope-freeze rule.
