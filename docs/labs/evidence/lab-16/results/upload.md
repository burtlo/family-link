# LAB-16 VARIANT `upload` — memory results

Generated: 2026-10-26 00:22 UTC by `render_memory_results.py`. Do not edit by hand; run `make lab.lab16.results`.

**Accepted repeatability pair:** [20261025-upload-manual-01](../runs/20261025-upload-manual-01/run-manifest.json) and [20261025-upload-manual-02](../runs/20261025-upload-manual-02/run-manifest.json).

**Firmware / fixture:** `(see run manifest)`, fixture `lab16-upload/v2`.

Column semantics match the campaign README: **total** is the region total at the beat window; **free** is the **interval minimum free** during that beat; the DMA block column is the **interval minimum largest free DMA block**. Values are not comparable to the V2 startup reference (different firmware).

<a id="campaign-summary"></a>
## Campaign summary (accepted pair)

Rows below use run A; the pair is identical on these summary beats (repeatability **PASS**).

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---:|---:|---:|---:|
| Baseline before SD, microphone, and Wi-Fi | 329.10 / 260.65 | 321.15 / 253.07 | 168.00 | 16384.00 / 16381.51 |
| SD + microphone recording active | 329.10 / 253.21 | 321.15 / 245.63 | 160.00 | 16384.00 / 16378.38 |
| Wi-Fi and WAV upload active | 329.10 / 187.13 | 321.15 / 177.85 | 92.00 | 16384.00 / 16315.11 |
| Upload complete, workers quiescent | 329.10 / 203.13 | 321.15 / 195.55 | 108.00 | 16384.00 / 16378.25 |

<a id="repeatability"></a>
## Repeatability compare

```text
LAB-16 upload repeatability: 20261025-upload-manual-01 vs 20261025-upload-manual-02
Memory/stack checks: 77/77 within tolerance
Repeatability: PASS

Informational (beat-window peaks; does not fail repeatability):
  - sd_mic_recording pcm_bytes_max: 6718720 vs 6719360 (beat-window max; finals may still match)
  - wifi_upload_active accepted_requests_max: 209 vs 210 (beat-window max; finals may still match)
```

<a id="run-20261025-upload-manual-01"></a>
## Full beat windows — `20261025-upload-manual-01`

Metrics: [metrics.json](../runs/20261025-upload-manual-01/metrics.json)

| Beat | Internal total / min free | DMA total / min free | DMA min largest | PSRAM total / min free | Samples |
|---|---:|---:|---:|---:|---:|
| `baseline_before_sd` | 329.10 / 260.65 | 321.15 / 253.07 | 168.00 | 16384.00 / 16381.51 | 1 |
| `sd_mount_probe` | 329.10 / 260.10 | 321.15 / 252.52 | 168.00 | 16384.00 / 16381.51 | 4 |
| `mic_i2s_open_start` | 329.10 / 253.25 | 321.15 / 245.67 | 160.00 | 16384.00 / 16378.38 | 2 |
| `mic_i2s_open_complete` | 329.10 / 253.21 | 321.15 / 245.63 | 160.00 | 16384.00 / 16378.38 | 2 |
| `sd_mic_recording` | 329.10 / 253.21 | 321.15 / 245.63 | 160.00 | 16384.00 / 16378.38 | 4200 |
| `wifi_upload_active` | 329.10 / 187.13 | 321.15 / 177.85 | 92.00 | 16384.00 / 16315.11 | 553 |
| `upload_complete_workers_quiescent` | 329.10 / 203.13 | 321.15 / 195.55 | 108.00 | 16384.00 / 16378.25 | 1 |


<a id="run-20261025-upload-manual-02"></a>
## Full beat windows — `20261025-upload-manual-02`

Metrics: [metrics.json](../runs/20261025-upload-manual-02/metrics.json)

| Beat | Internal total / min free | DMA total / min free | DMA min largest | PSRAM total / min free | Samples |
|---|---:|---:|---:|---:|---:|
| `baseline_before_sd` | 329.10 / 260.65 | 321.15 / 253.07 | 168.00 | 16384.00 / 16381.51 | 1 |
| `sd_mount_probe` | 329.10 / 260.10 | 321.15 / 252.52 | 168.00 | 16384.00 / 16381.51 | 4 |
| `mic_i2s_open_start` | 329.10 / 253.25 | 321.15 / 245.67 | 160.00 | 16384.00 / 16378.38 | 2 |
| `mic_i2s_open_complete` | 329.10 / 253.21 | 321.15 / 245.63 | 160.00 | 16384.00 / 16378.38 | 2 |
| `sd_mic_recording` | 329.10 / 253.21 | 321.15 / 245.63 | 160.00 | 16384.00 / 16378.38 | 4200 |
| `wifi_upload_active` | 329.10 / 187.13 | 321.15 / 179.47 | 92.00 | 16384.00 / 16315.11 | 559 |
| `upload_complete_workers_quiescent` | 329.10 / 203.13 | 321.15 / 195.55 | 108.00 | 16384.00 / 16378.25 | 1 |

