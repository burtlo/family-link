# Plan: Keep a failed recording for one safe retry

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `draft` — not implemented |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | None — leave an evidence link when implemented |
| **Depends on** | 01 lifetime owner; 02 truthful receipt; 04 same-ID server replay |
| **Implementation readiness** | Ready after listed dependencies |

## At a glance

Let the child retry the same failed recording while the box remains on, or knowingly discard it.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Keep a failed recording for one safe retry | todo |

This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Let the child retry the same failed recording while the box remains on, or knowingly discard it.

## Why

Temporary lack of persistence need not mean silently throwing away every usable recording on a transient network error.

## Current behavior

Current record_task_fn returns to carousel after a failed receipt, resets capture flags, and later reuses global buffers. There is no visible Retry/Discard, durable queue, or safe ambiguous-response retry. Do not equate allocated memory with an owned retained draft.

## Target behavior

Retain exactly one closed audio+optional drawing payload and frozen descriptor in the volatile adapter. Failure card: Couldn't send (known rejection) or Couldn't confirm delivery (ambiguous), recipients, brief “Keep this box on to try again,” Retry and Discard. Retry first checks a complete receipt when useful, then uses the same client ID, targets and media; never rerecord or resend only audio silently. Known-offline Retry is disabled with “Waiting for a connection,” but no background delivery is promised. Reconnect enables Retry without auto-send. Successful retry → Sent → Done. Discard requests cleanup, waits for worker borrow release and warns that accepted messages cannot be recalled. New capture is blocked while this draft is unresolved.

## Scope

Add explicit volatile retention policy and user actions to 01 controller/02 screen. Keep one payload and at most one attempt; no second full audio copy. Free/zero sensitive draft storage on acceptance or deliberate discard after reader cleanup. Preserve logical outcome facts separately from connection state. Adopt bounded error reasons: temporary connection/service, unavailable memory, conflict, authentication/recipient rejection. Technical details remain private.

## Non-goals

No persistent queue, reconnect-triggered background uploader, indefinite saved claim, new take used as Retry, retry of mismatched immutable metadata, or reclaim of server-accepted messages.

## Relevant implementation surfaces

01 message/payload modules; `firmware/v1/v1_record.c` failure UI and worker; 04 transport/status adapter; `x02_main.c` input routing; `v1_connect.c`; `docs/BOX-UI.md`.

## State and transitions

`sending → failed/uncertain + RAM retained → Retry → sending → sent`. Offline keeps the same retained state with Retry unavailable. `failed/uncertain → Discard requested → transport releases → RAM released → prior inbox`. Reset/power loss deletes volatile state; it cannot recover after reboot.

## Edge cases

Repeated Retry taps; server accepted before lost reply; offline during lookup; Discard with request in flight; allocation failure; recipient removed; bad hash conflict; new user signs in; reconnect while child reads failure card.

## Acceptance criteria

- Failure leaves the same bytes/hash/targets available without another recording.
- Retry creates no duplicate recipient references, including after a response drop and server restart.
- No second operation starts while one borrow/attempt remains live.
- Discard is explicit and no optimistic queued/saved badge appears.
- Known loss across power/reset is documented honestly, not tested as survival.
- Reconnection alone sends nothing.

## Verification

Automated fake transport/clock: drop response after acceptance, offline → online, double tap, discard race, hash conflict and OOM. Host counts one canonical/reference set. Device/manual: disconnect before Send, retain draft, reconnect, Retry without speaking again, observe receipt; reboot separately proves the documented loss behavior, not recovery.

## Evidence to leave behind

`docs/evidence/product-no-storage/05-ram-retry/`: media hashes across attempts, one-delivery counts, UI action matrix, cleanup ownership and reset-loss observation. Use generated audio; no family recording fixtures.

## Stop conditions

Stop if 04 idempotence/status lookup is not verified, if preserving a draft exceeds measured memory, or if session separation leaks media. Do not implement NVS/audio or an SD fallback to make tests pass. Privacy/sign-out behavior must use 14 or an explicit interim discard, never an invented cross-user queue.
