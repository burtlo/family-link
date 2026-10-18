# LAB-10 — Record canvas and media budget

L10-01 establishes the evidence baseline for a local record-canvas experiment.
L10-02 adds the ESP-IDF/LVGL UI project described below. L10-03 adds a
LVGL-free bounded capture state machine behind the UI routing hook. L10-04 adds
the bounded rendering and UI-presentation pipeline described below. L10-05 adds
the standalone bounded host audio-concurrency model described in this README.
L10-06 adds the manual and deterministic human-like workload suite described
below. L10-07 adds the bounded host observation parser and metric aggregator
under [`tools/aggregate_metrics.py`](tools/aggregate_metrics.py).
The package still does not contain a server, send flow,
recipient flow, inbox flow, firmware audio/DAC integration, or product
networking.

The authoritative machine-readable contract is
[`manifest.lab.json`](manifest.lab.json). The evidence report starter is
[`docs/labs/evidence/lab-10/TEMPLATE-REPORT.md`](../../../docs/labs/evidence/lab-10/TEMPLATE-REPORT.md).
The package-local trace contract is
[`trace-schema.v1.json`](trace-schema.v1.json), identified as
`lab10-record-trace/v1` in the manifest.
Workload metadata uses `lab10-workload/v1`; the deterministic source is
`synthetic-human-like-v1` in
[`components/lab10_workload/`](components/lab10_workload/). The generator is
compiled for toolchain coverage but is not executed by the firmware app. The
`CONFIG_LAB10_SCRIPTED` firmware profile has a separate bounded UI smoke path:
after entry reaches recording, the LVGL timer/UI task injects one DOWN followed
by MOVE events on absolute 44 ms schedule slots for the bounded 180-second
capture. It keeps one white stroke active throughout, does not synthesize
palette changes or touch-up events, and skips missed schedule slots rather than
catching up with a burst. The schedule can emit at most 4,090 MOVEs plus the
initial DOWN (4,091 accepted points), below the 4,096-point capture cap; the
capture duration remains the terminal condition. The active stroke closes when
capture reaches its duration cap. Events use the same routing, capture, render,
and canvas publication hooks as touch input, and are explicitly not
hardware-touch evidence.
L10-08 registers the package through the fixed `lab10` adapter key and adds the
bounded parser at [`tools/parse_lab_trace.py`](tools/parse_lab_trace.py). The
adapter owns lifecycle markers and evidence-file placement; L10-07 remains the
owner of metric aggregation and typed field records.

The LVGL canvas buffer is allocated from the configured heap capabilities with
8-bit PSRAM attempted first and internal 8-bit memory as a bounded fallback.
It is not allocated from LVGL's internal pool; the firmware logs the selected
region, byte count, and alignment before publishing pixels.

## L10-06 realistic workload suite

Manual and deterministic workloads are different evidence classes. A manual
trace is identified with a stable `manual-*` identity, uses the device's
monotonic timestamp, and can support human touch/visual behavior only when a
real kit and the required synchronized capture are present. A deterministic
trace is identified with a stable `synthetic-*-v1` identity, records a nonzero
seed and `lab10-workload/v1` metadata, and is host evidence. Synthetic output
must never be labeled hardware touch latency, display scanout, audio
continuity, or task scheduling.

### Manual human workload instructions

Before each case, record the stable identity in the run manifest, start from a
clean canvas, keep the six-color rail visible, and use an external synchronized
screen/audio recording for any scanout or audio claim. Do not replace a manual
case with a replay or synthetic timestamp.

| Identity | Human procedure and required observations |
|---|---|
| `manual-no-stroke-audio-control` | Start the local audio control with no canvas touch for the full interval. Confirm that the rail is not touched and that audio evidence is reported separately from UI responsiveness. |
| `manual-sparse-variable-speed` | Make three separate short strokes: one slow, one comfortable/medium, and one fast. Leave visible pen-up gaps between them; include one short pause while touching. |
| `manual-continuous-pauses-corners` | Draw one continuous stroke through at least three direction changes/corners. Vary speed within the stroke and hold still for two observable pauses without lifting. |
| `manual-dense-multicolor` | Fill a region with several overlapping strokes, alternating slow and fast passes. Use at least four strokes and keep the strokes separate with pen-up gaps. |
| `manual-background-erase` | Draw a colored mark, select the black/background swatch, then retrace part of it and add a separate black stroke. Record this as background-colored erase behavior, not as an eraser control. |
| `manual-edge-corner-leave-reenter` | Touch points adjacent to or exactly at `(0,0)`, `(239,0)`, `(239,239)`, and `(0,239)` without touching the rail. Leave the canvas during a stroke, lift/release, then re-enter with a new DOWN and verify a new stroke identity. |
| `manual-rapid-color-switch` | Between short strokes, select red, blue, green, gold, and background in quick succession. Verify each color applies only to the following stroke and rail touches never append ink. |
| `manual-stop-during-stroke` | Begin a visible stroke, press the stop control while the finger remains down, and release afterward. Expect one `stopped` terminal outcome and no late point mutation. |
| `manual-cancel-during-audio` | Start audio, draw a stroke, and cancel while audio is active. Expect one `cancelled` outcome, cleaned resources, and separate audio evidence. |
| `manual-mic-failure` / `manual-codec-failure` / `manual-allocation-failure` | Run the applicable injected or device failure before and during a stroke. Preserve the raw trace and failure reason, verify cleanup and terminal uniqueness, and mark microphone/codec/heap behavior `UNMEASURED` or `BLOCKED` when the required hardware evidence is absent. |

The sparse, continuous, and dense cases are intentionally distinct: sparse
tests low event density and spacing; continuous tests velocity changes, pauses,
corners, and one active stroke; dense tests bounded pressure with many points
and multiple strokes. Every manual case that uses a finger or stylus retains
raw/display/logical coordinates, stroke and color boundaries, pen-up gaps, and
the actual device timestamps. A manual failure run is not a passing experiment
merely because the workflow reached a terminal screen.

### Deterministic synthetic workload identities

The L10-06 component exposes eight stable identities. Each call takes a
nonzero `uint64_t` seed and writes a fixed-capacity trace without allocating or
reading a clock; the same identity/configuration/seed produces the same bytes.

| Identity | Profile / terminal behavior |
|---|---|
| `synthetic-sparse-multicolor-v1` | Low point density, varied spacing and speed, multiple short strokes; completed. |
| `synthetic-continuous-multicolor-v1` | Longer active strokes with corners and pauses; completed. |
| `synthetic-dense-multicolor-v1` | Highest bounded point density and render-pressure input; completed. |
| `synthetic-edge-corner-v1` | Edge-adjacent and corner coordinates plus leave/re-enter; completed. |
| `synthetic-rapid-color-switch-v1` | Palette changes between short strokes, including background; completed. |
| `synthetic-background-erase-v1` | Colored marks followed by black/background strokes; completed. |
| `synthetic-stop-during-stroke-v1` | Deterministic partial second stroke followed by one `stopped` terminal event. |
| `synthetic-cancel-during-audio-v1` | Deterministic partial second stroke followed by one `cancelled` terminal event; audio remains a separate model/evidence concern. |

The generated trace metadata includes `workload_identity`,
`workload_schema_version=lab10-workload/v1`,
`generator_version=synthetic-human-like-v1`, `seed`,
`timestamp_jitter_bound_us=2000`, actual jitter, `evidence_provenance=host`,
feature flags, and bounded counts. Each event retains raw, display, and
logical coordinates; phase, stroke ID, color index/RGB, boundary, ideal time,
actual time, jitter, pause, pen-up gap, and acceptance. Raw and display
coordinates are equal for this host source because no hardware transform is
being claimed; this is not a touch-latency measurement.

The generator uses non-repeating waypoint paths with corners, random-but-seeded
point perturbation, variable point spacing, variable inter-point velocity,
explicit pauses, color changes, palette boundaries, an actual leave/re-enter
boundary, edge points, and bounded timestamp jitter. The primary fixture is
not a set of repeated identical straight lines. Bounds are 512 events, 320
accepted points, 180,000,000 µs duration, and a calculated populated-trace
size no larger than the fixed trace object. Configuration rejects zero or
oversized capacities, zero seeds, durations outside the experiment cap, and a
jitter declaration other than the manifest's ±2,000 µs bound.

The C representation is the bounded source for the JSON trace contract. A
synthetic host trace must carry `workload_metadata.source=synthetic`,
`evidence_provenance=host`, a nonzero seed, and the declared feature list. A
manual trace must carry `source=manual`, a stable `manual-*` identity,
`seed=null`, `generator_version=not_applicable`, and a zero synthetic-jitter
bound. Missing or unsynchronized hardware prerequisites remain
`UNMEASURED`/`BLOCKED`; they are never inferred from generated timestamps.

## L10-05 bounded audio-concurrency experiment

`components/lab10_audio/` is a deterministic, LVGL-free host model. It has two
independent bounded queues: a PCM frame ring and a drawing-event queue. Drawing
events are an observation boundary for L10-03 accepted points or L10-04
published work; the model does not submit touch reports, own the capture FIFO,
change renderer budgets, or retain capture/renderer pointers. This exercises
concurrency pressure and terminal cleanup without changing L10-03/L10-04
ownership.

The default model inputs are explicit experiment assumptions, not hardware
facts: 256 bytes per PCM frame, 128 samples per frame, 8,000 µs per frame, an
8-frame PCM capacity, a 16-event drawing capacity, 256 transient PCM bytes,
4,096 retained sketch bytes, and two 28,800-byte framebuffer budgets. Callers
may provide smaller bounded values for fault tests. A PCM allocation is the
only model allocation; drawing events and metrics use fixed storage bounded by
the configured maxima.

The stable scenario identities and expected outcomes are:

| Identity | Expected outcome |
|---|---|
| `no-stroke-control` | `completed-audio-continuous` |
| `sparse-drawing` | `completed-audio-continuous` |
| `continuous-drawing` | `completed-audio-continuous` |
| `dense-drawing` | `completed-with-drawing-pressure` |
| `rapid-color-switching` | `completed-audio-continuous` |
| `stop-during-audio` | `stopped-and-clean` |
| `cancel-during-audio` | `cancelled-and-clean` |
| `audio-capture-failure` | `capture-failed-and-clean` |
| `allocation-failure` | `allocation-failed-and-clean` |

Audio continuity is reported independently from drawing queue depth and drawing
latency. The model records PCM occupancy/high-water in frames and bytes,
overruns/underruns and underrun duration, produced/consumed frame and sample
counts, audio errors, drawing event/queue occupancy/high-water/overflow, color
and stroke changes, p50/p95/p99/max drawing latency, retained/transient
PCM/sketch/framebuffer bytes, allocation bounds, cleanup time, live resources,
and stale-worker session rejections. Dense drawing is expected to create
drawing pressure while keeping model audio balanced; UI responsiveness is not
used as an audio-health proxy.

Every `lab10_audio_start` returns a nonzero session token. The token is
required by all producer, consumer, drawing, terminal, metrics, and result
operations. Tokens are monotonically issued per model and never reused; an old
worker token is rejected and counted without changing the current queues or
counters. Terminal cleanup receives a terminal timestamp and a post-cleanup
timestamp from the same deterministic monotonic host schedule. The model
records both and derives `cleanup_time_us = cleanup_timestamp_us -
terminal_timestamp_us`; zero timestamps are valid schedule values, while an
allocation failure before the schedule records no timestamps and reports zero
cleanup duration.

Each result carries `provenance=host` and a required missing/unknown reason:
`host bounded model only; microphone, codec, DAC, speaker, DMA, and task
scheduling were not exercised`. The optional `audio_concurrency` section in
[`trace-schema.v1.json`](trace-schema.v1.json) requires a scenario identity,
expected and observed outcomes, per-metric `{value, unit, provenance,
sample_count, missing_reason}` records, resource-release status, and an explicit
`hardware_audio_health_measured=false`. Host continuity is therefore only
`model-continuous`/`model-overrun`/`model-underrun`; it never claims DAC or
audible continuity.

There is no narrow firmware integration in L10-05: the current app has a
display/touch contract but no verified microphone/codec/DMA device contract.
The component is included in the ESP-IDF dependency graph only so the same
production C11 source is compiled by the firmware toolchain; the app does not
call it or integrate microphone/codec/DAC/DMA hardware. Hardware audio/DAC
continuity, actual task/heap/PSRAM pressure, and synchronized screen/audio evidence remain
`UNMEASURED/BLOCKED` until a separate device contract and kit run exist.

### L10-05 concurrency ownership contract

The model serializes all mutable state with one C11 `atomic_flag` spin lock.
The lock is portable to the native host and ESP-IDF component build and has no
pthread or test-only dependency. Every public start/restart, producer,
consumer, drawing submit/process, terminal stop/cancel/fail/complete, and
metrics/result read takes the lock before session-token validation and before
touching queues, terminal state, timestamps, allocations, or counters. The
terminal transition and cleanup are one-shot under that same lock, so exactly
one concurrent terminal call can publish an outcome; duplicate calls are
rejected. A stale or unknown token is rejected under the lock before any
current-session state mutation, including after a restart.

The queues and drawing-latency storage remain fixed and bounded by the
configured maxima; synchronization does not add a queue or block a producer on
capacity. The lock is non-recursive, and internal helpers are called only by
their already-locked public entry point. Model lifetime is a separate caller
responsibility: `lab10_audio_destroy` must not race with any API call, and the
caller must stop and join all worker threads before destroy. The threaded host
regression in `tests/test_lab10_audio.c` covers concurrent producer, consumer,
drawing, terminal, metrics/result, valid-token, stale-token, restart, bounded
queue, cleanup, and duplicate-terminal behavior. TSan runs that same pthread
test; it is host synchronization evidence only and makes no hardware audio
claim.

## Fixed display and coordinate contract

The target LCD is exactly 320×240 pixels with half-open display coordinates
`x ∈ [0,320)` and `y ∈ [0,240)`.

| Region | Geometry | Boundary and behavior |
|---|---:|---|
| Canvas | origin `(0,0)`, 240×240 | `0 ≤ x < 240`, `0 ≤ y < 240`; black `#000000`; 8 px corner radius where supported |
| Palette rail | origin `(240,0)`, 80×240 | `240 ≤ x < 320`, `0 ≤ y < 240`; surface `#101418`; never appends canvas ink |
| Logical storage | 120×120 | `floor(display/2)`, clamped to `0…119` on each axis |

The canvas/rail split is an exact hit boundary: `x=239` is canvas and `x=240`
is rail. Touches outside the LCD are rejected, not clamped into the canvas.
Raw touch reports remain separate from accepted points; accepted points retain
raw/display coordinates in trace evidence and store logical coordinates plus
per-stroke color.

### Palette hit regions

Each target is a 44 px diameter circle (`r=22`). A rail touch selects a swatch
only when `(x-cx)^2 + (y-cy)^2 ≤ 22^2`; its inclusive bounding box is shown for
inspection. A touch inside the rail but outside all six circles is
`rail_rejected`.

| Index | Role / color | Center `(cx,cy)` | Inclusive hit bounds `(x0,y0,x1,y1)` |
|---:|---|---:|---|
| 0 | background / erase behavior `#000000` | `(269,208)` | `(247,186,291,230)` |
| 1 | default white `#F8F8F8` | `(291,172)` | `(269,150,313,194)` |
| 2 | gold `#E8C040` | `(269,136)` | `(247,114,291,158)` |
| 3 | green `#4CAF50` | `(291,100)` | `(269,78,313,122)` |
| 4 | red `#E53935` | `(269,64)` | `(247,42,291,86)` |
| 5 | blue `#5AA0E8` | `(291,28)` | `(269,6,313,50)` |

Centers use a 36 px vertical pitch, alternate ±11 px around the rail center
`x=280`, and leave a 10 px bottom margin. There is no eraser control; black is
the background-colored stroke. Color changes apply only to subsequent points.

## Capture bounds and trace rules

These are explicit LAB-10 experiment bounds, not approved product budgets:

- Maximum capture duration: **180,000 ms (180 s)**.
- Maximum accepted canvas points: **4,096**. The 4,096th accepted point ends the
  capture with `point_cap`; no point is silently truncated.
- One FIFO of **16 immutable raw touch reports**, with one capture owner. A full
  queue ends the operation with `queue_overflow`, records the loss, and never
  overwrites or silently drops an entry.
- A MOVE may be coalesced only when it is consecutive, belongs to the same
  stroke and color, maps to the same logical coordinate, and is less than 2 px
  Euclidean display distance from the last accepted point. DOWN, UP, color
  changes, logical-coordinate changes, stroke boundaries, second-touch rejects,
  and all capacity outcomes are never coalesced.
- Hardware timestamps use one monotonic device clock in microseconds. Wall-clock
  time is not valid for ordering, duration, or latency. Host-generated traces use
  a host monotonic clock, declare a ±2,000 µs timestamp-jitter bound, and remain
  separate evidence.

DOWN starts one stroke; accepted MOVE points retain stroke identity and color;
UP closes the stroke. A rail touch can select a color but can never become canvas
ink. A second touch is rejected explicitly. Leaving the canvas closes the
stroke; a later DOWN begins a new stroke. Stop, cancel, duration cap, point cap,
queue overflow, allocation failure, and normal completion each produce at most
one terminal event.

The implementation lives in
[`components/lab10_capture/`](components/lab10_capture/). The UI callback only
copies the routed report into the non-blocking 16-entry FIFO; the UI poll hook
owns state-machine consumption. Accepted points are retained separately in a
4,096-entry bounded result arena with raw display coordinates, 120×120 logical
coordinates, timestamp, color, phase, stroke ID/boundary, and accepted
outcome. A nonzero 64-bit session generation guards producer, consumer, and
result-release calls so a late callback cannot mutate a newer capture. A queue
overflow request is
finalized before duration handling or an explicit stop/cancel/complete
operation. The FIFO is cleared on every terminal outcome; the immutable point
result remains available until the later renderer releases it or the next
session replaces it. After release, snapshot and duplicate release calls fail
explicitly, so no released result can report a nonzero point count with a null
arena. The session identity is `lab10_capture_session_id_t`, an unsigned
64-bit generation. Zero is invalid and each generation is permanently
consumed; generation exhaustion fails closed rather than reusing an older
identity.

Producer admission uses an atomic `ACTIVE -> TERMINATING -> TERMINAL`
lifecycle, a full-width generation-tagged admission gate, an in-flight producer
count, and a one-shot overflow latch. The all-ones 64-bit gate value is only a
transient claim marker; it is never issued as a session generation. The gate is
claimed before the count is incremented; terminalization closes that gate before
waiting for admitted producers, and session reset never resets the count. This
makes a submit that was paused before admission fail its old generation check
after a new session starts, without allowing it to write the new FIFO. A submit
admitted before terminalization completes its bounded FIFO critical section
before cleanup clears the queue; a submit that observes termination is rejected.
If an admitted submit observes a full FIFO and latches overflow before terminal
publication, `queue_overflow` wins over stop, cancel, complete, and duration.
The producer never waits; only the consumer-side terminal path may wait for the
bounded admission reservation and in-flight section.

Later instrumentation can register the consumer-side raw-report observer before
start; it receives each queued report, including reports that are subsequently
rejected or coalesced, and must copy it into its own bounded sink. The observer
never runs in the UI producer callback.

## L10-04 bounded rendering and presentation

The renderer is a separate LVGL-free component at
[`components/lab10_render/`](components/lab10_render/). It borrows the
session-guarded accepted-point result from L10-03 and never submits touch
reports or takes ownership of the capture FIFO. The logical framebuffer is
120×120 indexed color; the ESP-IDF adapter presents each logical pixel as a
bounded 2×2 display block on the 240×240 canvas. Background-colored points are
written as `LAB10_COLOR_BACKGROUND`, so black remains an observable erase
behavior. Stroke ID, phase boundary, and color are read from each accepted
point; a segment is never interpolated across a stroke boundary or color
change.

Four modes are explicit and measurable:

| Mode | Role | Semantics per UI update |
|---|---|---|
| `direct_point` | labeled control | Consume at most one accepted point and publish one logical pixel; no interpolation. |
| `segment` | device default | Consume at most one point and rasterize the same-stroke/same-color Bresenham segment in bounded pixel steps. |
| `bounded_batch` | candidate | Use the same segment semantics with independent point and raster budgets; the default batch limit is 8 points. |
| `whole_frame` | candidate | Build a second logical framebuffer through bounded clear/replay steps, then publish at most the publication budget per update. |

The default raster and publication budgets are 32 logical pixels per update;
the hard contract maxima are 512 each and 64 source points. Incremental modes
use the lower of their raster and publication budgets, so a render update
cannot enqueue more visible work than it can publish. Whole-frame mode has
three observable phases (`clear`, `replay`, `publish`) and never performs a
4096-point history replay or a 120×120 publication in one UI-task update.
The bounded 2×2 LVGL expansion is a fixed adapter factor of four physical
pixel writes per published logical pixel.

The ESP-IDF bridge selects the mode at build time with the package-local
`LAB10_RENDER_MODE` Kconfig choice (`DIRECT_POINT`, `SEGMENT`,
`BOUNDED_BATCH`, or `WHOLE_FRAME`) and uses bounded Kconfig budgets for source
points, raster operations, and publication operations. The normal manual and
scripted firmware default is `SEGMENT`, so accepted points in the same active
stroke are connected. `DIRECT_POINT` remains available as an explicitly
labeled control build; it intentionally renders isolated point pixels.

Every update exposes source backlog, capture FIFO depth/high-water, points
processed, raster operations, publication operations, frame generation,
invalidation count/rectangle, presentation-pending state, and the last
UI-task lock observation through `lab10_render_metrics_t` and callbacks. The
LVGL adapter reports `ui_task_owned`; it does not measure or claim
`bsp_display_lock` acquisition time. Whole-frame publication is chunked and
invalidates only the bounded published rectangle. A full-screen invalidation
counter is retained for evidence and must not be interpreted as display
scanout.

The control/candidate comparison is a host-verifiable deterministic renderer
comparison. Adjacent-point workloads can be compared for pixel equivalence;
`direct_point` is intentionally point-only for sparse gaps, so a sparse
direct-vs-segment difference is a mode semantic, not a capture loss. Host
tests prove bounds, backlog drain, color/stroke handling, invalidation and
publication hooks, allocation/error cleanup, and mode behavior. Visual
fidelity, touch-to-ink latency, LVGL invalidation/lock timing, scanout, and
hardware memory/task pressure remain hardware evidence and are
`unmeasured/blocked` until a synchronized kit run exists.

### Current LVGL touch-adapter limitation

The current LVGL adapter is explicitly single-touch. Canvas, rail, and palette
forwarding all emit `touch_id=0`; this is an adapter limitation, not evidence of
multi-touch hardware support. Native capture tests inject nonzero touch IDs to
exercise touch identity and second-touch rejection. Multi-touch hardware
behavior and evidence remain **unmeasured/unavailable**, and this package makes
no touch-latency claim.

## Workloads

The complete L10-06 manual procedures, stable identities, generator contract,
feature coverage, bounds, and evidence boundary are in the earlier
[`L10-06 realistic workload suite`](#l10-06-realistic-workload-suite) section.
Keep manual device timestamps and synthetic host timestamps in separate runs;
do not merge their latency distributions. The synthetic source is suitable for
models, bounds, parsers, generator determinism, aggregation, and deterministic
render pressure only.

## Metrics and evidence

Every metric record uses `{value, unit, provenance, sample_count,
missing_reason}` and provenance is one of `hardware`, `host`, `derived_static`,
or `unmeasured/blocked`. Where samples exist, timing/resource distributions
report p50, p95, p99, and max; missing samples stay missing. An
`unmeasured/blocked` record must be `value=null`, `sample_count=0`, and carry a
non-empty `missing_reason`; supplied numeric values or samples are rejected.
The same rule applies to hardware-only fields when their claimed provenance is
`host` or `derived_static`, so host output cannot turn unavailable hardware
evidence into a zero or pass.

### L10-07 bounded observation aggregation

[`tools/aggregate_metrics.py`](tools/aggregate_metrics.py) consumes the
dependency-free `lab10-metrics-observation/v1` document and emits one typed
record for every manifest field, in manifest order. It accepts explicit field
observations from the capture, render, audio, and workload boundaries, and it
can derive only facts represented by a validated `lab10-record-trace/v1`
trace. Unknown keys, duplicate JSON keys, malformed values, non-monotonic or
post-terminal events, mixed provenance within one metric, and over-limit input
fail closed.

The input `fields` object uses declared scalar fields such as
`raw_report_count: {"value": 12, "unit": "count"}` and declared sample
series such as `ui_update_us: {"samples": [31, 29], "unit": "us"}`. A sample
series is the only accepted source for its four percentile outputs. The
aggregator sorts finite samples and uses nearest-rank `ceil(p*n/100)` semantics;
there is no interpolation and an empty series produces `value=null`,
`sample_count=0`, and `missing_reason`, never zero. Exact units are declared
in `manifest.lab.json#/metric_schema/field_units`, and the output record order
is `value`, `unit`, `provenance`, `sample_count`, `missing_reason`.

Raw reports and accepted points retain raw, display, and logical coordinates;
color changes, stroke boundaries, pen-up boundaries, terminal cap outcomes,
queue overflow, trace-loss events, render budgets, and audio occupancy/
continuity observations remain in the `preserved` evidence output and are not
collapsed into a replacement field. Workload identity, schema, seed, generator
version, and provenance remain preserved. Repeated runs accept
paired `memory_before_bytes`/`memory_after_bytes` snapshots and report the
signed first-baseline-to-final-terminal `memory_drift_bytes` and
`repeated_run_memory_drift_bytes`; the last supplied repeated-run cleanup and
live-resource observations remain the current `cleanup_time_us` and
`live_resources_after_terminal` records.

CPU/task busy/load/stack and internal-heap/PSRAM fields are hardware-only when
not explicitly measured by a device probe. On a host-only document they emit
`provenance=unmeasured/blocked` with the manifest's explicit hardware-only
reason. Host model measurements remain `host` and cannot be promoted to device
task, heap, PSRAM, stack, touch-latency, display-scanout, or audible-audio
claims.

The bounded host command is:

```text
python3 client/labs/lab-10-record-canvas/tools/aggregate_metrics.py \
  <observation.json> <metrics.json>
```

The exact metric field inventory below is the same ordered grouping as
`manifest.lab.json` → `metric_schema.fields`; field names must not be shortened
or substituted in a run's parsed metrics.

### `input`

```text
raw_report_count
accepted_point_count
coalesced_move_count
rejected_report_count
rejected_rail_touch_count
rejected_outside_display_count
rejected_second_touch_count
duplicate_point_count
edge_point_count
stroke_count
color_change_count
palette_hit_count
queue_depth_current
queue_depth_high_water
queue_overflow_count
point_cap_hit_count
duration_cap_hit_count
terminal_event_count
terminal_outcome
```

### `latency_and_ui`

```text
touch_to_ink_us_p50
touch_to_ink_us_p95
touch_to_ink_us_p99
touch_to_ink_us_max
accepted_to_present_us_p50
accepted_to_present_us_p95
accepted_to_present_us_p99
accepted_to_present_us_max
ui_update_us_p50
ui_update_us_p95
ui_update_us_p99
ui_update_us_max
invalidation_us_p50
invalidation_us_p95
invalidation_us_p99
invalidation_us_max
invalidation_count
full_screen_invalidation_count
logical_display_mapping_mismatch_count
replay_2x_geometry_mismatch_count
```

### `rendering`

```text
render_mode
render_role
render_update_count
render_pending_point_count
render_backlog_high_water
render_raster_ops_last_update
render_raster_ops_high_water
render_publication_ops_last_update
render_publication_ops_high_water
render_invalidation_count
render_full_screen_invalidation_count
render_frame_generation
render_frame_phase
render_presentation_pending
render_lock_state
render_stale_session_count
render_cleanup_count
```

### `cpu_and_tasks`

```text
capture_worker_busy_us
capture_worker_load_percent
capture_worker_stack_high_water_bytes
ui_task_busy_us
ui_task_load_percent
ui_task_stack_high_water_bytes
audio_task_busy_us
audio_task_load_percent
audio_task_stack_high_water_bytes
```

### `audio`

```text
audio_continuity_status
audio_buffer_occupancy_bytes
audio_buffer_high_water_bytes
audio_buffer_occupancy_frames
audio_buffer_high_water_frames
audio_overrun_count
audio_underrun_count
audio_underrun_duration_us
audio_frame_count_produced
audio_frame_count_consumed
audio_sample_count_produced
audio_sample_count_consumed
audio_error_count
retained_pcm_bytes
transient_pcm_bytes
```

### `drawing_and_concurrency`

```text
drawing_event_count
drawing_queue_depth
drawing_queue_high_water
drawing_queue_overflow_count
drawing_color_change_count
drawing_stroke_count
drawing_latency_us_p50
drawing_latency_us_p95
drawing_latency_us_p99
drawing_latency_us_max
stale_worker_mutation_count
```

### `memory`

```text
retained_sketch_bytes
transient_sketch_bytes
retained_framebuffer_bytes
transient_framebuffer_bytes
largest_allocation_bytes
allocation_attempt_count
allocation_failure_count
internal_free_bytes
internal_min_free_bytes
internal_largest_free_block_bytes
psram_free_bytes
psram_min_free_bytes
psram_largest_free_block_bytes
memory_drift_bytes
```

### `storage_and_format`

```text
final_format_bytes
final_format_version
final_logical_width
final_logical_height
final_stroke_count
final_color_change_count
pen_up_boundary_count
raw_trace_bytes
trace_loss_count
```

### `cleanup_and_repetition`

```text
cleanup_time_us
stop_cleanup_us_p50
stop_cleanup_us_p95
stop_cleanup_us_p99
stop_cleanup_us_max
cancel_cleanup_us_p50
cancel_cleanup_us_p95
cancel_cleanup_us_p99
cancel_cleanup_us_max
live_resources_after_terminal
repeated_run_count
repeated_run_memory_drift_bytes
```

Raw traces, parsed metrics, workload identity/schema, build/configuration IDs,
and the 2× replay comparison belong under the run evidence directory. Audio or
scanout claims additionally require an external synchronized screen/audio
recording. Trace loss or a missing required artifact makes the affected result
incomplete; it is never treated as zero.

Hardware is required for visual fidelity, touch-to-ink latency, LCD scanout,
LVGL invalidation/lock behavior, task load, audio continuity, actual heap/PSRAM/
stack pressure, and synchronized screen/audio conclusions. LAB-03 is
playback-only evidence and is not a LAB-10 capture lab; its playback traces do
not satisfy these capture claims.

## Exclusions

This package deliberately excludes server fixtures, send/upload, recipient
selection, inbox integration, durable outbox/persistence, product networking,
and historical-v1 archive edits or build dependencies. It also excludes a
recording timer or on-screen recording indicator from the steady-state canvas.
No host-only result may be promoted to a hardware or product-performance claim.

## L10-02/L10-03/L10-04/L10-05 firmware and native checks

The ESP-IDF project is self-contained under this directory and uses the
Espressif ESP-BOX-3 BSP through its component manifest. The UI starts with a
red FAB bloom into the record surface. If the BSP exposes `BSP_BUTTON_MAIN`, a
UI-task-owned ring-wipe entry is registered; the button callback only posts a
flag and never touches LVGL. The steady-state surface contains only the 240×240
canvas and 80×240 palette rail. Recording has no timer or on-screen recording
indicator. Terminal presentation hooks expose stopped, cancelled, failed, and
completed states without adding capture behavior.

The model and geometry checks are host-native and do not claim touch latency,
scanout, LVGL scheduling, audio continuity, or hardware memory pressure:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Iclient/labs/lab-10-record-canvas/components/lab10_ui/include \
  client/labs/lab-10-record-canvas/components/lab10_ui/lab10_ui.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_ui.c \
  -o /tmp/lab10_ui_native_test && /tmp/lab10_ui_native_test
```

The L10-03 capture checks use the same strict warning policy and additionally
cover accepted-point snapshots, coalescing, stroke/color boundaries, rail and
outside rejection, second-touch rejection, timestamp validation, FIFO
overflow, duration and point caps, stale sessions, stop/cancel terminality,
allocation failure, invalid configuration, and the pre-admission
cross-session race. The hook-enabled suite also seeds the 64-bit boundary and
proves that a paused stale producer cannot enter a restarted session, that the
last generation is issued once, and that the next start fails closed instead of
wrapping. The capture suite is run both without and with its compile-time-only
pthread barriers; the boundary seed and all barrier symbols are absent from
production builds:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Iclient/labs/lab-10-record-canvas/components/lab10_ui/include \
  -Iclient/labs/lab-10-record-canvas/components/lab10_capture/include \
  client/labs/lab-10-record-canvas/components/lab10_ui/lab10_ui.c \
  client/labs/lab-10-record-canvas/components/lab10_capture/lab10_capture.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_capture.c \
  -o /tmp/lab10_capture_native_test_no_hooks && /tmp/lab10_capture_native_test_no_hooks
```

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -DLAB10_CAPTURE_TEST_HOOKS -pthread \
  -Iclient/labs/lab-10-record-canvas/components/lab10_ui/include \
  -Iclient/labs/lab-10-record-canvas/components/lab10_capture/include \
  client/labs/lab-10-record-canvas/components/lab10_ui/lab10_ui.c \
  client/labs/lab-10-record-canvas/components/lab10_capture/lab10_capture.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_capture.c \
  -o /tmp/lab10_capture_native_test && /tmp/lab10_capture_native_test
```

The firmware build requires the repository's ESP-IDF environment and a
component-manager resolution for `espressif/esp-box-3`; no historical-v1 source
is included or required at build time. The L10-04 bridge uses the ESP-IDF
project's LVGL canvas widget and the same bounded publication callbacks as the
native renderer; it does not add a server, send, recipient, or networking path.
The package selects the custom `partitions.csv` table exclusively; that file is
the flash layout contract and the built-in single-app-large choice is not
selected.

For a reproducible clean ESP-IDF 5.4.2 build, source the ESP-IDF environment
and use a new build directory (and, when needed, a new `SDKCONFIG` path) before
running `idf.py ... reconfigure` followed by `idf.py ... build`. Inspect the
bootloader CMake cache if the link reports an implausibly large `.iram.text`:
`CMAKE_LINKER`, `CMAKE_AR`, `CMAKE_NM`, `CMAKE_RANLIB`, and `CMAKE_OBJDUMP`
must resolve to the Xtensa tools, not `/usr/bin/*`. A stale bootloader
sub-build cached with host tools reproduced the 9,787-byte overflow; the same
source and resolved sdkconfig in a fresh ESP-IDF toolchain build produces both
bootloader and application images. This is build-environment evidence, not a
renderer-bound waiver or a hardware result.

The L10-04 renderer checks use the same strict warning policy and prove all four
modes, bounded raster/publication work, backlog draining, visible color/stroke
handling, invalidation and lock/publication hooks, and allocator/reset paths:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Iclient/labs/lab-10-record-canvas/components/lab10_ui/include \
  -Iclient/labs/lab-10-record-canvas/components/lab10_capture/include \
  -Iclient/labs/lab-10-record-canvas/components/lab10_render/include \
  client/labs/lab-10-record-canvas/components/lab10_ui/lab10_ui.c \
  client/labs/lab-10-record-canvas/components/lab10_capture/lab10_capture.c \
  client/labs/lab-10-record-canvas/components/lab10_render/lab10_render.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_render.c \
  -o /tmp/lab10_render_native_test && /tmp/lab10_render_native_test
```

The L10-05 native check covers every audio scenario, frame/sample accounting,
occupancy/high-water, overrun/underrun, drawing/audio separation, color and
stroke events, restart-unique stale-worker rejection, timestamp-derived
cleanup, invalid timestamp ordering, failure/cancel cleanup, allocation bounds,
and 100 repeated cycles. It also runs a real pthread-based concurrent
producer/consumer/drawing/terminal/metrics-result regression, including a
restart with stale workers, bounded queue assertions, terminal cleanup, and
single-winner terminal publication. Its allocator and failure injection are
supplied by the test process; there are no production-only test hooks. The
metric inventory comparator is `tests/test_lab10_metric_inventory.py` and
compares the README, manifest, and trace-schema inventories byte-for-byte in
declared order. The L10-07 unit suite additionally covers deterministic
percentile semantics, empty/missing fields, provenance, input/render/audio/
workload derivation, queue/cap/overflow/loss handling, repeated-run drift,
malformed/oversized documents, output bounds, and unit/field parity.

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
  -Iclient/labs/lab-10-record-canvas/components/lab10_audio/include \
  client/labs/lab-10-record-canvas/components/lab10_audio/lab10_audio.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_audio.c \
  -o /tmp/lab10_audio_native_test && /tmp/lab10_audio_native_test
```

Sanitizer runs use the same source/test pair. ThreadSanitizer is a host
race-check of the model boundary only; it is not hardware task evidence:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread -fsanitize=address,undefined -g \
  -Iclient/labs/lab-10-record-canvas/components/lab10_audio/include \
  client/labs/lab-10-record-canvas/components/lab10_audio/lab10_audio.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_audio.c \
  -o /tmp/lab10_audio_native_test_asan_ubsan && /tmp/lab10_audio_native_test_asan_ubsan

cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread -fsanitize=thread -g \
  -Iclient/labs/lab-10-record-canvas/components/lab10_audio/include \
  client/labs/lab-10-record-canvas/components/lab10_audio/lab10_audio.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_audio.c \
  -o /tmp/lab10_audio_native_test_tsan && /tmp/lab10_audio_native_test_tsan
```

The existing UI, capture, and render commands above remain required regression
checks. JSON validation is `jq empty` on the manifest and trace schema, and the
package source is checked with strict whitespace/static scans before evidence is
accepted. The ESP-IDF reconfigure/build remains the L10-04 firmware check; it
does not integrate or validate microphone, codec, DAC, DMA, or audible audio.

### L10-06 workload checks

The workload component and native suite use the same strict warning policy as
the earlier package checks:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Iclient/labs/lab-10-record-canvas/components/lab10_workload/include \
  client/labs/lab-10-record-canvas/components/lab10_workload/lab10_workload.c \
  client/labs/lab-10-record-canvas/tests/test_lab10_workload.c \
  -o /tmp/lab10_workload_native_test && /tmp/lab10_workload_native_test
```

Run the same binary repeatedly with the same seed to check reproducibility;
the test also covers variation across workload identities and seeds, bounded
event/point/duration/trace size, required gesture features, monotonic time and
jitter, raw/display/logical parity, edge/color/stroke/pen-up coverage,
stop/cancel terminal identities, and malformed configuration failures. The
workload component has no capture test-hook macro or production callback path;
the existing capture hook-on and hook-off commands above remain the production
hook-isolation check.

The complete package validation matrix is: strict UI, capture without and with
hooks, render, audio, and workload native suites; audio ASan/UBSan and TSan;
three or more repeated workload runs; `jq empty`/JSON-schema validation;
semantic and whitespace scans; symbol/source inspection for production hook
isolation; the L10-07 bounded parser/unit suite; and ESP-IDF reconfigure/build,
or the narrowest toolchain compile if the configured IDF environment is
unavailable. TSan, ASan, UBSan, repeated
generator output, and all host workload results remain host evidence. They do
not change the required 1-byte IRAM or hardware residual disclosures.

The preserved LAB-10 residual is explicit: the application had **1 byte of
IRAM free after the successful L10-04 link**, and L10-06 does not relax or
remeasure that headroom. Hardware visual/touch/scanout/lock/resource/audio
evidence remains unmeasured unless the required kit and synchronized capture
are available.

## L10-08 runner, parser, and structured evidence

The fixed host adapter identity is `lab10/v1`; the parser identity is
`lab10-record-parser/v1`. Registry data selects these allowlisted keys and does
not name import paths. The two operator modes are deliberately separate:

| Mode | Command | Workload source | Evidence boundary |
|---|---|---|---|
| Manual | `make lab.lab10 KIT=<kit> LAB10_WORKLOAD=manual-<case>` | device monotonic touch trace | hardware only when the kit and synchronized captures are present |
| Deterministic | `make lab.lab10.scripted KIT=<kit> LAB10_WORKLOAD=synthetic-<case>-v1` | seeded `lab10-workload/v1` trace | host synthetic; never hardware latency/scanout/audio evidence |

Immediately before the bounded serial reader starts, the adapter prints a
`CAPTURE ACTIVE` line containing the resolved command, backend, mode, kit, and
remaining seconds. Scripted mode runs one continuous white stroke with a DOWN
and scheduled MOVEs every 44 ms for the full 180-second capture; it makes no
palette transitions and does not emit touch-up between points. Missed schedule
slots are skipped (never burst-replayed), keeping the script to at most 4,091
accepted points. The active stroke is closed by the capture duration cap; the
scripted completion marker is emitted only after that terminal outcome and at
least 3,500 accepted points. Stop, cancel, early termination, point-cap
termination, or failure cannot produce a completion marker. Script-generated
events use the normal UI/capture path but never synthesize hardware-touch
evidence.

Resolved support commands are:

```text
make lab.lab10.help
make lab.lab10.build KIT=<kit> VARIANT=manual
make lab.lab10.scripted.build KIT=<kit>
make lab.lab10.capture KIT=<kit> RUN_ID=<run-id>
make lab.lab10.parse RUN_ID=<run-id>
```

The bounded raw serial artifact is
`logs/device/labs/lab-10/<run-id>/serial.txt` (2 MiB maximum). The shared run
envelope is written to
`docs/labs/evidence/lab-10/runs/<run-id>/` and contains `run-manifest.json`,
`events.jsonl`, and parser output `metrics.json` (4 MiB maximum). The parser
output document is `family-link.lab10-parser-output/v1`; it contains the
L10-07 `metric_schema`, ordered typed `metrics`, preserved trace evidence,
adapter/parser identities, lifecycle markers, diagnostics, workflow status,
experiment verdict, and field-level evidence provenance.

The device/host lifecycle contract is one `LAB10_READY` marker, one
`LAB10_RUN` marker, a bounded scenario sequence with at most one terminal
status per scenario identity, and at most one `LAB10_CANCEL` marker. Scripted
mode additionally requires the device-owned
`LAB10_SCRIPTED_DRAWING status=complete` marker, which records the actual
180-second duration-cap terminal, 3,500–4,095 accepted points, one
uninterrupted white stroke, no palette changes, and `hardware_touch=0`, before
the host closes the scenario as passed; its absence remains
incomplete/unconfigured. An early or stopped run
must never emit that marker; the 4,096th accepted point terminalizes as
`point_cap`, never as a scripted duration-cap completion. A trace
must contain exactly one terminal event; duplicate terminals, post-terminal
events, non-monotonic timestamps, malformed JSON, unknown fields, wrong types,
invalid colors/strokes/coordinates, cap violations, and over-bound logs fail
closed. Missing trace evidence produces explicit `UNMEASURED/BLOCKED` metric
records; it never becomes a zero or a hardware pass. `workflow_status` reports
whether the runner/marker/parser lifecycle completed, while
`experiment_verdict` remains independently `UNMEASURED`, `INCOMPLETE`, or
`FAIL` when the experiment evidence cannot support a pass.

Required hardware prerequisites are a registered ESP32-S3 Box-3 kit with a
matching USB serial binding and a clean build carrying `LAB10_READY`. Visual,
scanout, touch-latency, audio, task, heap/PSRAM/stack, and synchronized
screen/audio conclusions additionally require their corresponding external
hardware evidence. The runner has no server, send, recipient, inbox, or
networking path. Cancellation closes the serial capture and preserves a
`LAB10_CANCEL` marker and an incomplete run envelope.

## Execution command forms

L10-04 provides the firmware renderer and LVGL publication bridge. The L10-08
registration uses the existing Makefile pattern rules; no new Makefile target
or server is required.

```text
make lab.lab10.help
make lab.lab10 KIT=<kit>                 # manual hardware capture
make lab.lab10.scripted KIT=<kit>        # deterministic hardware run
make lab.lab10.build KIT=<kit>
make lab.lab10.flash KIT=<kit>
make lab.lab10.capture KIT=<kit>
make lab.lab10.parse RUN_ID=<run-id>
```

Expected evidence location: `docs/labs/evidence/lab-10/runs/<run-id>/`. Do not
claim a successful hardware experiment until the registry, adapter, readiness
marker, capture, parser, and required synchronized evidence have been
independently verified. A completed workflow is not an experiment PASS.
