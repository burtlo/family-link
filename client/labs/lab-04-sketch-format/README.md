# LAB-04 — color sketch format and preview strategies

| Identity | Value |
|---|---|
| Lab | `LAB-04`, revision `v1` |
| Firmware project | `lab04_sketch_format` |
| Target | ESP32-S3 |
| Build identity | assigned by the later firmware build; no measured build exists in this MODEL package |
| Server | none |
| Fixture revision | `lab04-sketch-format/v1` |
| Trace schema | `lab04-trace/v1` |
| Verification present | native/model L1 plus ESP-IDF firmware build when qualified |

This lab compares timed colored points, timed per-stroke color records, and a segment stream with one final-image preview. Sparse checkpoints and replay-to-position, plus the legacy uncolored point path, are exploratory. It does not select a product wire format, seek/checkpoint/cache policy, or make claims about LAB-03 playback quality. `OPEN-SKP-03` and `COM-OPEN-002` remain open. RGB565 palette values remain injected by a caller; this model does not map rail colors to wire values.

## Model boundaries and ownership

`components/lab04_format/model.cpp` is the production domain implementation used by native tests and linked directly into the ESP-IDF firmware harness. It owns format validation, three exploratory encoders/decoders, legacy uncolored compatibility, raster stepping, local-generator coordinate mapping, and operation/queue outcome state. The caller owns the submitted drawing and destination drawing. Decode builds a bounded temporary candidate and replaces the destination only after full validation; malformed data cannot partially change geometry. The firmware uses one worker task and bounded job/publication queues; no product session stage exists in this package.

The asynchronous envelope is `Operation { id, terminal, terminal_count, live_buffers, reason }`. Accepted decode work has one ID and one terminal (`success`, `failure`, or `cancelled`). Cancellation is non-blocking in the model and releases its temporary-buffer accounting. `DecodeOperation` owns the operation state; its caller owns the bytes and destination. `EncodeQueue` is single-owner, capacity four, and emits one `failure / overflow` terminal on the first rejected push while preserving the four queued items and counting overflow. No worker waits on another worker: calls are synchronous model operations. In a future firmware harness, the harness/caller owns scheduling and must not block UI callbacks; decode/raster updates report bounded progress back to that owner.

| Boundary | Declared capacity / behavior |
|---|---|
| Accepted input | `maximum_input_size = 65,536` bytes; larger input fails as `input_too_large` |
| Geometry | 120×120 logical pixels; at most 4,096 segments |
| Encode queue | 4 drawings; explicit rejection, preserved queue count, overflow counter, one failure terminal |
| Concurrent model operation | 1 per `DecodeOperation` instance |
| Raster update | at most 64 eligible segments per call, even when caller requests more; provisional lab probe only |
| Preview image | one RGB565 120×120 buffer = 28,800 bytes by arithmetic; not a performance pass |
| Capture mapping | local generator only: display coordinate 0–239 maps by integer divide-by-two to 0–119; out-of-range rejects |

### ResourceBudget

All entries below are **provisional lab probe** limits, not product budgets or measured hardware capacities.

```text
ResourceBudget
  max_live_nodes: 16384              # 4 queued drawings × 4096 segments
  max_transient_bytes: 65536          # one bounded encoded input / preview-independent decode input
  max_persistent_bytes: 262144        # provisional 4 × 4096 × 16-byte segment slots
  max_concurrent_operations: 1
  max_stack_or_call_depth: 8           # model call-chain probe declaration
  maximum_input_size: 65536
```

S-17 is not applicable: LAB-04 is a format/raster model without a staged user session or stage writer. The later firmware harness may introduce harness state, but it does not turn this model into a product session.

## Scenarios and terminal inventory

Only `F-malformed`, `F-overflow`, and `F-cancel` are decided experiment inputs. The encoding comparisons, checkpoint arm, and legacy arm are exploratory and MUST remain outside `experiment_inputs`. This package does not write a measured `experiment_verdict`.

| Scenario | Class | Expected terminal | Model evidence |
|---|---|---|---|
| `F-points` | exploratory | success when semantic roundtrip matches | Corpus B order, color, and pen-up boundaries survive points encode/decode; encoded bytes retained |
| `F-strokes` | exploratory | success when semantic roundtrip matches | Same equality through per-stroke color records; encoded bytes retained |
| `F-preview` | exploratory | success with one preview allocation | Preview storage counted; encoded bytes retained |
| `F-checkpoint` | exploratory | explicit `unsupported` for every encoding at early, middle, and late positions | `checkpoint_seek` returns `CheckpointOutcome::Unsupported` with reason `unsupported`; checkpoint/replay-to-position decision remains open |
| `F-legacy` | exploratory | success via explicit uncolored compatibility, otherwise `unsupported_legacy` | No color is invented; colored sketches cannot enter the legacy encoder |
| `F-malformed` | decided | one failure with named reason | truncated record, wrong color count, or input above maximum fails; destination remains unchanged |
| `F-overflow` | decided | one failure, reason `overflow` | Fifth queue push is rejected; four queued items and overflow count remain visible |
| `F-cancel` | decided | one cancelled terminal | Mid-decode cancellation reports `terminal_count=1`, `live_buffers=0`; duplicate terminal rejected |

## Fixtures and checks

`fixtures/short.sketch` and `fixtures/dense.sketch` are exact byte copies of LAB-03 Corpus A inputs. `fixtures/corpus-b.csv` is deterministic, has 14 ordered segments, duration 2,400 ms, and includes background-color strokes (palette index 0). Palette values in Corpus B are indices only; this does not decide RGB565 mapping. `fixtures/manifest.json` records input hashes and lengths, point counts and duration, plus the generated Corpus B output byte lengths and hashes for each encoding arm.

Focused native/model check:

```sh
python3 scripts/tests/test_lab04_model.py
```

The tests compile the production C++ component directly, then exercise roundtrip identity, malformed/oversized rejection, rejection of uncolored records in colored formats, preview storage arithmetic, explicit queue overflow, exactly-one cancellation terminal, zero live buffers, legacy uncolored behavior, explicit unsupported checkpoint behavior across all variants, capture mapping, and the 64-segment raster work cap. Host results do not qualify a device build, runtime, performance, or visual playback.

## Trace contract

`trace-schema.json` describes each emitted `LAB04_TRACE` `scenario_terminal` row, including its `scenario`, `variant`, operation identity, classification, terminal, raw timing samples, and resource observations. `raster_us` is the elapsed time for one call to `raster_update`, not a full-raster duration. `raster_work_cap` declares the 64-segment per-call cap; `raster_eligible`, `raster_rendered`, and `raster_remaining` count work eligible at `position_ms`, work completed by that update, and eligible work still pending. Rows where no update ran report zero work counts. The strict host parser preserves these counters as integers and rejects a cap other than 64, work above the cap, or inconsistent remaining work. The firmware emits `variant=points`, `variant=strokes`, and `variant=preview` for the three encoding arms. Exploratory observations remain present but cannot enter `experiment_inputs`. This synthetic trace accepts no secrets or household media.

For each successful `F-preview` operation, firmware emits a `resource_sample` point row immediately after decode completes and before raster work starts (`phase=post_decode_pre_raster`). The row is a point observation and does not assert a phase-specific peak.

On a semantically successful preview decode, firmware also emits one `resource_peak` row using the production decoder's reservation callback. The callback samples after the decoder reserves maximum output capacity and before it inserts any decoded records; `expected_output_segments`, `decoded_capacity_bytes`, and their Ledger B counterparts describe that reservation. The preview is the 28,800-byte image carried in the encoded payload and remains live through decode and raster work. This bounded allocation observation is not a whole-system concurrent-task temporal peak, and it is not a performance pass. A decode or semantic failure suppresses the peak row. Ledger A remains the LVGL pool; Ledger B remains encoded image plus decoded segment storage. A busy LVGL lock leaves Ledger A unavailable.

The row reports **Ledger A — LVGL pool** from `lv_mem_monitor`: `total_size`, `free_size`, `used_pct`, `free_biggest_size`, and `max_used`. All five fields are present in the pinned LVGL `lv_mem_monitor_t` API. `max_used` is LVGL's allocator high-water value and is not attributed to this phase. The measurement follows the [LVGL budget foundation's Ledger A/B definitions](../../../docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md). The monitor call runs while holding the BSP display mutex. If the bounded lock attempt fails, all Ledger A values are unavailable and `lvgl_sample_status=display_lock_busy` is emitted.

The row reports **Ledger B — pixel and decoded memory**: the 28,800-byte RGB565 image resides in the encoded result's `MALLOC_CAP_DEFAULT` allocation through raster work; the decoded drawing remains resident through this sample, with its segment count and actual vector capacity bytes recorded. Internal and PSRAM largest free blocks are also sampled at this point. These are point readings and do not establish a transient peak or minimum-free high-water mark. Older host parsers ignore this supplemental event, so it does not enter their current metrics aggregation until the HOST parser supports it.

After callbacks stop, the worker and publication queue drain, firmware emits `event=publication_loss`, then an unsequenced `event=trace_summary` footer. Every sequenced row receives `seq=1..N`; the footer reports `expected_rows=N`, `publication_queue_drops`, and `serial_write_errors`. Sequenced rows use a fixed-size output buffer and check formatting, `fwrite`, and flush results. These API results do not prove physical UART delivery or complete host capture. Footer absence leaves trace reconciliation unknown.

S-10 parser sensitive-data and redaction fixture tests are HOST-owned because the parser is outside this MODEL package. They are required in the HOST package; this MODEL package does not claim S-10 is satisfied.

## ESP-IDF firmware harness

The project targets ESP32-S3 and uses the Box-3 BSP/LVGL display. Manual mode
shows a short purpose and step-by-step guide on screen, three strategy buttons,
and a fault-cases control; each action submits a bounded job and the worker
publishes results to the app task. Scripted mode
queues all three variants, their checkpoint observations, malformed/overflow/
cancel cases for each variant, and the legacy path. Scripted success is emitted
only after all required publications arrive. Display, LVGL, queue, preview, or
worker initialization failure emits `LAB04_INIT_FAILED` and a failed harness
marker without `LAB04_READY` or a success marker.

### Manual device capture steps

Run `make lab.lab04 KIT=<kit>` with the target connected and keep the serial
capture running for its full 30-second window. At startup the screen explains
the goal: compare three sketch encodings, without selecting a product format.
The `Points`, `Strokes`, and `Preview` buttons are across the top; `Fault cases`
is below them. Tap `Points`, `Strokes`, and `Preview` once each, waiting for the
status label to update after each tap. Then tap `Fault cases` once. It runs the
bad-input, overflow, and cancellation scenarios for all three encodings; wait
until it finishes. If `Fault cases` is tapped too early, the status names the
missing strategy. Submit that strategy, then tap `Fault cases`. Duplicate taps
are ignored. The status `Complete - reset to rerun` means all scenarios ended;
the controls then stop accepting input and the device must be reset before
another run. `Stopped - reset to rerun` means a scenario failed, so keep that
run as failed/unmeasured evidence. These instructions describe the interaction
and do not establish a hardware or experiment pass.

`CONFIG_LAB04_SCRIPTED` is enabled only by
`sdkconfig.defaults.scripted`. Build the variants with ESP-IDF v5.4.2 into
separate outputs, for example:

```sh
idf.py -B build/labs/lab-04-sketch-format-manual \
  -D SDKCONFIG=build/labs/lab-04-sketch-format-manual/sdkconfig build
idf.py -B build/labs/lab-04-sketch-format-scripted \
  -D SDKCONFIG=build/labs/lab-04-sketch-format-scripted/sdkconfig \
  -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.scripted' build
```

The app uses one 28,800-byte RGB565 preview allocation, a worker queue of eight
jobs, and a publication queue of sixteen events. Trace rows include operation
identity, one terminal outcome, per-variant encoded bytes, raw per-operation
timings, and largest free blocks for internal memory, PSRAM, and LVGL. The
parser keeps these raw rows and adds `lab_metrics.measurements` records for
`encode_us`, `decode_us`, `raster_us`, `seek_us`, and the three largest-free-block
series. Each record includes its unit, provenance, sample count, raw samples,
nearest-rank p50/p95/p99, worst, and `missing_reason` when empty. Raster timing
is eligible only on a successful row with eligible work; zero counters for a
skipped update and failed operations are not samples. The adapter marks the
measurements `hardware` only after a successful reset-gated capture from its
resolved target. Fixture and standalone parser calls default to `host`, and
empty series use `unmeasured/blocked` provenance. Performance verdict remains
`UNMEASURED` because this lab has no approved performance budget. Checkpoint
operations are explicitly unsupported at all tested positions, so a successful
harness marker reports `checkpoints=0`.
