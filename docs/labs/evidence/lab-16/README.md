# LAB-16 evidence — memory campaign results

Published KiB tables for the [LAB-16 memory campaign](../../../client/labs/lab-16-attached-storage/README.md). Raw artifacts remain under `runs/<run_id>/`; serial captures under `logs/device/labs/lab-16/`.

Regenerate tables: `make lab.lab16.results` (runs `client/labs/lab-16-attached-storage/tools/render_memory_results.py`).

## VARIANT results

| VARIANT | Memory status | Published tables |
|---|---|---|
| `upload` | CAPTURED (accepted pair) | [results/upload.md](results/upload.md) |
| `server-offline-during-record` | NOT CAPTURED | [results/server-offline-during-record.md](results/server-offline-during-record.md) |
| `overlap-record-during-upload` | NOT CAPTURED | [results/overlap-record-during-upload.md](results/overlap-record-during-upload.md) |
| `live-stream-failover-during-record` | NOT CAPTURED | [results/live-stream-failover-during-record.md](results/live-stream-failover-during-record.md) |
