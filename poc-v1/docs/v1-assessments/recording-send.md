# V1 assessment: recording and send

Status: as-built assessment, not a v2 design  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md)  
Primary code: `firmware/v1/v1_record.c`, `v1_state.c`, `x02_main.c`  
Product contract: `docs/BOX-UI.md` § Record and
`docs/plans/v1-product-spec.md` § Recording

## Purpose and boundary

This subsystem presents recipient selection, captures microphone audio and drawing
input, builds WAV and sketch payloads, uploads one or more messages, and presents
the send receipt.

The intended boundary is:

- presentation: recipient picker, recording indicator/timer/canvas, receipt;
- session: selected recipients, active record/send operation, send phase;
- I/O: microphone/codec, media encoding, multipart upload, inbox refresh.

V1 places these responsibilities in one large module. Capture and upload run on a
worker, but screen painting also initiates network work and feature code directly
assigns session stages.

## User-visible stages

### Recipient selection

- Entry: red-circle action from carousel.
- Interaction: focus a recipient or Everyone; long press supports multi-select.
- Success: immutable recipient snapshot and transition to recording.
- Cancel: shoulder returns to carousel without capture or upload.
- Contract timing: picker timeout is configured as 10 seconds.
- Never acceptable: record begins for a visually different recipient, stale
  selection survives another session, or opening the picker blocks rendering.

### Recording

- Entry: selected target, online, unmuted, microphone/resources available.
- UX: start chirp, active record indicator, elapsed time, optional sketch.
- Completion: circle stop, five seconds silence, or three-minute cap.
- Cancel: shoulder discards without upload.
- Failure: mute and resource failures require explicit feedback.
- Never acceptable: microphone remains open after exit, cancelled recording
  uploads, or scene repaint resets an active capture.

### Finishing and sending

- Entry: capture completion.
- UX: `Finishing`, then `Sending...`.
- Success: `Sent`, recipient summary, approximately two-second receipt, then
  carousel.
- Failure: `Couldn't send`, then return; v1 currently discards failed work.
- Guard: v1 exits a stuck finishing/sending stage after 25 seconds.
- Never acceptable: false `Sent`, indefinite receipt, duplicate send on retry, or
  silent partial multi-recipient success.

## What v1 gets right

- Microphone capture and upload run on a dedicated worker.
- Record inputs are bounded by maximum duration and preallocated capacity.
- Mute, missing microphone, allocation failure, cancellation, silence, and maximum
  duration are considered.
- UI timers are deleted when leaving the recording scene.
- Presentation handles for picker, record, and send are invalidated by stage.
- Send has explicit visible phases and a stuck-stage escape.
- Multipart body allocation is checked and failures do not report success.
- Receipt copy largely matches the product brief.
- Scene paint reports heap for picker and record.

## Standards assessment

### Critical — record/send is not one explicit operation

Violates Standards §4.

The lifecycle is distributed across `s_record_armed`, `s_recording`,
`s_stop_record`, `s_cancel_pick`, `s_send_phase`, current stage, recipient arrays,
and worker notification state. Several Boolean combinations represent meaningful
states, but no operation ID binds recipient selection, capture, upload, and receipt.

A next-version operation should freeze all user intent:

```text
SendOperation
  id
  session_id
  recipients or broadcast
  record policy
  created_at

  selecting -> recording -> encoding -> uploading -> terminal
```

Terminal outcomes must include success, failed, cancelled, stale, timed out, and
partial success where multi-recipient sends are not atomic.

### Critical — session transitions have multiple writers

Violates Standards §3.

`v1_record.c` calls `v1_state_apply` from presentation callbacks, the worker, the
send watchdog, and cancellation paths. The central reducer therefore cannot
guarantee legal transitions or reject late worker results.

All feature paths should emit typed events such as:

```text
RecipientsConfirmed
CaptureStarted
CaptureCompleted
SendSucceeded
SendFailed
SendCancelled
SendTimedOut
```

Only the session reducer should decide the next stage.

### Critical — picker paint performs network I/O

Violates Standards §§1–2.

`paint_pick` calls `v1_connect_load_hangout_ms(V1_CONNECT_PROBE_MS)` immediately
after destroying the previous scene. Rendering can therefore wait up to the probe
timeout before constructing the picker.

The picker must render from a session snapshot. A background refresh may later
apply an identity-keyed Level 1 update.

### High — picker timeout is declared but not implemented

Violates Standards §7.

`V1_UI_PICK_TIMEOUT_MS` is generated as 10 seconds, but
`v1_record_tick_pick_timeout` always returns false. A search shows no active
implementation in the product module.

This is a concrete example of why generated constants do not prove behavioral
parity. Contract tests must assert that the timeout causes the specified terminal
event and next stage.

### High — send completion can race cancellation or a newer operation

Violates Standards §4.

The worker applies send phases and later returns to carousel without checking an
operation generation. A cancellation, session reset, or future send could make a
late upload result stale, but the result has no identity with which to reject it.

The next client must compare operation and session IDs before applying any capture
or upload completion.

### High — multi-recipient upload has undefined partial success

Violates Standards §§7 and 9.

V1 posts one request per selected recipient and stops on the first non-200 result.
Earlier recipients may have received the message, while the receipt shows
`Couldn't send`. There is no operation key, per-recipient result, compensation, or
copy for partial success.

The product must decide one of:

- server-side atomic fan-out;
- explicit per-recipient result and partial-success UX;
- idempotent retry with a shared send operation ID.

This is an OPEN product decision, not something implementation should silently
choose.

### High — failed user work is discarded

Relates to Standards §§7 and 9.

The current v1 plan explicitly treats discard-on-fail as as-built until outbox
behavior is decided. That may be acceptable for v1, but the next version must
state it before implementation and provide truthful copy. An outbox, persistence
medium, retry trigger, power-loss behavior, capacity, and idle copy remain product
decisions.

### High — upload and receipt timing are split across sources

Violates Standards §§7 and 12.

- Upload timeout is a private 20000 ms literal.
- Stuck-stage guard is `V1_SEND_STUCK_MS` at 25000 ms.
- Receipt duration comes from shared timing.

The next client needs one policy object describing per-attempt timeout, total
operation deadline, retry policy, and receipt dwell. A UI watchdog must not be the
only cancellation mechanism for an I/O operation.

### Medium — resource envelope is bounded but expensive

Relates to Standards §8.

V1 allocates:

- a full maximum-duration PCM buffer;
- a full-screen RGB565 recording framebuffer;
- maximum sketch points and packed sketch storage;
- another complete multipart body containing WAV and sketch.

This creates multiple copies of large user data. Allocation fallback to internal
memory increases pressure on UI and task resources.

The next client should prefer bounded streaming from capture/storage into an
idempotent upload, with explicit maximum retained work and measured memory
high-water marks.

### Medium — no-recipient and zero-audio outcomes are weakly classified

Relates to Standards §§4 and 7.

No recipients, mute, no microphone, allocation failure, cancellation, near-silent
audio, and upload failure are represented through a mixture of toasts,
`SEND_PHASE_FAILED`, and direct return to carousel. The user contract does not
fully distinguish these outcomes.

Use typed failure reasons first; map them to copy only after the product contract
decides which distinctions matter to the user.

## Performance priorities

1. Remove hangout refresh from picker paint.
2. Stream or chunk media rather than constructing duplicate full payloads.
3. Freeze recipient/session input in a send operation envelope.
4. Replace direct stage assignments with reducer events.
5. Avoid repainting the whole receipt scene when only phase text changes.

## Reliability tests for the next client

- Picker opens immediately while roster refresh is delayed.
- Ten-second picker timeout produces exactly one cancellation/timeout event.
- Shoulder cancel before, during, and after mic open never uploads.
- Stop and silence completion racing together produce one capture completion.
- A late upload result from operation A cannot alter operation B.
- Timeout cancels or detaches underlying I/O; it does not merely navigate away.
- Multi-recipient failure follows the chosen atomicity contract.
- Allocation failure closes microphone/codec resources and reaches terminal UX.
- Repeated stop input cannot enqueue duplicate uploads.
- Power loss and offline behavior match the decided persistence policy.

## Open product decisions

- Whether failed sends are discarded or retained in an outbox.
- Atomicity and copy for multi-recipient partial success.
- Exact UX for empty/near-silent recording versus microphone failure.
- Retry policy and idempotency for upload.
- Whether sketches are required, optional, or independently recoverable.

## Verification level of this assessment

This is source and contract analysis only. It identifies an as-built missing picker
timeout and concurrency/resource risks; it does not claim a new target run.
