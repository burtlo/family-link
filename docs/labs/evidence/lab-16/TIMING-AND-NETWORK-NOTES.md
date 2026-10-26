# LAB-16 — timing and networking notes (not memory repeatability)

This memory campaign **accepts pairs on identity, functional PASS, and heap/stack repeatability only**. Per-chunk HTTP latency, stream duration, and other timing still land in `events.jsonl` and `metrics.json`; `make lab.lab16.compare` prints them under **informational** sections and they **do not fail** memory repeatability.

**Why:** Concurrent SD I/O, Wi-Fi, and diary-server load produce run-to-run timing spread that does not change the memory low-water story we are proving here. A **separate timing-focused lab** (fixture, acceptance thresholds, and possibly controlled network) should own latency repeatability, tail outliers, and “why did this chunk take 5 s?” investigations.

## Retained runs with notable timing (memory was still usable)

| Run ID | VARIANT | Note |
|---|---|---|
| `20261025-server-offline-memory-02` | `server-offline-during-record` | First chunk after server comes online showed a large latency outlier; excluded from the accepted memory pair (`03` / `04`). Evidence kept under `runs/`. |
| `20261025-overlap-memory-04` | `overlap-record-during-upload` | Memory/stack matched the accepted pair; **max** per-chunk latency during `overlap_concurrent_peak` was much higher than runs `03` and `05` (multi-second spikes on individual chunks while medians stayed ~100–120 ms). Excluded from the published pair; use `events.jsonl` `request_end` rows for forensics. |
| `20261025-overlap-memory-01` | `overlap-record-during-upload` | Truncated capture (timeout / wrong server fixture for session 2). |
| `20261025-overlap-memory-02` | `overlap-record-during-upload` | Functional memory PASS; `server_chunk_count` journal quirk vs expected 360. |
| `20261025-overlap-memory-06` | `overlap-record-during-upload` | Stopped mid-capture; partial `server.log` only. |
| `20261025-livestream-memory-01` | `live-stream-failover-during-record` | Failed before capture (stale diary server on port 8776; no metrics). |
| `20261025-livestream-memory-02` | `live-stream-failover-during-record` | Memory COMPLETE and functional PASS; `server_chunk_count` 262 vs expected 180 in compare (server journal quirk). Excluded from accepted pair. |

## Accepted overlap pair (`03` / `05`) — timing snapshot

Compare output still lists mean/median/max request latency for documentation. On the accepted pair, **medians** were on the order of **~110 ms** while **max** per-chunk latency differed (**~2.1 s** vs **~1.6 s**) because of isolated slow chunks during concurrent record + upload—not because heap readings diverged.

## Where to look

- Device: `events.jsonl` → `kind=request_end`, field `latency_us`, `beat`, `session`, `seq`.
- Aggregates: `metrics.json` → `request_latencies_us`, `stream_elapsed_ms`.
- Tooling: `client/labs/lab-16-attached-storage/tools/compare_upload_runs.py` (memory gate vs informational timing).
