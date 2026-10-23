# LAB-15 — capture memory envelope

**Authoritative plan:** [`docs/labs/lab-15-capture-memory-envelope.md`](../../../docs/labs/lab-15-capture-memory-envelope.md)

Status: **registered** — `lab15` in `config/labs.registry.yaml`; L0 build + L1 host tests.

## One-line goal

On real hardware, record **internal / PSRAM / LVGL largest-free** and explicit **allocation success or failure** when loading a **catalog-heavy UI** and reserving a **full 180 s PCM buffer** (5,760,000 bytes), with optional exploratory **Opus link** and **SDMMC spill** arms.

## Quick start

```text
make lab.lab15.build
make lab.lab15.scripted.build
make lab.lab15.scripted KIT=<kit>
```

Expected scripted serial:

```text
LAB15_READY build=<id> arm=catalog_pcm_none_none harness=1
LAB15_HARNESS_DONE status=complete
```

## Scene

Montage id **`lab15-montage-v1`** (`fixtures/lab15-montage-v1.yaml`): Montserrat 16/32 header, six-color rail, 240×240 canvas placeholder.

## Resource budget (provisional lab probe)

| Field | Value |
|-------|-------|
| `max_live_nodes` | 128 |
| `max_transient_bytes` | 6,500,000 |
| `max_persistent_bytes` | 250,000 |
| `max_concurrent_operations` | 2 |
| `max_stack_or_call_depth` | 12 |
| `maximum_input_size` | 4096 |
| PCM ring | 5,760,000 B |
| Sketch upper bound | 32,768 B |
| Canvas | 115,200 B × 2 buffers |
| LVGL pool planning | `CONFIG_LV_MEM_SIZE` (65,536 default) |

## Kconfig arms

| Symbol | Role |
|--------|------|
| `CONFIG_LAB15_UI_NONE` / `CONFIG_LAB15_UI_CATALOG` | Baseline vs catalog montage |
| `CONFIG_LAB15_AUDIO_PCM_RING` / `CONFIG_LAB15_AUDIO_PCM_CHUNK` | Ring vs chunked PCM |
| `CONFIG_LAB15_OPUS_LINK` | Stub (default off) |
| `CONFIG_LAB15_SDMMC_SPILL` | Stub (default off) |
| `CONFIG_LAB15_SCRIPTED` | Fake monotonic clock harness |
| `CONFIG_LAB15_PEAK_ALLOC_ONLY` | `E-catalog-peak-only` style run |

Default **scripted** profile: `UI_CATALOG` + `PCM_RING`, Opus off, SDMMC off (`sdkconfig.defaults.scripted`).

## Experiment inputs (decided)

- `E-baseline-ui` — `UI_NONE`, PCM ring, fake clock to 180 s
- `E-catalog-ui` — `UI_CATALOG`, PCM ring, fake clock to 180 s
- `E-catalog-peak-only` — `UI_CATALOG`, reserve at t=0, `PEAK_ALLOC_ONLY`
- `E-chunk-fragmentation` — `UI_CATALOG`, PCM chunk arm (8 × 720,000 B)

Exploratory: `X-opus-link`, `X-sdmmc-spill`, `X-realtime-180s`.

## Host tests

```text
python client/labs/lab-15-capture-memory-envelope/tests/test_lab15_allocation_math.py
python client/labs/lab-15-capture-memory-envelope/tests/test_lab15_parser.py
python client/labs/lab-15-capture-memory-envelope/tests/test_lab15_terminals.py
```

Or `make test` (includes `scripts/tests/test_lab15_host.py`).

## Logical version

```text
LAB-15/v1 = firmware <build-id> + fixture lab15-envelope/v1 + trace lab15-envelope-trace/v1
```

No server. `workflow` and `service` are `not_configured` in the run manifest.

## Evidence

Report template: `docs/labs/evidence/lab-15/TEMPLATE-REPORT.md`.
