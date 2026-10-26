# LAB-16 memory campaign — all VARIANT summaries

Generated: 2026-10-26 03:38 UTC by `render_memory_results.py`. Do not edit by hand; run `make lab.lab16.results`.

Campaign index: [README.md](../README.md). Per-run inventory: [runs/README.md](../runs/README.md). Full per-beat tables live in each linked `results/<variant>.md` file.

Cells: interval minimum free (or min largest DMA block) in KiB and % of the region heap total KiB named in each column header.

## `upload`

Accepted pair: [20261025-upload-manual-01](../runs/20261025-upload-manual-01/run-manifest.json) / [20261025-upload-manual-02](../runs/20261025-upload-manual-02/run-manifest.json) · fixture `lab16-upload/v2` · repeatability **PASS** · [full results](upload.md)

| Moment | Internal min free KiB (% of 329.10 KiB heap total) | DMA min free KiB (% of 321.15 KiB DMA heap total) | Min largest DMA block KiB (% of 321.15 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.65 (79%) | 253.07 (79%) | 168.00 (52%) | 16381.51 (100%) |
| SD + microphone recording active | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) |
| Wi-Fi and WAV upload active | 187.13 (57%) | 177.85 (55%) | 92.00 (29%) | 16315.11 (100%) |
| Upload complete, workers quiescent | 203.13 (62%) | 195.55 (61%) | 108.00 (34%) | 16378.25 (100%) |

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

## `server-offline-during-record`

Accepted pair: [20261025-server-offline-memory-03](../runs/20261025-server-offline-memory-03/run-manifest.json) / [20261025-server-offline-memory-04](../runs/20261025-server-offline-memory-04/run-manifest.json) · fixture `lab16-upload/v2` · repeatability **PASS** · [full results](server-offline-during-record.md)

| Moment | Internal min free KiB (% of 329.10 KiB heap total) | DMA min free KiB (% of 321.15 KiB DMA heap total) | Min largest DMA block KiB (% of 321.15 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.65 (79%) | 253.07 (79%) | 168.00 (52%) | 16381.51 (100%) |
| Recording active with server offline | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) |
| Server online; Wi-Fi and WAV upload active | 186.92 (57%) | 177.72 (55%) | 92.00 (29%) | 16315.11 (100%) |
| Upload complete, workers quiescent | 203.13 (62%) | 195.55 (61%) | 108.00 (34%) | 16378.25 (100%) |

```text
LAB-16 memory repeatability (server-offline-during-record): 20261025-server-offline-memory-03 vs 20261025-server-offline-memory-04
Memory/stack checks: 77/77 within tolerance
Repeatability: PASS

Informational (does not fail memory repeatability):
  - sd_mic_recording pcm_bytes_max: 6718720 vs 6719360 (beat-window max; finals may still match)
  - wifi_upload_active accepted_requests_max: 210 vs 209 (beat-window max; finals may still match)
  - stream_elapsed_ms: 209933 vs 209933 (delta 0, tol 20993 ms)
  - request_latency_mean_ms: 104.0 vs 108.0 (delta 4.0, tol 250.0 ms)
  - request_latency_median_ms: 104.3 vs 107.9 (delta 3.7, tol 250.0 ms)
  - request_latency_max_ms: 153.0 vs 231.6 (delta 78.6, tol 250.0 ms)
```

## `overlap-record-during-upload`

Accepted pair: [20261025-overlap-memory-03](../runs/20261025-overlap-memory-03/run-manifest.json) / [20261025-overlap-memory-05](../runs/20261025-overlap-memory-05/run-manifest.json) · fixture `lab16-diary/v1` · repeatability **PASS** · [full results](overlap-record-during-upload.md)

| Moment | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) |
| Message 1 recording | 202.40 (62%) | 194.82 (61%) | 108.00 (34%) | 16378.25 (100%) |
| Message 1 uploading while message 2 records (overlap peak) | 174.36 (53%) | 166.87 (52%) | 76.00 (24%) | 16315.07 (100%) |
| Message 2 uploading | 186.66 (57%) | 179.21 (56%) | 88.00 (27%) | 16315.07 (100%) |
| Both sessions complete and workers quiescent | 202.98 (62%) | 195.40 (61%) | 92.00 (29%) | 16378.21 (100%) |

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

## `live-stream-failover-during-record`

Accepted pair: [20261025-livestream-memory-03](../runs/20261025-livestream-memory-03/run-manifest.json) / [20261025-livestream-memory-04](../runs/20261025-livestream-memory-04/run-manifest.json) · fixture `lab16-diary/v1` · repeatability **PASS** · [full results](live-stream-failover-during-record.md)

| Moment | Internal min free KiB (% of 329.08 KiB heap total) | DMA min free KiB (% of 321.14 KiB DMA heap total) | Min largest DMA block KiB (% of 321.14 KiB DMA heap total) | PSRAM min free KiB (% of 16384.00 KiB heap total) |
|:---|:---:|:---:|:---:|:---:|
| Baseline before SD, microphone, and Wi-Fi | 260.63 (79%) | 253.05 (79%) | 168.00 (52%) | 16381.51 (100%) |
| Live recording and HTTP posting | 187.11 (57%) | 179.53 (56%) | 92.00 (29%) | 16315.11 (100%) |
| Server outage / SD-retained chunks | 183.12 (56%) | 175.55 (55%) | 88.00 (27%) | 16315.11 (100%) |
| Drain of retained chunks after recovery | 187.15 (57%) | 179.57 (56%) | 92.00 (29%) | 16315.11 (100%) |
| 180 chunks accepted and workers quiescent | 203.08 (62%) | 195.51 (61%) | 108.00 (34%) | 16378.25 (100%) |

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

