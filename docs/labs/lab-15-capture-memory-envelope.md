# LAB-15 — capture memory envelope (UI + PCM + optional codec)

Status: **registered** — `client/labs/lab-15-capture-memory-envelope/` and `lab15` in `config/labs.registry.yaml`; L2 hardware memory verdict remains UNMEASURED until L15-06.

Purpose: measure whether a **product-representative desk image** can retain a **full owner-decided capture** (180 s `pcm_s16le` at 16 kHz mono, logical 120×120 sketch, 240×240 record canvas) without silent allocation failure—and whether **optional Opus** or **external spill** arms change the answer. This lab does **not** approve wire, storage medium (`COM-OPEN-003`), or a product codec choice.

Related gaps: [v2 implementation review § memory](../plans/v2-experience/review-2026-10-22.md) (5,760,000-byte PCM + sketch allocation plan); [LAB-10 report](../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md) (`experiment_verdict=UNMEASURED` for heap/PSRAM at duration cap); legacy **v1 POC archive** Opus partition probe (full shell + Opus overflowed the then-current 1.5 MiB app slot—extract facts there; do not treat as v2 approval).

## Product question

1. At the **simultaneous peak** of signed-in UI (LVGL + catalog fonts), record canvas, and worst-case audio/sketch retention, what are **internal**, **PSRAM**, and **LVGL pool** high-water marks and **largest free blocks**?
2. Can a single **contiguous** (or explicitly fragmented) reservation of **5,760,000** audio bytes plus sketch and framebuffer bytes succeed without tripping `allocation_failure`?
3. If an **Opus encode arm** is linked, does the image still **boot** and does runtime peak still allow the PCM arm’s reservation (compare arms; do not declare Opus “approved”)?
4. If **SDMMC spill** is present on the kit, can a **bounded file writer** hold the PCM stream without blocking the UI task beyond declared thresholds (exploratory until storage medium is decided)?

## Non-goals

- Send upload, inbox poll suspension, or second pending message (`COM-010`).
- Proving acoustic quality, touch latency, or upload throughput.
- Approving `pcm_s16le` vs Opus for the delivery contract.
- Modifying `client/device/` or the v1 POC archive.

## Composition (reuse, do not fork product)

| Source | Reuse in LAB-15 |
|--------|------------------|
| [LAB-10](../client/labs/lab-10-record-canvas/) | Capture state machine, 180 s cap, 4096 point cap, canvas buffer policy, trace field names for capture terminals |
| [LAB-14](../client/labs/lab-14-device-ui-catalog/) | Font faces and **one montage scene** that loads the heaviest catalog combination (see § UI load) |
| [LAB-06](../client/labs/lab-06-stage-shell/) | Optional: mount record stage inside shell instead of bare canvas—second scripted arm |
| Legacy v1 POC archive | **Read-only**: Opus link probe methodology and partition table sizes when documenting the `ARM_OPUS` flash budget row |

Link or copy components; do not edit LAB-10/LAB-14 sources in place.

## Build arms (Kconfig)

Exactly one **audio retention policy** per flash. UI and codec flags compose.

| Kconfig | Meaning |
|---------|---------|
| `CONFIG_LAB15_UI_NONE` | Capture-only control UI (LAB-10 parity baseline) |
| `CONFIG_LAB15_UI_CATALOG` | Load LAB-15 montage (fonts + representative widgets) before capture |
| `CONFIG_LAB15_AUDIO_PCM_RING` | Retain PCM in a fixed ring until stop; size **5,760,000** bytes max |
| `CONFIG_LAB15_AUDIO_PCM_CHUNK` | Retain PCM in **N** chunks (default 8 × 720,000 B); measure fragmentation |
| `CONFIG_LAB15_OPUS_LINK` | Link Opus encoder; **no** product UI exposure; encode **synthetic** PCM feed in scripted arm only |
| `CONFIG_LAB15_SDMMC_SPILL` | Exploratory: write PCM chunks to a file under `/sdcard/lab15/` (requires card present) |

Default scripted profile: `UI_CATALOG` + `PCM_RING` + Opus **off** + SDMMC **off**.

## UI load (catalog montage)

Define scene id `lab15-montage-v1` in the lab README (YAML optional under `fixtures/`). It must include, in one active tree:

- Montserrat **16** and **32** (or the same faces LAB-14 uses for login PIN and inbox header).
- At least one **split-pane** header with title + subtitle strings at max plausible length (25 chars + ellipsis rule).
- Six-color rail + **240×240** canvas placeholder (ink may be disabled in arm `PEAK_ALLOC_ONLY`).

**Peak_alloc_only scripted mode:** mount montage, allocate audio buffer(s), **do not** run 180 s real time; advance fake clock to `duration_cap` while filling PCM with a deterministic pattern. Still emit memory samples at **start**, **after UI mount**, **after audio reserve**, **at duration_cap**.

## Resource budget (provisional lab probe)

Declare before the first L2 run. Labels in reports: **provisional lab probe**, not product budget.

| Field | Provisional value | Notes |
|-------|-------------------|--------|
| `max_live_nodes` | 128 | Montage + canvas |
| `max_transient_bytes` | 6,500,000 | PCM 5,760,000 + sketch + slack |
| `max_persistent_bytes` | 250,000 | Canvas double-buffer worst case |
| `max_concurrent_operations` | 2 | UI pump + capture worker |
| `max_stack_or_call_depth` | 12 | |
| `maximum_input_size` | 4096 | Trace line cap |

**Declared next allocation check** (compare to sampled `largest_free_block` at peak):

- Audio: 5,760,000 B (PCM ring arm) or largest chunk size (chunk arm).
- Sketch: 4096 points × 8 B `flsk2` upper bound = 32,768 B (or LAB-10 packed form).
- Canvas: 115,200 B (240×240×2) × retained buffers per LAB-10 manifest.
- LVGL: use `CONFIG_LV_MEM_SIZE` from sdkconfig (default 65536 planning value).
- Opus arm: add **192 KiB** planning reserve for codec state (from legacy ~180 KiB growth; replace with measured `.map` row when built).

## Trace schema

Id: `lab15-envelope-trace/v1`. Parser: `client/labs/lab-15-capture-memory-envelope/tools/parse_lab_trace.py`.

Required events:

| Event | Fields |
|-------|--------|
| `resource_sample` | `t_us`, `arm`, `phase`, `internal_free`, `internal_largest_free`, `internal_min_free`, `psram_free`, `psram_largest_free`, `lvgl_used`, `lvgl_largest_free`, `dma_largest_free` |
| `allocation_attempt` | `kind` (`pcm_ring` \| `pcm_chunk` \| `sketch` \| `canvas` \| `opus_state`), `bytes`, `result` (`ok` \| `fail`), `region` (`internal` \| `psram` \| `sdmmc`) |
| `terminal` | `operation_id`, `outcome` (`duration_cap` \| `allocation_failure` \| `cancelled`), `reason` |

Boot marker: `LAB15_READY build=<id> arm=<ui>_<audio>_<opus>_<sdmmc> harness=<0|1>`  
Scripted done: `LAB15_HARNESS_DONE status=complete`  
Init failure: `LAB15_INIT_FAILED status=<reason>` (no ready line).

## Scenarios

### Decided experiment inputs

| ID | Input | Pass condition (experiment) |
|----|--------|-----------------------------|
| `E-baseline-ui` | `UI_NONE`, PCM ring, fake clock to 180 s | One `duration_cap` terminal; `allocation_attempt` pcm_ring `ok`; peak `psram_largest_free` ≥ 0 after reserve (record value, no product PASS threshold) |
| `E-catalog-ui` | `UI_CATALOG`, PCM ring, fake clock to 180 s | Same as baseline; **additionally** `resource_sample` after UI mount shows montage mounted (`live_nodes` > baseline) |
| `E-catalog-peak-only` | `UI_CATALOG`, reserve PCM + sketch + canvas at t=0, no clock advance | Documents **instantaneous** peak; terminal `duration_cap` or `peak_alloc_complete` |
| `E-chunk-fragmentation` | `UI_CATALOG`, PCM **chunk** arm (8 chunks), fake clock | All chunk `allocation_attempt` `ok` OR single terminal `allocation_failure` with explicit reason; no silent drop |

### Exploratory (out of `experiment_verdict`)

| ID | Class | Observation only |
|----|--------|------------------|
| `X-opus-link` | `ARM_OPUS` + `UI_CATALOG` | App `.bin` size vs partition; boot ok; runtime peak vs `E-catalog-ui` |
| `X-sdmmc-spill` | SDMMC spill during scripted fill | Bytes written, UI tick p95, `allocation_failure` on card full |
| `X-realtime-180s` | Real-time 180 s on device | Wall clock; thermal/Wi-Fi irrelevant; memory drift |

## Verification ladder

| ID | Level | Check |
|----|-------|--------|
| L15-01 | L0 | `make lab.lab15.build` and `.scripted.build` (after registry) |
| L15-02 | L1 | `test_lab15_allocation_math.py`: 5,760,000 + sketch + canvas sums match README declared peaks |
| L15-03 | L1 | `test_lab15_parser.py`: fixture serial → `metrics.json` `document=family-link.lab-metrics/v2` |
| L15-04 | L1 | `test_lab15_terminals.py`: one terminal per `operation_id` |
| L15-05 | L2 | `make lab.lab15.scripted KIT=<kit>` → manifest + serial with `LAB15_HARNESS_DONE` |
| L15-06 | L3 | `E-catalog-ui` on device: `resource_sample` rows with `provenance=hardware` at phases `ui_mounted`, `audio_reserved`, `duration_cap` |
| L15-07 | L1 | `test_lab15_experiment_inputs`: `experiment_inputs` = decided ids only; exploratory excluded |

Performance verdict for memory remains **UNMEASURED** until L15-06; workflow can be **PASS** at L2 without L15-06.

## Metrics table (report)

Copy into `REPORT-LAB-15-v1.md`:

| Phase | Internal largest free (B) | PSRAM largest free (B) | LVGL largest free (B) | PCM reserved (B) | Allocation result |
|-------|---------------------------|-------------------------|------------------------|------------------|-------------------|
| After boot | | | | 0 | |
| After UI montage | | | | 0 | |
| After audio reserve | | | | 5,760,000 | |
| At duration_cap | | | | 5,760,000 | |

Add row **App partition free (B)** for `X-opus-link` from `idf.py size` + partition CSV.

## Recommended decisions (lab output, not product)

| Outcome | Suggested follow-on |
|---------|---------------------|
| `pcm_ring` fails with `UI_CATALOG` | Owner/engineering: chunk spill, SD medium, shorter cap, or codec—before P7/P8 |
| PCM ok, Opus link overflows flash | Partition strategy (legacy POC research) or Opus in ROM/external |
| PCM ok, PSRAM largest free < 128 KiB after reserve | Risk for Wi-Fi/TLS buffers; measure under upload in a later lab |
| SDMMC spill ok | Candidate for `COM-OPEN-003` proposal |

## Implementation order

1. Fork LAB-10 project skeleton → `lab-15-capture-memory-envelope` (CMake, partitions, trace).
2. L15-02/L15-03 host tests with **stub** firmware printing scripted `resource_sample` lines.
3. Integrate catalog montage (copy LAB-14 asset pack subset or link component).
4. PCM ring allocator with explicit `allocation_attempt` trace on failure.
5. Register `lab15` in `config/labs.registry.yaml`; `make config.labs`.
6. L2 scripted run on a registered BOX-3 kit; publish `docs/labs/evidence/lab-15/REPORT-LAB-15-v1.md`.

## Registry (when ready)

| Field | Value |
|-------|--------|
| `lab_id` | `LAB-15` |
| `make` | `lab15` |
| `slug` | `lab-15-capture-memory-envelope` |
| `server` | none |
| `evidence_slug` | `lab-15` |
