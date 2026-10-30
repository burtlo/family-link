# LAB-16 VARIANT `upload` — memory results

Generated: 2026-10-30 09:36 UTC by `render_memory_results.py`. Do not edit by hand; run `make lab.lab16.results`.

**Accepted repeatability pair:** [20261025-upload-manual-01](../runs/20261025-upload-manual-01/run-manifest.json) and [20261025-upload-manual-02](../runs/20261025-upload-manual-02/run-manifest.json).

**Firmware / fixture:** `(see run manifest)`, fixture `lab16-upload/v2`.

Table cells are **interval minimum free** (or minimum largest DMA block) in KiB, with **percent of that region's heap total** at the beat (totals KiB are in each column header). Values are not comparable to the V2 startup reference (different firmware).

**Repeatability:** memory/stack and identity only; timing lines in the compare block are informational ([TIMING-AND-NETWORK-NOTES.md](../TIMING-AND-NETWORK-NOTES.md)).

<a id="campaign-summary"></a>
## Campaign summary (accepted pair)

Rows below use run A; the pair is identical on these summary beats (repeatability **PASS**).

| Moment | Internal min free KiB (% of 329.10 KiB heap total) | DMA min free KiB (% of 321.15 KiB DMA heap total) | Min largest DMA block KiB (% of 321.15 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.65 (79%) | 253.07 (79%) | 168.00 (52%) | 16381.51 (100%) |
| SD + microphone recording active | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) |
| Wi-Fi and WAV upload active | 187.13 (57%) | 177.85 (55%) | 92.00 (29%) | 16315.11 (100%) |
| Upload complete, workers quiescent | 203.13 (62%) | 195.55 (61%) | 108.00 (34%) | 16378.25 (100%) |

<a id="repeatability"></a>
## Repeatability compare

```text
LAB-16 memory repeatability (upload): 20261025-upload-manual-01 vs 20261025-upload-manual-02
Memory/stack checks: 77/77 within tolerance
Repeatability: PASS

Informational (does not fail memory repeatability):
  - sd_mic_recording pcm_bytes_max: 6718720 vs 6719360 (beat-window max; finals may still match)
  - wifi_upload_active accepted_requests_max: 209 vs 210 (beat-window max; finals may still match)
  - stream_elapsed_ms: 209932 vs 209936 (delta 4, tol 20993 ms)
  - request_latency_mean_ms: 107.5 vs 109.1 (delta 1.5, tol 250.0 ms)
  - request_latency_median_ms: 104.4 vs 105.3 (delta 0.8, tol 250.0 ms)
  - request_latency_max_ms: 305.1 vs 449.7 (delta 144.6, tol 250.0 ms)
```

<a id="run-20261025-upload-manual-01"></a>
## Full beat windows — `20261025-upload-manual-01`

Metrics: [metrics.json](../runs/20261025-upload-manual-01/metrics.json)

| Beat | Internal min free KiB (% of 329.10 KiB heap total) | DMA min free KiB (% of 321.15 KiB DMA heap total) | Min largest DMA block KiB (% of 321.15 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.65 (79%) | 253.07 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 260.10 (79%) | 252.52 (79%) | 168.00 (52%) | 16381.51 (100%) | 4 |
| `mic_i2s_open_start` | 253.25 (77%) | 245.67 (76%) | 160.00 (50%) | 16378.38 (100%) | 2 |
| `mic_i2s_open_complete` | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) | 2 |
| `sd_mic_recording` | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) | 4200 |
| `wifi_upload_active` | 187.13 (57%) | 177.85 (55%) | 92.00 (29%) | 16315.11 (100%) | 553 |
| `upload_complete_workers_quiescent` | 203.13 (62%) | 195.55 (61%) | 108.00 (34%) | 16378.25 (100%) | 1 |


<a id="run-20261025-upload-manual-02"></a>
## Full beat windows — `20261025-upload-manual-02`

Metrics: [metrics.json](../runs/20261025-upload-manual-02/metrics.json)

| Beat | Internal min free KiB (% of 329.10 KiB heap total) | DMA min free KiB (% of 321.15 KiB DMA heap total) | Min largest DMA block KiB (% of 321.15 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) | Samples |
|:---|:---:|:---:|:---:|:---:|:---:|
| `baseline_before_sd` | 260.65 (79%) | 253.07 (79%) | 168.00 (52%) | 16381.51 (100%) | 1 |
| `sd_mount_probe` | 260.10 (79%) | 252.52 (79%) | 168.00 (52%) | 16381.51 (100%) | 4 |
| `mic_i2s_open_start` | 253.25 (77%) | 245.67 (76%) | 160.00 (50%) | 16378.38 (100%) | 2 |
| `mic_i2s_open_complete` | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) | 2 |
| `sd_mic_recording` | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) | 4200 |
| `wifi_upload_active` | 187.13 (57%) | 179.47 (56%) | 92.00 (29%) | 16315.11 (100%) | 559 |
| `upload_complete_workers_quiescent` | 203.13 (62%) | 195.55 (61%) | 108.00 (34%) | 16378.25 (100%) | 1 |

