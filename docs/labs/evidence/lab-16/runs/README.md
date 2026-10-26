# LAB-16 run bundles

Immutable evidence per run: `run-manifest.json`, `events.jsonl`, `metrics.json` (when captured), `server.log`, `notes.md`. Serial captures: `logs/device/labs/lab-16/<run_id>/serial.txt`.

Published memory tables and accepted pairs: [../README.md](../README.md). Timing/outlier forensics: [../TIMING-AND-NETWORK-NOTES.md](../TIMING-AND-NETWORK-NOTES.md).

## Accepted memory pairs (required)

| VARIANT | Run A | Run B |
|---|---|---|
| `upload` | `20261025-upload-manual-01` | `20261025-upload-manual-02` |
| `server-offline-during-record` | `20261025-server-offline-memory-03` | `20261025-server-offline-memory-04` |
| `overlap-record-during-upload` | `20261025-overlap-memory-03` | `20261025-overlap-memory-05` |
| `live-stream-failover-during-record` | `20261025-livestream-memory-03` | `20261025-livestream-memory-04` |

## Canonical historical functional runs (pre–memory campaign)

Linked from [campaign README](../../../../client/labs/lab-16-attached-storage/README.md) and `manifest.lab.json`:

- `20261023-upload-run-b193b616`
- `20261023-server-offline-during-record-run-d9566b03`
- `20261023-overlap-record-during-upload-run-8d644d2f`
- `20261023-live-stream-failover-during-record-run-77755e4a`

## Supplemental runs (retained, not in accepted pairs)

- `20261024-upload-run-c4c3662e` — interrupted upload attempt (README)
- `20261025-server-offline-memory-02` — timing outlier ([TIMING notes](../TIMING-AND-NETWORK-NOTES.md))
- `20261025-overlap-memory-01`, `02`, `04`, `06` — overlap forensics ([TIMING notes](../TIMING-AND-NETWORK-NOTES.md))
- `20261025-livestream-memory-01`, `02` — host failure / server journal quirk ([TIMING notes](../TIMING-AND-NETWORK-NOTES.md))

Duplicate Oct 2023 functional attempts and empty failed host-only retries were removed from this tree when they were not linked from docs.
