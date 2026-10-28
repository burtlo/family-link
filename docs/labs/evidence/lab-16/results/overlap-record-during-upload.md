# LAB-16 VARIANT `overlap-record-during-upload` — memory results

Generated: 2026-10-28 02:42 UTC by `render_memory_results.py`. Do not edit by hand; run `make lab.lab16.results`.

**Accepted repeatability pair:** [20261025-overlap-memory-03](../runs/20261025-overlap-memory-03/run-manifest.json) and [20261025-overlap-memory-05](../runs/20261025-overlap-memory-05/run-manifest.json).

**Firmware / fixture:** `(see run manifest)`, fixture `lab16-diary/v1`.

Table cells are **interval minimum free** (or minimum largest DMA block) in KiB, with **percent of that region's heap total** at the beat (totals KiB are in each column header). Values are not comparable to the V2 startup reference (different firmware).

**Repeatability:** memory/stack and identity only; timing lines in the compare block are informational ([TIMING-AND-NETWORK-NOTES.md](../TIMING-AND-NETWORK-NOTES.md)).

<a id="campaign-summary"></a>
## Campaign summary (accepted pair)

Rows below use run A; the pair is identical on these summary beats (repeatability **PASS**).

| Moment | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) |
| Message 1 recording | 202.40 (62%) | 194.82 (61%) | 108.00 (34%) | 16378.25 (100%) |
| Message 1 uploading while message 2 records (overlap peak) | 174.36 (53%) | 166.87 (52%) | 76.00 (24%) | 16315.07 (100%) |
| Message 2 uploading | 186.66 (57%) | 179.21 (56%) | 88.00 (27%) | 16315.07 (100%) |
| Both sessions complete and workers quiescent | 202.98 (62%) | 195.40 (61%) | 92.00 (29%) | 16378.21 (100%) |

<a id="repeatability"></a>
## Repeatability compare

```text
LAB-16 memory repeatability (overlap-record-during-upload): 20261025-overlap-memory-03 vs 20261025-overlap-memory-05
Memory/stack checks: 88/88 within tolerance
Repeatability: PASS

Timing / networking (informational; does not fail memory repeatability):
  - request_latency_max_ms: 2122.2 vs 1602.1 (delta 520.1, tol 250.0 ms)

Informational (does not fail memory repeatability):
  - request_latency_mean_ms: 132.3 vs 127.0 (delta 5.3, tol 250.0 ms)
  - request_latency_median_ms: 115.8 vs 111.5 (delta 4.3, tol 250.0 ms)
```

<a id="run-20261025-overlap-memory-03"></a>
## Full beat windows — `20261025-overlap-memory-03`

Metrics: [metrics.json](../runs/20261025-overlap-memory-03/metrics.json)

| Beat | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 260.08 (79%) | 252.50 (79%) | 168.00 (52%) | 16381.51 (100%) | 5 |
| `overlap_msg1_mic_i2s_open_start` | 253.10 (77%) | 245.53 (76%) | 160.00 (50%) | 16378.38 (100%) | 3 |
| `overlap_msg1_mic_i2s_open_complete` | 253.10 (77%) | 245.53 (76%) | 160.00 (50%) | 16378.38 (100%) | 1 |
| `overlap_msg1_recording` | 202.40 (62%) | 194.82 (61%) | 108.00 (34%) | 16378.25 (100%) | 3638 |
| `overlap_concurrent_peak` | 174.36 (53%) | 166.87 (52%) | 76.00 (24%) | 16315.07 (100%) | 3602 |
| `overlap_msg2_upload` | 186.66 (57%) | 179.21 (56%) | 88.00 (27%) | 16315.07 (100%) | 459 |
| `upload_complete_workers_quiescent` | 202.98 (62%) | 195.40 (61%) | 92.00 (29%) | 16378.21 (100%) | 1 |


<a id="run-20261025-overlap-memory-05"></a>
## Full beat windows — `20261025-overlap-memory-05`

Metrics: [metrics.json](../runs/20261025-overlap-memory-05/metrics.json)

| Beat | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 260.08 (79%) | 252.50 (79%) | 168.00 (52%) | 16381.51 (100%) | 5 |
| `overlap_msg1_mic_i2s_open_start` | 253.10 (77%) | 245.53 (76%) | 160.00 (50%) | 16378.38 (100%) | 3 |
| `overlap_msg1_mic_i2s_open_complete` | 253.10 (77%) | 245.53 (76%) | 160.00 (50%) | 16378.38 (100%) | 1 |
| `overlap_msg1_recording` | 202.70 (62%) | 195.12 (61%) | 108.00 (34%) | 16378.25 (100%) | 3637 |
| `overlap_concurrent_peak` | 174.36 (53%) | 166.76 (52%) | 76.00 (24%) | 16315.07 (100%) | 3603 |
| `overlap_msg2_upload` | 185.12 (56%) | 179.15 (56%) | 88.00 (27%) | 16315.07 (100%) | 536 |
| `upload_complete_workers_quiescent` | 202.96 (62%) | 195.38 (61%) | 92.00 (29%) | 16378.21 (100%) | 1 |

