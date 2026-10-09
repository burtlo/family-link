# Concept guide: asynchronous operation envelope

Status: explanatory guidance with v1 device examples  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md) §4  
Related assessments: [carousel/playback](carousel-playback.md),
[authentication/connectivity](authentication-connectivity.md), and
[recording/send](recording-send.md)

## Definition

An asynchronous operation envelope is the durable identity and lifecycle around
work that continues after the initiating callback returns. It answers:

- What exact work did the user submit?
- Which session and domain object does it belong to?
- Is this still the current operation?
- Has cancellation been requested?
- Which one terminal outcome ended it?

The envelope prevents a worker from depending on mutable global state. Without it,
the worker may start for one user, message, or recipient and finish after the UI
has moved to another one.

The envelope is not merely a `busy` Boolean. `busy` says that something may be
happening; it cannot say which operation, what its frozen inputs were, why it
ended, or whether a late result is stale.

## Minimum structure

```text
Operation
  id             unique or monotonically increasing
  kind           login, play, upload, probe, ...
  session_id     identity of the owning signed-in/out session
  submitted_at   monotonic time
  deadline       optional total deadline
  attempt        retry number
  input          immutable operation-specific snapshot
  status         pending | running | terminal
  cancel_token   non-blocking cancellation state

TerminalOutcome
  success(result)
  failure(reason, retryable)
  cancelled(reason)
  stale(reason)
  timed_out(reason)
```

The session owner stores the current operation identity. A worker receives a copy
or immutable reference. Completion is applied only if the operation and session
identities still match.

## Lifecycle rules

1. Submission validates the request, allocates an ID, and freezes all inputs.
2. Queue acceptance is observable. A full queue is a submission failure, not
   invisible success.
3. The worker reports progress as optional events and reports exactly one terminal
   outcome.
4. Cancellation sets a token or posts a command; the caller never spins waiting.
5. The session owner rejects late outcomes as stale.
6. Retry either increments `attempt` under the same logical operation or creates a
   new operation ID according to the persistence/idempotency contract.
7. Terminal state is immutable and emits structured telemetry.

## Implementation shape

Framework-neutral pseudocode:

```c
typedef struct {
    uint64_t id;
    operation_kind_t kind;
    uint64_t session_id;
    int64_t submitted_at_ms;
    int64_t deadline_ms;
    uint32_t attempt;
    atomic_bool cancel_requested;
    operation_input_t input;
} operation_t;

typedef struct {
    uint64_t operation_id;
    uint64_t session_id;
    outcome_kind_t kind;
    failure_reason_t reason;
    operation_result_t result;
} operation_outcome_t;
```

The worker may own device or network handles. It does not own the user-visible
stage and never mutates widgets.

## V1 example 1: PIN login

### Current employment

`v1_auth.c` already has part of an envelope:

- `s_login_generation` and `s_login_pending_gen` identify an attempt;
- pending user and PIN are copied before the worker wakes;
- the worker compares generation before applying success.

This is a strong start because it avoids rereading the visible PIN slots as input.

### Missing pieces

- The session identity is implicit.
- Timeout, cancellation, stale, wrong credential, server failure, and transport
  failure are not one typed outcome family.
- `V1_EV_AUTH_FAIL` and `V1_EV_AUTH_STALE` do not reconcile state in the reducer.
- Queue/semaphore submission does not communicate rejection to the operation.
- Some stale paths directly clear `busy` and request repaint.

### Concrete v1 adaptation

```text
LoginOperation
  id: generation
  session_id: signed_out_session_generation
  input: { user_id, four_digit_pin }
  deadline: submitted_at + login_timeout

LoginOutcome
  authenticated(session_user, pin_reset)
  wrong_credential
  service_unavailable
  cancelled(user_back)
  stale(session_changed)
  timed_out
```

`v1_auth_login_task` would post one `LoginOutcome`. `v1_state_drain` would be the
only code that clears the active login and chooses PIN, connecting, cooldown, or
carousel.

## V1 example 2: carousel playback

### Current employment

Playback has a dedicated worker and a natural operation boundary: one selected
message from one position. V1 also posts transport refresh rather than repainting
the whole scene when playback ends.

### Missing pieces

Playback is represented by `s_want_play`, `s_stop_play`, `s_playing`, current
focus, and mutable message position. `stop_playback()` spins until the worker
changes a flag. A late worker completion has no identity proving it belongs to the
currently focused message.

### Concrete v1 adaptation

```text
PlayOperation
  id
  session_id
  input: { message_seq, start_position_ms, expected_media_kind }

PlayProgress
  { operation_id, position_ms }

PlayOutcome
  completed(final_position_ms)
  fetch_failed(reason)
  codec_failed(reason)
  cancelled(focus_changed | user_stop | idle_lock)
  stale(session_or_message_changed)
```

Focus change requests cancellation and proceeds immediately. The worker stops at a
safe boundary and reports `cancelled`; no UI callback waits for it.

## V1 example 3: record and send

### Current employment

V1 bounds record duration and uses a worker for capture/upload. Its visible phases
(`Finishing`, `Sending...`, `Sent`, `Couldn't send`) already resemble operation
progress.

### Missing pieces

The operation is fragmented across `s_record_armed`, `s_recording`,
`s_stop_record`, `s_cancel_pick`, recipients, send phase, and session stage.
Recipients are mutable globals, and no ID prevents a late upload from completing a
newer send.

### Concrete v1 adaptation

```text
SendOperation
  id
  session_id
  input: { recipients, broadcast, capture_policy }
  phase: recording | encoding | uploading

SendOutcome
  sent(message_ids)
  failed(reason, retained_work?)
  partial(per_recipient_results)
  cancelled(before_upload | during_capture)
  stale(session_changed)
  timed_out
```

Recipients are frozen when recording starts. Capture and upload can be separate
child operations under the same send ID. Multi-recipient results become explicit
rather than collapsing to the last HTTP status.

## V1 example 4: signed-out hangout probe

### Current employment

V1 has retry and attempt timeouts and correctly requires an application-level
hangout response before showing the roster.

### Missing pieces

The probe has no operation ID. A result is applied through shared
`s_server_online`, and the poller policy is encoded through
`v1_state_may_probe()`. There is no formal stale result if login starts while a
probe is in flight.

### Concrete v1 adaptation

```text
ProbeOperation
  id
  session_id
  input: { purpose: signed_out_readiness }
  deadline: submitted_at + probe_timeout

ProbeOutcome
  ready(roster_snapshot, freshness)
  unavailable(reason)
  cancelled(user_operation_started)
  stale(newer_probe_or_session)
  timed_out
```

The poller schedules the operation. The session reducer decides whether the result
may alter connectivity truth or stage.

## Tests that prove an envelope works

- Submit A, then B; complete A last. A is recorded as stale and cannot alter B.
- Cancel during each worker phase; each accepted operation emits one terminal
  outcome.
- Force queue full; the UI receives submission failure and never shows indefinite
  progress.
- Fire timeout and success concurrently; only one terminal outcome wins.
- Change session while work runs; completion cannot mutate the new session.
- Retry a remote write; idempotency policy prevents duplicate user work.

## Review checklist

- Are all worker inputs immutable and captured at submission?
- Is operation identity carried through progress and completion?
- Can every accepted operation terminate exactly once?
- Are timeout, cancel, and stale distinct from failure?
- Is cancellation non-blocking for presentation?
- Does only the session owner apply the outcome?
