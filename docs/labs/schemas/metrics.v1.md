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
