# Slice 4 — recording, sketching and durable send

Date: 2026-10-30
Status: Defined for review; delivery/storage interfaces require current-source planning.
Prerequisites: delivered Slice 2; Slice 3 for end-to-end recipient playback.

## Outcome

Replace `RuntimeDestination::Recording`'s `NoDestinationViewController` with
`SketchViewController`. Capture audio and optional multicolor sketch, commit a
durable finished message, deliver it to frozen recipients, and show truthful
preparing/sending/sent feedback. Preserve pending sends across navigation,
mute, outages and restart according to the experience.

## Sources and integration seams

- [Recording and sending](../../product/experiences/message-recording-and-sending.md):
  ordinary journey, controls, wipe/collapse, failure classes, keep/resume,
  recipients-only publication, acceptance and confirmation timing.
- [Sketch](../../product/experiences/sketch-record-and-playback.md): canonical
  120×120 coordinates, 240×240 interactive surface, palette and point semantics.
- [Message delivery](../../product/communication-contracts/message-delivery.md).
- [Record UI](../../product/device-ui/screens/SCR-RECORD-SKETCH.md) and
  [send feedback](../../product/device-ui/screens/SCR-SEND-FEEDBACK.md).
- [Memory envelope](device-ui-view-controller-memory-envelope.md) and
  [Slice 3](device-ui-view-controller-third-slice-playback.md).
- LAB-10 capture/ring evidence and [LAB-16 overlap](../../labs/evidence/lab-16/results/overlap-record-during-upload.md).
- Current `product_runtime.*`, target service/request payload, surface broker,
  storage/media abstractions, microphone capture, shared record/send models and
  workers. Inspect legacy lifetime coupling before reusing their functions.

Lab outcomes are supporting evidence, not acceptance of the integrated product.

## Ownership

| Owner | Responsibility |
|---|---|
| Root/coordinator | Child appearance and specified stage motion; no hidden sibling scenes |
| `SketchViewController` | Canvas/palette objects, drawing input, leased render surface |
| Record service + capture worker | Audio/stroke clock, bounded queues, begin/finish/discard and capture terminal result |
| Media/outbox store | Append-only audio/strokes, finished commit, recipient/message identity and durable retry state |
| Upload service/worker | Bounded storage reads, idempotent transport, acceptance/refusal classification and progress |
| `SendFeedbackViewController` | Snapshot-driven header and preparing/sending/sent/wait/failure body |

Finished media and upload work do not belong to either controller. No view
constructor starts a worker or resets an existing send journey.

## S4-P1 — durable take and outbox storage

Define storage records for unfinished take versus committed message, including
stable client message ID, owner identity, frozen recipient IDs, formats,
lengths/integrity, audio/stroke streams and upload progress. Use existing local
non-NVS media storage. Commit metadata atomically enough to distinguish a
complete finished send after power loss; test every commit boundary.

Use bounded append/read chunks. Do not hold complete PCM, WAV or stroke history
in RAM. Existing appends and upload-reader access require explicit arbitration
that does not block LVGL. Storage failures have explicit terminal outcomes;
defer a new boot-space UI without ignoring actual append/read failure.

Verifier tests: restart before/after commit, torn writes, truncated media,
integrity failure, duplicate IDs, unfinished cleanup, committed retention,
bounded queues, simultaneous writer/reader and same-user ownership on resume.

## S4-P2 — record service and capture pipeline

Freeze recipients when valid RED begins the recording journey. Start hardware
capture as needed, but sent audio and sketch time zero is wipe completion;
speech before that boundary is excluded. Recording runs for up to three minutes,
finishes on RED, and never ends merely because of silence.

Capture workers append audio and compact `flsk2` strokes to storage through
bounded queues. Touch capture emits adjacent logical pixels along moving touch,
without bridging lifted strokes. Full sketch stops taking more ink and keeps
audio. Initial microphone unavailability returns the specified couldn't-record
outcome; mid-record failure follows the existing finish/send behavior.

BOOT during wipe or recording discards and returns Inbox. Mute discards
unfinished capture. Cancellation is non-blocking; acknowledge producer/consumer
quiescence before releasing queue/job storage. An accepted operation has exactly
one finish/discard/failure outcome.

Verifier tests: wipe cutoff, duration cap, immediate finish, audio-only,
multicolor/background strokes, point cap, queue overflow, mic start/midstream
failure, BOOT/mute races, late events, clock boundaries and storage faults.

## S4-P3 — `SketchViewController` and record motion

Install Sketch directly beneath Root. Acquire one lazy 240×240 RGB565 lease
(115,200 bytes), show the specified six-color 80×240 rail and black canvas,
and map touch coordinates by two into 120×120 logical coordinates. Default ink
is white. No timer, recording disk, eraser tool or extra on-screen controls.

Implement the specified ring-wipe into recording and reverse red-circle
collapse into feedback using the existing lab behavior as motion evidence.
Root remains the appearance owner; record motion must not introduce another
stage writer or require two live controller trees. Declare any pixel snapshot
or temporary lease and its lifetime, including overlap with the capture surface.
Canvas/palette input stays gated until wipe completion. RED is ignored during
wipe; BOOT cancels it.

On exit, preserve/commit required streams, quiesce display and capture references,
detach LVGL sources and return the surface. View teardown cannot delete a
finished outbox message or stop its upload.

Verifier tests: real-LVGL geometry, palette hit bounds/colors, coordinate
mapping, input before/after wipe, callback order, cancellation/finish-now,
allocation failure and repeated surface return. Target tests measure ink and
ring latency with actual allocator placement.

## S4-P4 — bounded upload and recovery

Upload from durable storage. Streaming during recording may send bounded
provisional chunks only if the active delivery contract supports them; no
recipient publication occurs before finished atomic acceptance. If that wire
does not support provisional chunks, retain recording locally and begin upload
after commit rather than inventing an endpoint.

Preserve stable message/recipient/content identity across retries. Backpressure
pauses read-ahead, not recording or UI. Timeouts, 5xx, connectivity loss and
expired credentials retain the message and wait; definite refusal follows
the specified couldn't-send/discard behavior. Accepted publication is only to
the frozen recipients, never a sender Inbox insertion.

Mute after finish retains work. Resume only for the same authenticated user;
restart resumes committed sends whose storage survives. Upload may outlive its
feedback view and overlap the next recording within measured limits. Worker
shutdown acknowledges terminal state before queues/jobs are destroyed.

Verifier tests: lost acceptance/retry, duplicate chunks/commit, refusal classes,
offline backlog/drain, credential replacement, different-user login, restart,
queue refusal, cancellation/quiescence, corruption and two-message overlap.
Record bounded outstanding bytes and internal/PSRAM largest blocks.

## S4-P5 — feedback and runtime completion

Implement `SendFeedbackViewController : SplitPaneViewController` with stable
`sending to ...` header and frozen names. Body progresses preparing → sending
→ sent, with specified dots and connectivity replacement. Show `sent` only
after server acceptance, dwell two seconds when still on that view, then return
to the prior Inbox selection paused. If the person left the wait screen, skip
sent presentation rather than navigating them back.

BOOT from connectivity wait returns Inbox without deleting the pending send.
RED from Inbox with pending work returns to that wait; ordinary preparing,
sending and sent ignore BOOT/RED as specified. Integrate same-user resume,
mute during every stage, and memory/resource failure outcomes through services.

Verifier tests: stable header, status/copy timing, no false sent, no autoplay,
return selection, wait exit, pending-send reentry, late acceptance offscreen,
same-user resume, and view destruction while upload continues.

## S4-P6 — integrated target delivery

Run host/model and real-LVGL checks, build, flash and saved monitoring with the
product server. Exercise normal audio-only and multicolor messages plus outage,
mute, restart and upload/next-record overlap. Use LAB-16-style interval minima
and event probes on the product, with stack watermarks and actual pointer-region
classification. Measure latency as well as free bytes.

Acceptance:

1. Valid targets enter the specified ring-wipe; touches are gated until it ends.
2. Sent audio excludes wipe-leading speech; colors and background paint replay
   correctly at 120×120 inside a recipient's Inbox card.
3. RED and duration cap finish; BOOT/mute discard unfinished work as specified.
4. Wi-Fi loss during recording leaves canvas/capture working with bounded RAM.
5. Finished sends survive wait navigation, mute and restart, and resume only
   for their owner; no duplicate recipient publication occurs.
6. `sent` follows acceptance and its visible dwell; the sender returns to the
   previous paused Inbox selection without a card of their own send.
7. One message uploading while another records stays within measured memory,
   stack, storage and responsiveness envelopes.
8. No retained sketch lease, stale view handle, stopped upload from view unload,
   unbounded backlog in RAM, worker lifetime race, or false-success state.

## Sequence and remaining gates

S4-P1 → S4-P2 → S4-P3 → S4-P4 → S4-P5 → S4-P6. Test packages independently,
but do not ship a local-record-only product route or claim completion before
durable delivery and recipient playback pass together.

Before dispatch, identify the current concrete media append/read APIs, atomic
commit boundary, delivery wire operations and audio codec. Resolve any OPEN
contract item needed by that package. Keep initial lazy surface and 32 KiB
LVGL policy; record measured changes explicitly. Boot storage-health UI, profile
editing, remote administration and a settings hub remain separate future work.
