# Device UI view-controller memory envelope

Date: 2026-10-29  
Revision: 1  
Status: **proposal for owner review** — owner direction recorded; numeric budgets
remain measurement-gated  
Audience: product owner and implementers  
Scope: `client/device/` view-controller, media, transition, and inbox-sketch
memory architecture; this document does not authorize implementation

## Owner direction captured by this document

- Audio recording is storage-backed and incrementally streamable. A complete
  recording does not need to remain in RAM.
- Sketch stroke data and sketch pixels are different resources. Stroke data is
  durable; large rendered surfaces are temporary.
- `SketchViewController` may acquire a large interactive surface while it is
  loaded and must release that surface after its visual work is complete.
- Inbox sketch playback remains inside the selected message card.
- Inbox cards do not each keep an RGB565 sketch buffer merely so any card could
  later be selected.
- The selected card uses one shared live playback surface. Paused or completed
  state is preserved as durable sketch data and, where justified, a bounded
  checkpoint or thumbnail—not as one permanent uncompressed framebuffer per
  card.
- The LVGL budget includes the active controller hierarchy and an explicit
  transition reserve. PIN, settings/Volume, recipient picker, send feedback,
  and other inactive view trees are unloaded rather than retained hidden.
- Internal SRAM, DMA-capable internal memory, PSRAM, LVGL object memory, and
  persistent storage are separate ledgers. They are coordinated but are not
  interchangeable.

The intended inbox behavior remains the current owner-directed behavior: Play
animates the sketch within the centered message card.

## Goals

1. Keep only the active view hierarchy and deliberately admitted transition
   resources live.
2. Make long recordings independent of available RAM.
3. Bound sketch pixel memory regardless of inbox message count.
4. Make every large allocation identify its capability, lifetime, owner, and
   fallback.
5. Preserve recording, upload, playback, and paused-sketch state across view
   destruction without retaining LVGL objects or borrowed pixel pointers.
6. Turn transition memory from an accidental peak into an admitted budget.
7. Recover deterministically when a resource request cannot be satisfied.

## Evidence boundary

### Current product facts

- Product LVGL uses one 32 KiB `CONFIG_LV_MEM_SIZE` pool.
- The record canvas is 240×240 RGB565: 115,200 bytes (112.5 KiB), allocated
  outside the LVGL pool.
- One inbox sketch surface is 120×120 RGB565: 28,800 bytes (28.125 KiB).
- Current inbox code can hold a selected sketch surface, a scratch surface,
  and per-card canvas buffers.
- Current record code tries internal 8-bit memory for the record canvas and
  falls back to PSRAM; the canvas pointer is retained after surface teardown.
- Product microphone capture uses a 2 KiB staging buffer and appends captured
  bytes to SD-backed message storage.
- The current product mounts SD storage before display initialization and uses
  it for record/send media rather than NVS.

### LAB-16 evidence

The accepted LAB-16 `overlap-record-during-upload` pair measured, at its
record-while-uploading peak:

| Region | Minimum observed free |
|---|---:|
| Internal heap | 174.36 KiB |
| DMA-capable internal heap | 166.87 KiB |
| Largest DMA-capable block | 76.00 KiB |
| PSRAM | 15,315.07 KiB |

LAB-16 recorded to SD while upload and a later recording overlapped. Its
repeatability check passed for the accepted pair.

These figures establish feasibility for the lab architecture. They do not
establish the product margin: LAB-16 and `client/device/` have different
firmware, UI trees, tasks, configuration, and runtime loads. Product decisions
require equivalent same-boot product measurements.

## Memory domains

| Domain | Intended use | Allocation rule |
|---|---|---|
| LVGL object arena | Controller roots, widgets, styles, animations, LVGL metadata | Sized for root/container + largest active hierarchy + admitted transition + recovery reserve |
| Internal/DMA-capable SRAM | LCD flush, microphone/I²S, real-time queues, API-required DMA buffers, interaction-critical work proven to need internal memory | Protect capability-specific largest-block floors; never infer availability from aggregate free heap |
| General internal SRAM | Latency-sensitive work without a DMA requirement | Use only after accounting for Wi-Fi, audio, display, stacks, and transition peaks |
| PSRAM | Network bodies, bounded upload buffers, compact caches, reconstruction scratch, and interactive surfaces only if target latency passes | Preferred for large non-DMA buffers; each fallback is explicit and measured |
| Persistent local storage | Audio chunks, sketch stroke streams, checkpoints, thumbnails, outbox records, acknowledgements | Authoritative for data that must survive view destruction, network loss, or reboot according to product policy |

Internal-free and DMA-free values can describe overlapping physical memory and
must not be added. Persistent storage capacity is not heap capacity.

## Resource owners

```text
AppServices
├─ RecordService                 capture and take state
├─ SketchStreamService           durable strokes and checkpoints
├─ OutboxService                 durable message/upload state
├─ UploadService                 bounded chunk streaming and retry
├─ InboxService                  messages, selection, playheads
└─ PlaybackService               audio/sketch timing

DisplayResourceCoordinator
├─ LvglBudgetCoordinator         logical reservations and admission
└─ DisplaySurfaceBroker          exclusive/partitioned pixel leases

RootViewController
└─ admits and executes controller transitions
```

Services never own LVGL objects. Controllers never become the durable owner of
audio, strokes, upload state, or outbox state. A loaded view may borrow a pixel
surface only through a lease.

## Storage-first recording pipeline

### Required data flow

```text
microphone/I²S
  → small bounded capture buffer
  → RecordService
       ├─ append audio chunk to local message object
       └─ enqueue durable chunk identity for UploadService

UploadService
  → read bounded chunk from storage
  → stream request body
  → persist acknowledgement or retry state
  → release network buffer
```

Local storage is the authoritative copy while recording. Network delivery is
opportunistic and independently retryable.

### Recording rules

- Do not allocate a message-length PCM or WAV buffer.
- Keep capture buffers fixed and small. Their exact count and size are measured
  against producer/consumer jitter.
- Write sequentially. Do not wait for a full message before beginning storage
  or upload.
- Bound upload read-ahead and outstanding requests. A network stall must not
  grow RAM use.
- Persist enough metadata to distinguish captured, stored, queued, accepted,
  retryable, committed, and discarded chunks.
- Commit a message manifest only after audio and sketch streams reach the
  required durable boundary.
- View teardown does not stop an authorized upload. Service shutdown follows a
  worker-quiescence contract.
- Storage-full, write-failure, and unavailable-storage outcomes are explicit
  recording outcomes; they are not converted into unbounded RAM buffering.

### Network buffer placement

Network request bodies, response bodies, and upload chunks should prefer PSRAM
when the HTTP and Wi-Fi paths support it. Internal memory remains available for
driver-required structures, stacks, DMA, and UI work. Any library allocation
that remains internal should be attributed with heap tracing or event-bracketed
measurements before configuration changes are proposed.

## Durable sketch stream and temporary render surfaces

### Authoritative representation

The sketch's authoritative form is its compact stroke stream:

- logical coordinates;
- timestamp or audio-relative timing;
- color;
- stroke boundaries;
- erase operations when supported;
- format/version and integrity metadata.

The stream is appended to the in-progress local message object and may be
batched for network streaming. It survives destruction of
`SketchViewController` and `InboxViewController`.

### Recording surface

`SketchViewController` requests one 240×240 RGB565 surface lease. It paints
incoming strokes for immediate interaction while the same strokes are appended
to durable storage.

The surface is not the recording. When the view leaves:

1. stop accepting drawing input;
2. flush the last stroke batch;
3. quiesce every renderer and LVGL reference;
4. optionally write a bounded checkpoint;
5. detach the LVGL image/canvas source;
6. return the 112.5 KiB lease;
7. unload the controller's LVGL tree.

Re-entry reconstructs the surface from a checkpoint plus later strokes or from
the full stream. Reconstruction time must be measured on the target.

### Checkpoints and thumbnails

Checkpoints are an optimization, not the source of truth. Candidate forms are:

- compressed 120×120 RGB565 image;
- run-length encoded sparse sketch pixels;
- a stroke-stream offset plus a compact raster checkpoint;
- no raster checkpoint when complete replay meets latency.

Checkpoint frequency and format remain open until replay latency, storage
write cost, and encoded size are measured. A checkpoint should not silently
become a permanent uncompressed per-message RAM allocation.

## In-card inbox playback

### Required presentation

- Cards normally display their default portrait/name/duration presentation.
- The centered selected card acquires the one live inbox playback surface when
  Play is pressed.
- Sketch strokes render incrementally inside that card in sync with audio.
- Pause retains the product state: message identity, playhead, stroke-stream
  position, and visible-sketch status.
- The selected paused card may continue displaying the leased surface while it
  remains selected and the Inbox view remains loaded.
- When selection changes or Inbox disappears, the old card releases the live
  surface after its state is durable/reconstructible.
- Returning to the card reconstructs the same paused or completed visual state
  into the shared surface; it does not require that card to own a permanent
  framebuffer.

### Bounded pixel design

At steady state, Inbox owns at most:

| Buffer | Count | Bytes |
|---|---:|---:|
| Live selected-card playback surface | 1 | 28,800 |
| Optional reconstruction/transition scratch | 0 or 1 | Up to 28,800 |
| Per-card uncompressed sketch surfaces | 0 | 0 |

The preferred renderer paints directly into the live playback surface. It does
not keep separate model pixels, scratch pixels, and LVGL canvas pixels for the
same frame.

If direct rendering cannot safely update the surface, one bounded scratch
surface is permitted during reconstruction or transition. It is returned as
soon as the frame is installed. It is not multiplied by message count.

### Card virtualization

Only the center card and the minimum neighboring card chrome required by the
carousel are live LVGL objects. Non-visible messages remain model records.
Neighbor cards use the default card presentation unless a later owner-approved
visual requirement explicitly requires paused raster previews.

Collection refresh uses stable identities and rebinds the bounded card slots.
It does not materialize one LVGL subtree or pixel surface per inbox message.

## Display surface broker

### Arena modes

One backing arena may be interpreted in different mutually exclusive modes:

```text
Sketch mode
└─ 240×240 interactive surface                     115,200 B

Inbox mode
├─ 120×120 live playback surface                    28,800 B
├─ optional 120×120 scratch                         28,800 B
└─ remaining capacity available only through a declared lease

Transition mode
└─ bounded snapshot or wipe artifact                declared per transition
```

The arena is not part of the LVGL object allocator even when LVGL displays an
image backed by it.

### Lease contract

Every lease records:

- owner controller and service, if any;
- dimensions, pixel format, and byte count;
- required capabilities and accepted fallback regions;
- acquisition and release phase;
- whether LVGL, DMA, decoder, or worker references are outstanding;
- preservation action required before release.

Transfer is allowed only after the old owner has stopped producers/consumers,
quiesced outstanding references, preserved required state, invalidated borrowed
pointers, and detached the surface from LVGL.

### Allocation timing

Lazy first acquisition is the initial recommendation. Boot-time reservation
would reduce later fragmentation but would also withhold the largest surface
from Wi-Fi and other startup allocations before it is needed. The choice is
measurement-gated.

## LVGL active and transition budgets

### One physical arena, multiple logical reservations

The initial target is one LVGL allocator arena with logical reservations:

```text
LVGL total
├─ root + current container reserve
├─ active controller allowance
├─ transition allowance
├─ allocator fragmentation margin
└─ low-memory recovery allowance
```

Two logical budgets do not require two physical allocators. Hard physical
partitioning can strand free bytes and does not automatically associate an
LVGL allocation with a controller.

The configured pool size is selected from measurements:

```text
required LVGL bytes =
    root/container steady high-water
  + largest active-controller steady high-water
  + largest approved transition incremental peak
  + measured fragmentation margin
  + recovery-controller high-water
```

The current 32 KiB setting is an as-built value, not an accepted target for
this architecture. The existing LVGL-budget foundation's 64 KiB planning
default is also not an approved product number. Product measurements reconcile
the two.

### Controller resource declaration

Each controller declares a target envelope before implementation and measured
values after target validation:

| Field | Meaning |
|---|---|
| LVGL steady | Root and widgets after appearance completes |
| LVGL load peak | Temporary allocations while constructing the view |
| LVGL unload recovery | Bytes expected to return after teardown |
| Pixel leases | Dimensions, format, capability, lifetime |
| Other heap | Models, queues, worker stacks, decode/network scratch |
| Storage activity | Read/write bandwidth and synchronization boundary |
| Transition modes | Replace, cover/snapshot, or overlap |
| Degradation | Defined behavior if the preferred allocation is unavailable |

Estimates are admission inputs. Only target measurements become budgets.

## Transition admission

`RootViewController` asks `DisplayResourceCoordinator` to admit every route
change before building the destination tree.

### Replace before build

```text
outgoing will disappear
→ opaque cover if visually required
→ outgoing unloads and returns leases
→ incoming loads
→ cover reveals incoming
```

This is the default when two live trees exceed the transition allowance. It
supports a visible transition without simultaneous full scene trees.

### Snapshot then replace

Use a bounded surface snapshot when motion requires outgoing pixels but not
outgoing interactivity. The snapshot has an explicit byte count and lease.

### Live overlap

Allow both trees only when the specific transition has measured LVGL and heap
headroom. The duration, peak, and input owner are declared. “Transitions may
need it” is not sufficient admission.

### Recovery reserve

The LVGL arena retains enough capacity for a minimal recovery controller or
overlay. An allocation failure must not consume the bytes needed to explain or
recover from the failure.

## Initial envelope worksheet

Numeric entries marked `MEASURE` are intentionally not guessed.

| Controller / transition | LVGL steady | LVGL load peak | Pixel lease | Storage/network activity | Default transition |
|---|---:|---:|---|---|---|
| `LoginViewController` | MEASURE | MEASURE | none | roster/auth service | replace or covered replace |
| `PinEntryViewController` | MEASURE | MEASURE | none | auth request only | covered replace; PIN tree unloads on exit |
| `InboxViewController`, idle | MEASURE | MEASURE | none | inbox metadata refresh | covered replace |
| `InboxViewController`, playing | MEASURE | MEASURE | 1×120×120 RGB565 | bounded audio read/download + sketch stream | same-controller state update |
| Inbox reconstruction | current Inbox + MEASURE | MEASURE | optional second 120×120 temporary surface | storage read | no route change |
| `RecipientPickerViewController` | MEASURE | MEASURE | none | recipient model | covered replace |
| `SketchViewController` | MEASURE | MEASURE | 1×240×240 RGB565 | audio/strokes append; bounded live upload | covered replace or measured wipe |
| `SettingsViewController` | MEASURE | MEASURE | none | settings persistence | covered replace |
| `SendFeedbackViewController` | MEASURE | MEASURE | none | upload service continues independently | covered replace |
| Low-memory recovery controller | MEASURE | MEASURE | none | log only | reserved |

## Measurement program

### Product beats

Capture the same-boot minimum free, largest free block, and LVGL monitor values
for:

1. before storage, microphone, display/LVGL, NVS, UI, and Wi-Fi;
2. initial controller steady state;
3. every controller load start, load peak, appeared state, unload, and recovered
   state;
4. PIN transition loops to expose LVGL fragmentation;
5. Inbox idle, active playback, pause, selection change, and reconstruction;
6. first and repeated sketch-surface acquisition;
7. simultaneous recording, storage write, and streaming;
8. upload of one message while the next recording and sketch are active;
9. server outage, bounded queue saturation, recovery, and drain;
10. every transition mode at its intended peak.

### Required probes

- `lv_mem_monitor`: total, free, largest free, used percentage, and maximum used;
- capability-specific current free and minimum free for internal, DMA-capable
  internal, and PSRAM;
- capability-specific largest free blocks;
- returned pointer classification for every large allocation;
- surface lease owner, bytes, format, region, acquisition, and release;
- storage queue depth, write latency, and synchronization latency;
- upload outstanding chunk count and bytes;
- task stack high-water marks;
- controller load/unload duration and transition frame timing.

The 50 ms LAB-16 sampling approach may be reused, but known shorter allocations
must also have event probes around allocation and release.

## Acceptance constraints

- Inbox pixel memory is bounded independently of inbox message count.
- A 64-message inbox does not imply 64 framebuffers or 64 live card trees.
- Leaving sketch releases the 240×240 surface after required visual references
  quiesce.
- A complete audio recording never exists as one RAM buffer.
- Network failure does not cause unbounded RAM growth.
- Upload can continue after its feedback view unloads.
- Repeated controller transitions return LVGL memory to the expected envelope
  without declining largest-block trends beyond the accepted fragmentation
  margin.
- Transition admission fails before partial destination presentation.
- The recovery presentation remains constructible at the worst admitted peak.
- In-card sketch playback, pause, selection change, return, and completion meet
  the experience requirements on target hardware.

## Open decisions

| ID | Decision | Evidence needed |
|---|---|---|
| `MEM-OPEN-001` | Final LVGL pool size | Per-controller and transition high-water plus fragmentation loops |
| `MEM-OPEN-002` | Active/transition/recovery logical reservation sizes | Worst approved transition measurements |
| `MEM-OPEN-003` | Surface arena region and fallback | Internal vs PSRAM interaction latency and system-margin comparison |
| `MEM-OPEN-004` | Lazy vs boot-time arena allocation | Fragmentation and startup/Wi-Fi/audio comparisons |
| `MEM-OPEN-005` | Inbox renderer: direct single surface vs bounded scratch | Rendering correctness, tearing, and frame timing |
| `MEM-OPEN-006` | Checkpoint format and frequency | Reconstruction latency, encoded size, and storage-write cost |
| `MEM-OPEN-007` | Capture and upload chunk sizes/count | Storage latency, network jitter, retry behavior, and memory peak |
| `MEM-OPEN-008` | Storage quota and full-storage policy | Product retention and failure behavior |
| `MEM-OPEN-009` | Which transitions justify live overlap | Visual approval and measured peak |
| `MEM-OPEN-010` | Which Wi-Fi/HTTP allocations can be forced or configured into PSRAM | Product heap tracing and ESP-IDF/API constraints |

## Explicit non-goals

- One framebuffer per inbox message or per carousel card
- Keeping paused card pixels permanently in RAM
- Treating PSRAM as suitable for interactive work without target testing
- Combining LVGL object memory and pixel-surface memory
- Using storage as synchronous work on the LVGL task
- Changing Wi-Fi allocator configuration without attribution and regression
  measurements
- Treating LAB-16 free memory as the product firmware's available budget

## Related documents

- [`device-ui-shell-architecture-proposal.md`](device-ui-shell-architecture-proposal.md)
- [`ui-memory-lifecycle-review-2026-10-25.md`](ui-memory-lifecycle-review-2026-10-25.md)
- [`overlap-record-during-upload.md`](../../labs/evidence/lab-16/results/overlap-record-during-upload.md)
- [`FOUNDATION-LVGL-BUDGET.md`](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md)
- [`device-target-invariants.md`](../../standards/device-target-invariants.md)
- [`device-presentation-invariants.md`](../../standards/device-presentation-invariants.md)

## Readiness

This document is ready for owner review of resource ownership, allocation
policy, in-card playback, and the measurement program. It is not ready for
implementation until the affected experience/state-preservation details are
traced and the `MEM-OPEN-*` items needed by the first implementation package
are resolved.

