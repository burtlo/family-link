# Slice 3 — in-card audio and sketch playback

Date: 2026-10-30
Status: Implemented in the product runtime. Host LVGL verifier tests pass. Target build, flash, and monitor are still open.
Prerequisite: owner-delivered Slice 2 (PIN, Inbox browsing, Volume, targets).

## Outcome

The selected Inbox card plays audio and a synchronized sketch. Play/pause/resume,
selection changes, completion, connectivity loss, navigation and mute preserve
or discard state exactly as the current Inbox experience requires. Restore the
header transport action omitted during Slice 2.

No new screen or playback view controller is introduced. `InboxViewController`
continues to own the visible carousel and binds its selected card to one leased
120×120 RGB565 playback surface (28,800 bytes). Playback state and workers live
outside that view.

## Sources and current seams

- [Inbox experience](../../product/experiences/message-carousel-and-playback.md),
  especially playback, continuity, connection status, selection and error scenarios.
- [Sketch experience](../../product/experiences/sketch-record-and-playback.md):
  `SK-001`–`SK-009`, `EXP-SKP-001`–`EXP-SKP-009`; logical coordinates and pixel replay.
- [Inbox screen](../../product/device-ui/screens/SCR-MESSAGE-INBOX.md).
- [Memory envelope](device-ui-view-controller-memory-envelope.md).
- Active inbox retrieval/last-viewed and message-delivery communication contracts.
- `client/device/main/product_inbox_service.*`,
  `product_inbox_view_controller.*`, `product_inbox_http_worker.*`,
  `product_runtime.*`, and the existing shared sketch parser/playback models.

Reuse tested algorithms through narrow interfaces. Do not restore a
`CarouselSession` framebuffer plus scratch plus per-card canvas copy chain.
Verify actual legacy allocation/caller state before reusing any bridge.

## Ownership and data flow

```text
Inbox input → PlaybackService intent(message ID, generation)
                 → bounded fetch/storage/decode worker
                 → audio output clock + timed stroke consumption
                 → snapshot/event → selected-card renderer

DisplaySurfaceBroker → one exclusive selected-card surface lease
MediaStore → bounded audio read/cache and stroke/checkpoint storage
```

Services own selected message playback positions and stroke offsets by identity.
The controller owns LVGL objects and borrows the active surface. Workers never
touch widgets. View unload quiesces display references and returns its lease,
while session state remains reconstructible.

## S3-P1 — shared surface and media-storage contracts

Deliver a `DisplaySurfaceBroker` (or equivalently explicit resource owner) and
bounded media access interfaces. Initial surface allocation is lazy. Each lease
declares owner, dimensions, RGB565 format, bytes, heap capabilities, generation,
outstanding consumers, and release boundary. Reserve no 240×240 surface yet.

Media reads/downloads run on workers with bounded chunks and response limits.
Persist/reuse fetched assets where appropriate; no whole-audio RAM requirement.
Keep durable asset cache distinct from session-only playback progress. Preserve
the existing product restriction against playing cached media through a
connectivity/session-readiness outage.

Verifier tests: exclusive acquisition, failed allocation, double release,
stale generation, references outstanding at transfer, fixed buffer counts,
bounded reads, interrupted fetch, cache integrity and cleanup. Compare internal
versus PSRAM interaction on target before declaring interactive placement final.

## S3-P2 — audio playback service and worker

Implement start/pause/resume/restart bound to immutable message identity and
signed-in generation. Derive sketch timing from the actual audio playback clock,
not wall time or the UI tick. Use bounded decode/output queues. Pause before
selection change, settings/target navigation, or connectivity loss. Mute stops
output and clears session progress; stale completions cannot alter a later user.

Unplayable media before start and mid-play failure use the experience's
`can't play` behavior and preserved-position rules. A worker fault has one
terminal result. Stop/cancel is non-blocking for presentation; queues/tasks are
released only after quiescence acknowledgment. Volume uses the settings service's
current endpoint stop, including silent.

Verifier tests: first start, pause/resume, completed replay from zero, selection
race, signed-out late completion, underrun/decode failure, queue refusal,
never-completing operation, and worker shutdown. No scrubbing or autoplay.

## S3-P3 — timed sketch replay and paused reconstruction

Consume validated `flsk2` points at their audio-relative timestamps. Paint each
stored point as its own pixel, including background-colored points; do not
connect replay points or bridge lifted strokes. Render directly into the leased
surface when safe. One bounded temporary scratch surface is permitted only if
correctness or measured timing requires it, with a declared release point.

Preserve playhead, stroke offset, and ink-visible state by message ID. On lease
release, retain a compact reconstructible state and optionally a storage-backed
checkpoint. On reacquisition, reconstruct the paused/completed frame before
showing it. Checkpoint format is an engineering choice backed by measured size,
reconstruction latency and integrity checks; it must not become one permanent
RGB565 allocation per message.

Paused/completed cards retain their specified sketch appearance. Virtualized
visible neighbors that need retained ink use bounded image decoding or replay
from stored state. Releasing a surface must not silently revert a visible paused
card to its portrait. Any unavoidable change to that visible behavior requires
owner review rather than an allocation shortcut.

Verifier fixtures: multicolor, audio-only, single point, fast diagonal,
separate strokes, background paint, malformed/late points, long stroke stream,
pause/resume at boundaries, checkpoint reconstruction, and repeated selection.
Sketch-only faults do not stop playable audio or show an audio failure.

## S3-P4 — Inbox view and runtime integration

Add header Play/Pause according to the experience. Card gestures still select
only. Hide portrait and metadata while ink is visible, restore them when no ink
applies. Bind the shared live surface to the selected card without a second
frame copy. Detach all LVGL image references before returning a lease.

Maintain the three-card browsing strategy and stable IDs from Slice 2. Pause
and preserve before BOOT/RED; reconstruct on return without autoplay. A new
message or remote last-viewed update must not steal selection. Connectivity
status replaces the lower pane and disables transport while preserving state.
Keep credential refresh and health independent of playback and view lifetime.

Verifier tests: actual transport and card events, label change detection,
same-snapshot render, portrait/ink switching, view unload/reload, card rebind
during worker completion, status crossfade, navigation, and mute mid-transition.
Run existing browsing/authentication/settings tests to catch regressions.

## S3-P5 — target qualification and delivery

Run host/model and real-LVGL suites, build, flash, server and saved monitor.
Use existing server messages as fixtures; do not require Slice 4 capture.
Record build ID, asset identity, frame/clock timing, audio underruns, UI latency,
LVGL pool free/largest/high-water, stack high-water and each buffer's actual
region. Measure paused reconstruction and selection changes on the device.

Acceptance:

1. Audio-only and multicolor messages start only on Play; pause/resume and
   completed replay behave correctly.
2. Drawing stays synchronized and framed at 120×120 inside the selected card.
3. Changing selection, then returning, restores paused/completed state without
   autoplay or permanent per-card framebuffers.
4. BOOT → Volume → Inbox and RED → targets → Inbox preserve selection/progress.
5. Wi-Fi/server loss pauses immediately; recovery stays paused. Mute clears
   session progress; later login starts unplayed.
6. Missing sketch leaves audio playing; audio failure shows specified feedback.
7. Every Inbox item remains reachable, including larger-than-visible lists.
8. Repeated play/selection/navigation cycles recover resources; no competing
   timers, stale pointers, leaked leases, or whole-message RAM growth.

## Execution order and exclusions

S3-P1 → S3-P2 → S3-P3 → S3-P4 → S3-P5, each implemented then independently
verified. Media format/decoder details follow the active wire contract; resolve
material contract gaps before dispatch, not in widget code.

Deferred to Slice 4: microphone capture, recording surface, outbox, upload,
record ring-wipe and send feedback. The existing recording stand-in remains.
