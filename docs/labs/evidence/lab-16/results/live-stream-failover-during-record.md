# LAB-16 VARIANT `live-stream-failover-during-record` — memory results

Generated: 2026-10-30 09:36 UTC by `render_memory_results.py`. Do not edit by hand; run `make lab.lab16.results`.

**Accepted repeatability pair:** [20261025-livestream-memory-03](../runs/20261025-livestream-memory-03/run-manifest.json) and [20261025-livestream-memory-04](../runs/20261025-livestream-memory-04/run-manifest.json).

**Firmware / fixture:** `(see run manifest)`, fixture `lab16-diary/v1`.

Table cells are **interval minimum free** (or minimum largest DMA block) in KiB, with **percent of that region's heap total** at the beat (totals KiB are in each column header). Values are not comparable to the V2 startup reference (different firmware).

**Repeatability:** memory/stack and identity only; timing lines in the compare block are informational ([TIMING-AND-NETWORK-NOTES.md](../TIMING-AND-NETWORK-NOTES.md)).

<a id="campaign-summary"></a>
## Campaign summary (accepted pair)

Rows below use run A; the pair is identical on these summary beats (repeatability **PASS**).

| Moment | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) |
| Live recording and HTTP posting | 187.11 (57%) | 179.53 (56%) | 92.00 (29%) | 16315.11 (100%) |
| Server outage / SD-retained chunks | 183.12 (56%) | 175.55 (55%) | 88.00 (27%) | 16315.11 (100%) |
| Drain of retained chunks after recovery | 187.15 (57%) | 179.57 (56%) | 92.00 (29%) | 16315.11 (100%) |
| 180 chunks accepted and workers quiescent | 203.08 (62%) | 195.51 (61%) | 108.00 (34%) | 16378.25 (100%) |

<a id="repeatability"></a>
## Repeatability compare

```text
LAB-16 memory repeatability (live-stream-failover-during-record): 20261025-livestream-memory-03 vs 20261025-livestream-memory-04
Memory/stack checks: 88/88 within tolerance
Repeatability: PASS

Informational (does not fail memory repeatability):
  - live_stream_upload_active accepted_requests_max: 27 vs 28 (beat-window max; finals may still match)
  - stream_elapsed_ms: 201809 vs 201332 (delta 477, tol 20157 ms)
  - request_latency_mean_ms: 110.2 vs 107.2 (delta 3.0, tol 250.0 ms)
  - request_latency_median_ms: 105.5 vs 103.8 (delta 1.7, tol 250.0 ms)
  - request_latency_max_ms: 2263.3 vs 2248.0 (delta 15.3, tol 250.0 ms)
```

<a id="run-20261025-livestream-memory-03"></a>
## Full beat windows — `20261025-livestream-memory-03`

Metrics: [metrics.json](../runs/20261025-livestream-memory-03/metrics.json)

| Beat | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 209.54 (64%) | 201.96 (63%) | 116.00 (36%) | 16381.38 (100%) | 40 |
| `live_mic_i2s_open_start` | 202.78 (62%) | 195.21 (61%) | 108.00 (34%) | 16378.25 (100%) | 3 |
| `live_mic_i2s_open_complete` | 202.66 (62%) | 195.08 (61%) | 108.00 (34%) | 16378.25 (100%) | 2 |
| `live_stream_upload_active` | 187.11 (57%) | 179.53 (56%) | 92.00 (29%) | 16315.11 (100%) | 626 |
| `live_failover_outage` | 183.12 (56%) | 175.55 (55%) | 88.00 (27%) | 16315.11 (100%) | 3412 |
| `live_sd_drain_active` | 187.15 (57%) | 179.57 (56%) | 92.00 (29%) | 16315.11 (100%) | 72 |
| `upload_complete_workers_quiescent` | 203.08 (62%) | 195.51 (61%) | 108.00 (34%) | 16378.25 (100%) | 2 |


<a id="run-20261025-livestream-memory-04"></a>
## Full beat windows — `20261025-livestream-memory-04`

Metrics: [metrics.json](../runs/20261025-livestream-memory-04/metrics.json)

| Beat | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 209.54 (64%) | 201.96 (63%) | 116.00 (36%) | 16381.38 (100%) | 40 |
| `live_mic_i2s_open_start` | 202.78 (62%) | 195.21 (61%) | 108.00 (34%) | 16378.25 (100%) | 3 |
| `live_mic_i2s_open_complete` | 202.66 (62%) | 195.08 (61%) | 108.00 (34%) | 16378.25 (100%) | 2 |
| `live_stream_upload_active` | 186.99 (57%) | 180.93 (56%) | 92.00 (29%) | 16315.11 (100%) | 646 |
| `live_failover_outage` | 185.11 (56%) | 177.53 (55%) | 92.00 (29%) | 16315.11 (100%) | 3383 |
| `live_sd_drain_active` | 187.13 (57%) | 179.55 (56%) | 92.00 (29%) | 16315.11 (100%) | 68 |
| `upload_complete_workers_quiescent` | 203.11 (62%) | 195.53 (61%) | 108.00 (34%) | 16378.25 (100%) | 2 |

