# LAB-16 evidence — memory campaign results

Published KiB tables for the [LAB-16 memory campaign](../../../client/labs/lab-16-attached-storage/README.md). Raw artifacts remain under `runs/<run_id>/`; serial captures under `logs/device/labs/lab-16/`.

**Repeatability** for accepted pairs is **memory and identity only**; HTTP latency and phase timing are captured but documented separately — see [TIMING-AND-NETWORK-NOTES.md](TIMING-AND-NETWORK-NOTES.md).

Regenerate tables: `make lab.lab16.results` (runs `client/labs/lab-16-attached-storage/tools/render_memory_results.py`).

**All VARIANT KiB summaries on one page:** [results/campaign-all-variants.md](results/campaign-all-variants.md). Run inventory: [runs/README.md](runs/README.md).

Compare a pair: `make lab.lab16.compare RUN_A=<run_id> RUN_B=<run_id>`.

## VARIANT results (accepted pairs)

| VARIANT | Scenario | Accepted runs (manifests) | Memory repeatability | KiB tables |
|---|---|---|---|---|
| `upload` | Record to SD, then upload | [20261025-upload-manual-01](../runs/20261025-upload-manual-01/run-manifest.json) / [20261025-upload-manual-02](../runs/20261025-upload-manual-02/run-manifest.json) | **PASS** | [results/upload.md](results/upload.md) |
| `server-offline-during-record` | Server offline during recording | [20261025-server-offline-memory-03](../runs/20261025-server-offline-memory-03/run-manifest.json) / [20261025-server-offline-memory-04](../runs/20261025-server-offline-memory-04/run-manifest.json) | **PASS** | [results/server-offline-during-record.md](results/server-offline-during-record.md) |
| `overlap-record-during-upload` | Overlap record during upload | [20261025-overlap-memory-03](../runs/20261025-overlap-memory-03/run-manifest.json) / [20261025-overlap-memory-05](../runs/20261025-overlap-memory-05/run-manifest.json) | **PASS** | [results/overlap-record-during-upload.md](results/overlap-record-during-upload.md) |
| `live-stream-failover-during-record` | Live stream, outage, SD drain | [20261025-livestream-memory-03](../runs/20261025-livestream-memory-03/run-manifest.json) / [20261025-livestream-memory-04](../runs/20261025-livestream-memory-04/run-manifest.json) | **PASS** | [results/live-stream-failover-during-record.md](results/live-stream-failover-during-record.md) |

## Peak stress snapshot (run A of each accepted pair)

Interval **minimum free** during the listed stress beat; KiB and % use that run's heap total at the beat (per-VARIANT baseline totals listed below).

**Region heap totals KiB** (run A, baseline or first summary beat — used for % denominators on variant pages):

- `upload`: internal 329.10, DMA 321.15, PSRAM 16384.00
- `server-offline-during-record`: internal 329.10, DMA 321.15, PSRAM 16384.00
- `overlap-record-during-upload`: internal 329.08, DMA 321.14, PSRAM 16384.00
- `live-stream-failover-during-record`: internal 329.08, DMA 321.14, PSRAM 16384.00

| VARIANT stress moment | Internal min free KiB (%) | DMA min free KiB (%) | Min largest DMA block KiB (%) | PSRAM min free KiB (%) |
|---|---:|---:|---:|---:|
| `upload` — Wi-Fi and WAV upload active | 187.13 (57%) | 177.85 (55%) | 92.00 (29%) | 16315.11 (100%) |
| `server-offline-during-record` — Recording with server offline | 253.21 (77%) | 245.63 (76%) | 160.00 (50%) | 16378.38 (100%) |
| `overlap-record-during-upload` — Concurrent record + upload peak | 174.36 (53%) | 166.87 (52%) | 76.00 (24%) | 16315.07 (100%) |
| `live-stream-failover-during-record` — Server outage / SD-retained chunks | 183.12 (56%) | 175.55 (55%) | 88.00 (27%) | 16315.11 (100%) |
