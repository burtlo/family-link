# Concept guide: resource budget envelope

Status: explanatory guidance with v1 device examples  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md) §8  
Related assessments: [carousel/playback](carousel-playback.md) and
[recording/send](recording-send.md)

## Definition

A resource budget envelope states the maximum resources a scene or operation may
consume while still meeting responsiveness and reliability requirements. It
turns “probably fits” into an enforceable design input.

The concept is hardware-agnostic. Targets vary, but every runtime has finite:

- memory and allocatable contiguous blocks;
- presentation nodes and rendering work;
- task/thread stacks;
- queues and concurrent operations;
- storage and network buffers;
- CPU/frame time;
- input/media size.

A budget is not merely a current measurement. A measurement says what happened in
one run. A budget says what is allowed and what the implementation must do when it
would exceed that allowance.

## Envelope shape

```text
ResourceBudget
  scope                   scene or operation name
  target_class            constrained, standard, high-memory, ...
  max_live_nodes
  max_transient_bytes
  max_persistent_bytes
  max_single_allocation
  max_stack_bytes
  max_queue_depth
  max_concurrent_operations
  maximum_input_size
  max_frame_or_callback_ms
  degradation_strategy
  allocation_failure_outcome
  measurement_points
```

Use target classes rather than embedding a board model into architectural rules.
A concrete build maps a target class to measured limits.

## Budget categories

### Persistent versus transient memory

Persistent memory remains for the scene/session lifetime. Transient memory appears
during decode, rebuild, upload, or transition. Peak usage often occurs when both
old and new resources coexist.

Track both. Average free heap does not reveal a failed large allocation.

### Largest allocation and fragmentation

A runtime may report enough total free memory but lack one contiguous block.
Where relevant, measure:

- total free bytes;
- largest allocatable block;
- high-water usage;
- allocation failures by size and capability/class.

### Presentation budget

Count live nodes, layers/canvases, textures/framebuffers, callbacks, animations,
and scene construction time. Bound collection size through virtualization or lazy
construction.

### Concurrency and queues

Memory and latency multiply with concurrent operations. Define maximum active
login, play, capture, upload, fetch, and poll operations. Queue capacity requires
an overflow strategy, not silent loss.

### Time budget

Resources include time:

- input callback duration;
- frame/update duration;
- scene construction duration;
- codec/network worker latency;
- cancellation latency.

Measure p95/p99 and worst representative cases, not only happy-path averages.

## Degradation and failure

Every budget needs behavior at the boundary:

- virtualize or evict off-screen content;
- stream rather than buffer;
- reduce optional detail;
- reject operation before accepting user work;
- preserve work for later;
- show a defined terminal failure.

Crashing, rebooting, white-screening, silently dropping an event, or remaining in
progress are never valid degradation strategies.

## V1 example 1: carousel cards and sketches

### Current employment

V1 bounds messages with `V1_MSG_MAX` and logs free heap after carousel paint. It
avoids transform layers on cards and comments that scaling can exceed the LVGL
heap. Sketch framebuffers are allocated per card when needed.

### Missing envelope

The implementation eagerly creates every card and has no declared:

- maximum live widget/node count;
- maximum simultaneous sketch framebuffers;
- largest required UI allocation;
- minimum post-paint margin;
- scene build-time limit.

### Concrete v1 budget

```text
CarouselSceneBudget
  live_cards: center plus two neighbors
  active_sketch_buffers: one playing plus bounded cache
  collection_size: V1_MSG_MAX domain items, virtualized presentation
  scene_build_ms: measured threshold
  update_frame_ms: measured p99 threshold
  failure: card without optional sketch, never failed scene
```

The exact numbers require measurement on the target; the structure is portable.

## V1 example 2: playback media buffer and worker

### Current employment

V1 allocates a fixed `V1_PLAYBACK_BUF_CAP` of 320 KiB and creates a playback task
with an 8192-word stack, falling back to 6144 words. It logs task creation memory
and stack high-water after playback.

### Missing envelope

The whole media blob is fetched into memory. There is no documented maximum
message duration/encoded size relationship, behavior for a larger blob, or
concurrency rule between playback, chirp, and recording.

### Concrete v1 budget

```text
PlaybackOperationBudget
  concurrent_playbacks: 1
  buffered_media_bytes: bounded or streaming window
  codec_chunk_bytes: fixed
  worker_stack: measured high-water plus margin
  cancellation_latency: one chunk boundary
  oversized_media: typed failure, not partial decode or overflow
```

Streaming would reduce peak memory and make the input limit explicit.

## V1 example 3: recording, sketch, and upload copies

### Current employment

V1 bounds recording to three minutes and allocates:

- maximum PCM capture buffer;
- full-screen RGB565 canvas;
- maximum sketch-point array;
- packed sketch blob;
- complete multipart upload body containing another media copy.

Allocations prefer external memory and fall back to internal memory.

### Missing envelope

The combined peak and largest allocation are not declared. Falling back to
internal memory may consume memory needed by presentation or task stacks.

### Concrete v1 budget

```text
SendOperationBudget
  maximum_capture_seconds: policy-defined
  capture_window_bytes: streaming/ring buffer
  retained_unsent_bytes: policy-defined
  drawing_points: bounded with visible degradation policy
  multipart_copy_bytes: zero through streaming encoder
  concurrent_uploads: 1
  allocation_failure: terminal SendFailed(resource_exhausted)
```

The user-visible contract must say whether captured work is retained when resource
allocation or upload fails.

## V1 example 4: session event queue

### Current employment

`v1_state.c` creates a queue of eight state events. Producers use non-blocking send
but ignore the return value.

### Missing envelope

Queue depth is a resource budget, yet overflow behavior and telemetry are absent.
A full queue can silently discard authentication or navigation events and leave
the visible stage inconsistent.

### Concrete v1 budget

```text
SessionEventBudget
  queue_depth: derived from worst-case concurrent producers
  coalescible_events: repaint, inbox-invalidated, connectivity freshness
  non-droppable_events: operation terminal outcomes
  overflow: reject submission visibly or reserve terminal-event capacity
  telemetry: high-water and rejected event kind
```

Queue depth should be validated with burst and fault-injection tests.

## V1 example 5: roster and recipient collections

### Current employment

Roster and picker arrays are bounded by `V1_USER_MAX`, and presentation creates a
card for each user plus Everyone in the picker. Roster, picker, and carousel repeat
similar snap-card implementations.

### Concrete use

Declare one reusable card-collection budget:

```text
SnapCollectionBudget
  maximum_domain_items
  maximum_live_cards
  callbacks_per_live_card
  animation_count: 1
  preserved_identity: user ID or message ID
  off-screen policy: recycle card nodes
```

One measured component reduces duplicated resource behavior and makes roster,
picker, and carousel limits comparable.

## Measuring the envelope

Measure at:

- before scene construction;
- after construction;
- after worst-case population;
- during transition when old/new resources may overlap;
- worker start and terminal outcome;
- cancellation;
- repeated enter/exit cycles to expose leaks and fragmentation.

Record input cardinality and target class with every measurement. “Free heap” with
no scene size or largest-block value is insufficient evidence.

## Tests that enforce budgets

- Populate maximum collection and assert node/memory/build-time limits.
- Repeat scene entry/exit and assert no downward memory trend.
- Force largest-allocation failure and assert terminal UX.
- Run maximum media plus active UI and verify stack/heap margins.
- Fill queues and verify explicit overflow policy.
- Run competing operations and assert concurrency limits.
- Feed oversized input and verify rejection before unsafe allocation.

## Review checklist

- What is the scope and target class of the budget?
- Are persistent, transient, and largest allocations separated?
- Are collection size and live presentation nodes both bounded?
- Are queue depth and concurrent operations explicit?
- What happens when the budget cannot be met?
- Are measurements representative and repeatable?
