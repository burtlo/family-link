# Lab run manifest (`family-link.lab-run-manifest/v1`)

Each device run stores one JSON file at:

`docs/labs/evidence/<lab-slug>/runs/<run-id>/run-manifest.json`

Example lab slug: `lab-02`. **Run ID convention:** `YYYYMMDD-<phase>-<fixture>-<variant>-<seq>`  
(e.g. `20261011-phase-a-64-recycle-01`).

## Required fields

| Field | Type | Description |
|-------|------|-------------|
| `document` | string | `family-link.lab-run-manifest/v1` |
| `lab_id` | string | e.g. `LAB-02` |
| `lab_revision` | string | e.g. `v1` |
| `lab_version_string` | string | Human triple: `LAB-02/v1 = firmware … + server … + fixture …` |
| `run_id` | string | Unique per run |
| `started_at_utc` / `ended_at_utc` | ISO 8601 | Run bounds |
| `verification_levels` | object | Map scenario ID → `L0`…`L3` |
| `firmware.build_id` | string | Immutable build stamp |
| `firmware.project_path` | string | Repo-relative ESP-IDF project |
| `firmware.idf_version` | string | e.g. `v5.4.2` |
| `firmware.cmake_options` | object | Kconfig/CMake flags that affect behavior |
| `server.path` | string | Repo-relative server tree or `none` |
| `server.revision` | string \| null | Server package revision when used |
| `fixture.schema` | string | e.g. `inbox-summary/v1` |
| `fixture.revision` | string | Fixture content revision |
| `fixture.sha256` | string | Hash of primary fixture file used |
| `hardware.board` | string | e.g. `esp32s3-box-3` |
| `artifacts.serial_log` | string | Repo-relative path under `logs/device/labs/` |
| `artifacts.metrics` | string | Repo-relative `metrics.json` |
| `scenarios` | string[] | Scenario IDs exercised |
| `comparison_run_id` | string \| null | Paired run (e.g. eager vs recycle) |
| `operator` | string | `human`, `harness`, or `agent` |

## Example (Phase A)

```json
{
  "document": "family-link.lab-run-manifest/v1",
  "lab_id": "LAB-02",
  "lab_revision": "v1",
  "lab_version_string": "LAB-02/v1 = firmware abc123 + server none + fixture inbox-summary/v1",
  "run_id": "20261011-phase-a-64-recycle-01",
  "started_at_utc": "2026-10-11T18:00:00Z",
  "ended_at_utc": "2026-10-11T18:25:00Z",
  "verification_levels": {
    "S-fix-64": "L3",
    "S-trav-20": "L2"
  },
  "firmware": {
    "build_id": "abc123",
    "project_path": "client/labs/lab-02-bounded-inbox",
    "idf_version": "v5.4.2",
    "sdkconfig_sha256": "…",
    "cmake_options": {
      "LAB02_EAGER_BASELINE": "0",
      "LAB02_FIXTURE": "64"
    }
  },
  "server": {
    "path": "none",
    "revision": null
  },
  "fixture": {
    "schema": "inbox-summary/v1",
    "revision": "v1",
    "file": "fixtures/inbox_64.json",
    "sha256": "…"
  },
  "hardware": {
    "board": "esp32s3-box-3",
    "kit_alias": "lynn",
    "display_profile": "box3-default"
  },
  "artifacts": {
    "serial_log": "logs/device/labs/lab-02/20261011-phase-a-64-recycle-01/serial.txt",
    "metrics": "docs/labs/evidence/lab-02/runs/20261011-phase-a-64-recycle-01/metrics.json",
    "video": "docs/labs/evidence/lab-02/runs/20261011-phase-a-64-recycle-01/video.md"
  },
  "scenarios": ["S-fix-64", "S-trav-20", "S-rev"],
  "comparison_run_id": null,
  "operator": "harness",
  "notes": ""
}
```

Phase B: set `server.path` to `server/labs/lab-02-paging`, populate `server.revision`, and add script name in `notes` or a dedicated `server.script` field when used.
