# REPORT — LAB-10 Record canvas and media budget (template)

Replace placeholders and publish as `REPORT-LAB-10-<rev>.md` only after the
run artifacts and missing-evidence reasons are preserved. This template follows
the contract in [`client/labs/lab-10-record-canvas/manifest.lab.json`](../../../../client/labs/lab-10-record-canvas/manifest.lab.json)
and its package-local [`trace-schema.v1.json`](../../../../client/labs/lab-10-record-canvas/trace-schema.v1.json).

## Run identity and lab version

- Run ID: `20261017-L10-RECORD-CANVAS-001`
- Lab version: `LAB-10/<rev> = firmware <build-id> + server none + fixture none-local-capture`
- Trace schema: `lab10-record-trace/v1` (see the package-local schema above)
- Adapter: `lab10/v1` (allowlisted shared-runner adapter)
- Parser: `lab10-record-parser/v1` at `client/labs/lab-10-record-canvas/tools/parse_lab_trace.py`
- Parser output: `family-link.lab10-parser-output/v1` in `metrics.json`
- Firmware revision:
- Host revision / toolchain:
- Configuration snapshot:
- Raw trace path and SHA-256:
- Parsed metrics path and SHA-256:

### Resolved runner commands and prerequisites

```text
make lab.lab10.help
make lab.lab10 KIT=<kit> LAB10_WORKLOAD=manual-<case>
make lab.lab10.scripted KIT=<kit>
make lab.lab10.parse RUN_ID=<run-id>
```

- Run mode: `manual | scripted` (scripted is the deterministic mode)
- Run marker: `LAB10_RUN` observed exactly once:
- Readiness marker: `LAB10_READY` observed exactly once:
- Scenario markers and terminal uniqueness:
- Cancellation marker, if applicable: `LAB10_CANCEL`
- Required hardware prerequisites: registered ESP32-S3 Box-3 kit, matching USB
  serial binding, clean build identity, and synchronized screen/audio capture
  for any visual/scanout/audio claim.
- Missing prerequisites and absent trace evidence remain `UNMEASURED` or
  `BLOCKED`; the runner never synthesizes hardware/audio/display/touch results.

### Instrumentation and aggregation contract

- Observation document: `lab10-metrics-observation/v1`
- Aggregator: [`client/labs/lab-10-record-canvas/tools/aggregate_metrics.py`](../../../../client/labs/lab-10-record-canvas/tools/aggregate_metrics.py)
- Parser output wrapper: `family-link.lab10-parser-output/v1`; parser metadata,
  lifecycle markers, diagnostics, and evidence provenance are preserved beside
  the aggregator's `metric_schema`, `metrics`, and `preserved` objects.
- Output record field order: `value`, `unit`, `provenance`, `sample_count`, `missing_reason`
- Unit source: `manifest.lab.json#/metric_schema/field_units`
- Percentile rule: nearest-rank; sort finite real samples and select one-based `ceil(p*n/100)` for p50/p95/p99; max is the largest real sample; no interpolation
- Empty/missing sample rule: `value=null`, `sample_count=0`, explicit `missing_reason`; unmeasured/blocked values or samples are rejected, never zero-filled or marked pass
- Parser bounds: 2 MiB input document, 4 MiB output, 16,384 trace events, 4,096 samples per series, 32,768 total samples, 256 repeated runs
- Hardware-only host gaps: CPU/task busy/load/stack and internal-heap/PSRAM fields use `provenance=unmeasured/blocked` with the exact hardware-only reason from the manifest; `host`/`derived_static` records for these fields must remain null with zero samples
- Preserved evidence: raw/display/logical coordinates, color changes, stroke and pen-up boundaries, terminal/cap/overflow/loss events, render budgets, audio occupancy/continuity observations, and workload identity/provenance remain in the preserved trace evidence

Do not report precomputed percentile values as aggregator output. Supply the
declared base sample series (for example `ui_update_us`) and retain its real
sample count. Host model records remain host evidence and cannot establish
touch latency, display scanout, device task scheduling, heap/PSRAM/stack
pressure, or audible audio continuity.

Record whether a run is manual, scripted device capture, or host synthetic
generation. Scripted device captures use the fixed
`scripted-human-like-multicolor-180s-v1` identity; `LAB10_WORKLOAD` does not
override it. The UI-task workload schedules 18 strokes with 17 real palette
changes at variable 36–52 ms cadence, with bounded pen-up gaps and pauses.
Completion requires the 180-second duration cap, 3400–4095 accepted points,
6–18 strokes, 5–17 palette changes with `strokes = palette_changes + 1`, all
five ink colors in accepted points, and no trace loss. Do not combine manual
touch-latency evidence with host-generated timestamps.

## Workload identity and evidence boundary

Record one stable identity per trace. Workload metadata is
`lab10-workload/v1`; trace metadata remains `lab10-record-trace/v1`.

| Source | Stable identity examples | Required metadata | Permitted evidence |
|---|---|---|---|
| Manual | `manual-no-stroke-audio-control`, `manual-sparse-variable-speed`, `manual-continuous-pauses-corners`, `manual-dense-multicolor`, `manual-background-erase`, `manual-edge-corner-leave-reenter`, `manual-rapid-color-switch`, `manual-stop-during-stroke`, `manual-cancel-during-audio`, `manual-mic-failure`, `manual-codec-failure`, `manual-allocation-failure` | `source=manual`, `seed=null`, `generator_version=not_applicable`, `timestamp_jitter_bound_us=0`, actual monotonic device clock, hardware provenance only when a kit is exercised | Human gesture, visual, touch, and synchronized screen/audio observations when the corresponding hardware evidence is present |
| Scripted device | `scripted-human-like-multicolor-180s-v1` | `mode=scripted`; 18 UI-task strokes, 17 palette changes, 36–52 ms cadence, 180-second duration cap; no `lab10-workload/v1` seed | Scripted runner completion only; hardware measurements still require target evidence |
| Synthetic | `synthetic-sparse-multicolor-v1`, `synthetic-continuous-multicolor-v1`, `synthetic-dense-multicolor-v1`, `synthetic-edge-corner-v1`, `synthetic-rapid-color-switch-v1`, `synthetic-background-erase-v1`, `synthetic-stop-during-stroke-v1`, `synthetic-cancel-during-audio-v1` | `source=synthetic`, nonzero `seed`, `generator_version=synthetic-human-like-v1`, `timestamp_jitter_bound_us=2000`, declared feature list, `evidence_provenance=host` | Generator determinism, schema, bounds, parser/aggregation behavior, and deterministic renderer pressure only |

Synthetic raw/display coordinates may be equal because no hardware transform is
being modeled. Synthetic timestamps and their declared ±2,000 µs jitter bound
must never be reported as touch latency. Host output cannot establish display
scanout, audio continuity, task scheduling, or hardware heap/PSRAM/stack
pressure. Missing, absent, or unsynchronized hardware artifacts are
`UNMEASURED` or `BLOCKED`, not zero and not a hardware pass. Preserve raw trace
loss and malformed/missing evidence reasons in the report.

### Workload coverage record

| Required case | Manual identity / procedure artifact | Synthetic identity / seed | Observed feature or missing reason |
|---|---|---|---|
| Sparse, continuous, and dense strokes | | | |
| Variable speed, spacing, pauses, corners | | | |
| Pen-up gaps and multiple strokes | | | |
| Rapid color changes | | | |
| Background/erase strokes | | | |
| Edge/corner points | | | |
| Leave and re-enter | | | |
| Stop, cancel, and failure injection | | | |

For synthetic traces, link the bounded trace artifact and generator version. For
manual traces, link the raw device trace and any synchronized recording. Do not
combine the two classes into one latency or audio metric distribution.

## Hardware and capture prerequisites

- Board / kit alias:
- LCD and touch controller:
- Audio input/output path, if exercised:
- IDF/toolchain:
- Display/LVGL profile:
- External synchronized screen/audio recording and cue offset:
- Firmware and host probes that may affect timing:
- Missing or unavailable prerequisite:

If no suitable kit or synchronized recording was available, mark the affected
claim `UNMEASURED` or `BLOCKED`; do not infer hardware behavior from host output.

## Preserved residual disclosures

- Application IRAM: preserve the existing L10-04 result of **1 byte free after
  successful link**. L10-06 does not relax or remeasure this headroom.
- Hardware visual/touch/scanout/lock/resource/audio evidence: `UNMEASURED` or
  `BLOCKED` unless the required kit and synchronized capture are present.
- Synthetic workload timestamps and ±2,000 µs jitter are generator metadata,
  not touch-latency evidence.

## Fixed contract confirmation

| Item | Required value | Observed / evidence |
|---|---|---|
| LCD | 320×240 | |
| Canvas | x `[0,240)`, y `[0,240)`, 240×240, black, 8 px radius where supported | |
| Rail | x `[240,320)`, y `[0,240)`, 80×240, `#101418` | |
| Logical storage | 120×120; `floor(display/2)` clamped to `0…119` | |
| Palette | six 44 px circles, 36 px pitch, ±11 px zig-zag, 10 px bottom margin | |
| Default color | white, palette index 1 | |
| Duration bound | 180,000 ms | |
| Accepted-point bound | 4,096 | |
| Queue bound | one FIFO, 16 immutable raw reports | |
| MOVE policy | same stroke/color/logical point and display distance `<2 px` may coalesce | |
| Hardware clock | one monotonic device clock in microseconds | |

Record any contract deviation as a finding; do not silently revise the manifest
in the report.

## Scenarios

| Scenario | Workload | Level | Workflow result | Experiment result | Evidence / missing reason |
|---|---|---|---|---|---|
| sparse multicolor | manual / synthetic | | | | |
| dense multicolor | manual / synthetic | | | | |
| background-colored strokes | manual / synthetic | | | | |
| edge and corner points | manual / synthetic | | | | |
| rapid color switch | manual / synthetic | | | | |
| no-stroke audio control | manual / synthetic | | | | |
| stop during stroke | manual / synthetic | | | | |
| cancel during audio | manual / synthetic | | | | |
| mic failure | manual / injected | | | | |
| codec failure | manual / injected | | | | |
| allocation failure | host / hardware | | | | |
| duration cap | host / hardware | | | | |
| point cap | host / hardware | | | | |
| queue overflow | host / hardware | | | | |
| repeated start/stop/cancel | manual / synthetic | | | | |

Use the repository distinction between workflow completion and experiment
verdict. A completed host workflow may still be `UNMEASURED` or `INCOMPLETE`.

### L10-05 audio-concurrency scenarios

L10-05 uses the package-local bounded host model in
`client/labs/lab-10-record-canvas/components/lab10_audio/`. It does not
integrate the firmware app with a microphone, codec, DAC, DMA, or speaker.

| Stable identity | Expected outcome | Observed | Evidence / missing reason |
|---|---|---|---|
| `no-stroke-control` | `completed-audio-continuous` | | |
| `sparse-drawing` | `completed-audio-continuous` | | |
| `continuous-drawing` | `completed-audio-continuous` | | |
| `dense-drawing` | `completed-with-drawing-pressure` | | |
| `rapid-color-switching` | `completed-audio-continuous` | | |
| `stop-during-audio` | `stopped-and-clean` | | |
| `cancel-during-audio` | `cancelled-and-clean` | | |
| `audio-capture-failure` | `capture-failed-and-clean` | | |
| `allocation-failure` | `allocation-failed-and-clean` | | |

Audio continuity/occupancy/overrun/underrun and frame/sample counts must be
reported separately from drawing queue/backlog and latency. Every metric record
must include provenance and `missing_reason`; host-model results must include
`hardware_audio_health_measured=false` and state that DAC/microphone/codec/DMA
and audible continuity were not exercised.

### Concurrency ownership and provenance

The L10-05 model serializes its mutable state with one portable C11
`atomic_flag` lock compiled in both the native host test and the ESP-IDF
component. The lock covers start/restart, producer/consumer, drawing
submit/process, stop/cancel/fail/complete, and metrics/result reads. Session
tokens are validated under that lock before any state mutation; a stale token
cannot enter a restarted session. Terminal publication and cleanup are
one-shot under the same lock, and duplicate terminal calls are rejected.

The PCM and drawing queues remain fixed and bounded; synchronization adds no
unbounded queue and the lock is non-recursive. `lab10_audio_destroy` has a
caller-owned lifetime precondition: it must not race with any model call, and
all worker threads must be stopped and joined before destroy. Record the
pthread regression and TSan result as host synchronization evidence only. The
host provenance remains `host bounded model only; microphone, codec, DAC,
speaker, DMA, and task scheduling were not exercised`; no hardware audio or
audible-continuity claim follows from this test.

### Session restart and cleanup timestamp evidence

| Item | Required evidence | Observed / missing reason |
|---|---|---|
| Old/new session tokens | nonzero tokens differ after restart | |
| Stale worker calls | old-token produce/consume/drawing/terminal/result calls are rejected | |
| Stale rejection metric | `stale_worker_mutation_count` increases without queue/frame/sample counter changes | |
| Terminal timestamp | deterministic monotonic host-model timestamp | |
| Cleanup timestamp | post-terminal cleanup timestamp from the same schedule | |
| Cleanup duration | `cleanup_time_us = cleanup_timestamp_us - terminal_timestamp_us` | |
| Zero/order cases | zero is valid; reversed timestamps are rejected; pre-schedule allocation failure is explicit | |

The host model cannot promote these results to hardware-worker or audible audio
evidence. Hardware audio remains unmeasured.

## Metrics summary

Link the complete `metrics.json` and report field-level provenance. Use p50,
p95, p99, and max for sampled distributions; preserve `missing_reason` for
unmeasured or blocked fields.

| Metric group | Required fields / highlights | Provenance | Result / missing reason |
|---|---|---|---|
| Input | raw, accepted, coalesced, rejected, rail/outside/second-touch, edge, stroke, color, queue, cap, terminal | | |
| Latency/UI | touch-to-ink, accepted-to-present, UI update, invalidations, mapping mismatch, replay mismatch | | |
| CPU/tasks | capture worker/UI busy, capture/UI/audio stack high-water | | |
| Audio | continuity, occupancy/high-water in bytes and frames, overruns/underruns, frame/sample counts, errors, retained/transient PCM | | |
| Drawing/concurrency | event/queue occupancy/high-water/overflow, drawing latency p50/p95/p99/max, color/stroke events, stale-worker session rejections | | |
| Memory | sketch/framebuffer retained/transient, largest allocation, allocation attempts/failures, internal/PSRAM free/largest block, drift | | |
| Format | final bytes/version, logical dimensions, strokes, colors, pen-up boundaries, trace loss | | |
| Cleanup/repetition | cleanup_time_us, stop/cancel cleanup, live resources, repeated-run drift | | |

## 2× replay comparison

- Display capture artifact:
- Card/replay artifact:
- Scale and crop method:
- Logical-coordinate and aspect-ratio agreement:
- Mismatch count / provenance:
- Visual confirmation status: `CONFIRMED | UNMEASURED | BLOCKED`

This comparison does not establish LCD scanout or touch latency without hardware
capture and synchronization evidence.

## Resource and failure boundaries

| Boundary | Expected terminal outcome | Observed | Resource cleanup evidence |
|---|---|---|---|
| 180 s duration cap | `duration_cap` | | |
| 4,096th accepted point | `point_cap` | | |
| 16-entry queue full | `queue_overflow` | | |
| allocation failure | `failed` / `allocation_failure` | | |
| stop | `stopped` | | |
| cancel | `cancelled` | | |

State whether each operation produced exactly one terminal event and whether a
late worker/callback changed state after terminalization.

## Host versus hardware limits

Host evidence may support geometry, mapping, state-machine, parser/schema,
workload-generator, aggregation, deterministic-renderer checks, and the bounded
audio-concurrency model. Hardware is
required for visual fidelity, touch-to-ink latency, scanout, LVGL invalidation or
lock behavior, task load, audio continuity, actual heap/PSRAM/stack pressure,
and synchronized screen/audio conclusions. LAB-03 is playback-only and is not
capture evidence. The L10-05 host model cannot promote model continuity to DAC,
microphone, codec, DMA, speaker, or audible-continuity evidence.

## Decision and exclusions

| Topic | Outcome | Evidence |
|---|---|---|
| Geometry and mapping | OPEN / supported / deviation | |
| Palette routing and color retention | OPEN / supported / deviation | |
| Bounded capture policy | OPEN / supported / deviation | |
| Stream/chunk versus whole-record control | OPEN / recommendation | |
| Audio concurrency | OPEN / UNMEASURED / BLOCKED / recommendation | |
| Product promotion | **Not decided by this lab report** | |

This LAB-10 package does not add or evaluate a server, send/upload, recipient,
inbox, durable outbox, product networking, or historical-v1 archive change. Do
not turn a provisional bound or host result into a product budget without a
separate reviewed decision.

## Verification summary

| Area | Highest level | Status | Limitation |
|---|---|---|---|
| Contract/static geometry | L0–L1 | | |
| Host capture model and bounds | L0–L1 | | |
| Deterministic workload | L0–L1 | | |
| Hardware visual/touch | L2–L3 | | |
| Audio continuity | L2–L3 | | |
| Resource pressure / repeated runs | L2–L3 | | |

Final experiment verdict: `PASS | FAIL | INCOMPLETE | UNMEASURED`.
