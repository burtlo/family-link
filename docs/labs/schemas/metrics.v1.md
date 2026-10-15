# Parsed lab metrics (`family-link.lab-metrics/v1`)

Produced by lab-specific parsers (e.g. `client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py`) into:

`docs/labs/evidence/<lab-slug>/runs/<run-id>/metrics.json`

## LAB-02 minimum keys

| Key | Description |
|-----|-------------|
| `document` | `family-link.lab-metrics/v1` |
| `run_id` | Matches run manifest |
| `distributions` | Per-metric `{ "p50", "p95", "p99", "worst" }` in milliseconds or microseconds as labeled |
| `distributions.ui_tick_ms` | UI owner tick period |
| `distributions.snap_ms` | Snap animation / scroll-to-focus |
| `distributions.bind_ms` / `unbind_ms` | Card rebind cost |
| `peaks.lvgl_mem_used` / `lvgl_mem_high_water` / `lvgl_biggest_free` | LVGL pool |
| `peaks.internal_largest_free` / `psram_largest_free` | Heap classes at simultaneous peak |
| `peaks.live_widget_count` | LVGL objects tied to cards |
| `counts.bind` / `counts.unbind` | Totals for run |
| `counts.stale_bind_events` | Should be 0 |
| `event_counts.harness_done` | Successful auto-harness completion (once per run; trace canonical) |
| `event_counts.harness_abort` | `HARNESS_DONE status=…` without `LAB_TRACE` success |
| `counts.wrong_content_events` | Should be 0 |
| `metadata.fixture_size` | 8, 16, 64, or 256 |
| `metadata.mode` | `recycle` or `eager` |

Parsers should preserve raw event counts even when distributions pass provisional gates.

## Additive C5 observability fields

The LAB-02 parser may add `metadata.backend`, `metadata.scenario`,
`metadata.build_id`, `metadata.trace_schema`, and observed
`metadata.effective_config` values. Effective configuration is copied only from
the validated `LAB02_READY` marker or an authoritative manifest/config adapter;
ordinary trace and resource rows cannot set it. Mismatched authoritative
markers invalidate the field; unavailable values remain `null`.

The additive `counts` fields include operation admissions and outcomes,
duplicate/orphan/unterminated terminal accounting, malformed rows, queue
rejections, and trace loss. An accepted operation is correlated by its scoped
identity and must have exactly one terminal. Missing identity, invalid elapsed
time, nonmonotonic timestamps, malformed rows, and session-reset violations
invalidate affected samples.

`trace_loss` uses `NONE`, `OBSERVED`, or `UNKNOWN`; observed or unknown loss
sets `evidence.status` to `INCOMPLETE`. Parser success is not workflow or
application PASS.

When typed `resource_sample`, `draw_allocation`, `stack_sample`, or envelope
markers are present, the parser retains them under `resources` and reduces only
observed values. Largest-free values are retained with their sample provenance
and remain `null` when a unique simultaneous peak sample cannot be identified.
It does not infer transient peaks from free-heap deltas. Fetch-attempt timing
requires an observed worker-start timestamp; terminal elapsed time alone is not
worker-attempt latency.
Absent allocator, runtime high-water, stack, static-attribution, or visual
evidence is represented by `null` values and `evidence.missing` entries.
