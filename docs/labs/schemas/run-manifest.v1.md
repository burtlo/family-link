# Lab run manifest (`family-link.lab-run-manifest/v1`)

Each accepted host run stores one JSON file at:

`docs/labs/evidence/<lab-slug>/runs/<run-id>/run-manifest.json`

Example lab slug: `lab-02`. **Run ID convention:** `YYYYMMDD-<phase>-<fixture>-<variant>-<seq>`  
(e.g. `20261011-phase-a-64-recycle-01`).

## Required fields

The manifest is a run summary, including when preflight, build, capture, or
cleanup fails. Fields whose values were not observed are `null` (or an empty
collection where appropriate) and must not be reconstructed from guesses.
`workflow_status` is `PASS`, `FAIL`, or `INCOMPLETE`; `experiment_verdict` is
`PASS`, `FAIL`, `UNMEASURED`, or `INCOMPLETE`. These are independent: a completed
host workflow can have a semantic experiment failure. Parser success alone
does not establish either PASS. A workflow PASS requires the caller to confirm
all required workflow stages completed and cleanup succeeded.

The writer also records `source` (revision, dirty state, and available file
hashes), `build` and `capture` stage summaries, `artifacts.events`,
`artifacts.device_log`, `terminal_reason`, and the two status fields. Unknown
source/build identity remains null on preflight or build failure. Firmware
`cmake_options` is a whitelist of behavior-relevant safe flags; never include
raw deployment data, Wi-Fi settings, credential-bearing URLs, or raw
`sdkconfig` contents. `hardware.kit_nickname` may identify the selected kit;
USB serial bindings are excluded.

The run directory is created exclusively from a validated single-segment run
ID. It is never reused or overwritten. Worker events are sanitized and
appended to `events.jsonl`. At most the configured regular-event quota (default
2000) is retained; each encoded row is limited to 16 KiB. Overflow and
oversized rows are dropped, one `events_truncated` row is reserved, and the
manifest records the total `event_loss.dropped_events`. The terminal row has a
separate reserved slot, so the JSONL bound is quota + two rows. Unknown object
values are replaced with `[UNSUPPORTED]`; they are never serialized via repr.
The sanitized terminal reason is shortened as needed so the JSON-encoded
`run_terminal` row stays within 16 KiB; the event sets `reason_truncated: true`
and the manifest sets `terminal_reason_truncated: true` when shortening occurs.
Both records use the same bounded reason, ending in `[truncated]`.
The final manifest is written atomically. If the atomic write fails, a retry
uses the same frozen status, reason, timestamp, and terminal event. The result
is cached only after the manifest write succeeds.

Artifact references are emitted only for existing regular files within the
canonical run roots: serial/device log at
`logs/device/labs/lab-02/<run-id>/serial.txt`, and metrics at
`docs/labs/evidence/lab-02/runs/<run-id>/metrics.json`. Absolute paths,
traversal, unrelated files, symlink escapes, missing files, and non-files are
recorded as `null`. Callers may update the writer with newly observed facts
before finalization; that update is sanitized and merged into the partial
context, which freezes at the first finalization attempt.

| Field | Type | Description |
|-------|------|-------------|
| `document` | string | `family-link.lab-run-manifest/v1` |
| `lab_id` | string | e.g. `LAB-02` |
| `lab_revision` | string | e.g. `v1` |
| `lab_version_string` | string | Human triple: `LAB-02/v1 = firmware … + server … + fixture …` |
| `run_id` | string | Unique per run |
| `started_at_utc` / `ended_at_utc` | ISO 8601 | Run bounds |
| `verification_levels` | object | Map scenario ID → `L0`…`L3` |
| `firmware.build_id` | string | null | Immutable build stamp, null if unavailable |
| `firmware.project_path` | string | null | Repo-relative ESP-IDF project |
| `firmware.idf_version` | string | null | e.g. `v5.4.2` |
| `firmware.source_sha256`, `binary_sha256`, `sdkconfig_sha256` | string | null | Available source/build provenance hashes |
| `firmware.cmake_options` | object | Whitelisted Kconfig/CMake flags that affect behavior |
| `server.path` | string | null | Repo-relative server tree or `none` |
| `server.revision` | string \| null | Server package revision when used |
| `fixture.schema`, `fixture.revision`, `fixture.sha256` | string | null | Selected fixture identity and hash |
| `hardware.board` | string | null | e.g. `esp32s3-box-3` |
| `artifacts.serial_log` | string | null | Repo-relative path under `logs/device/labs/` |
| `artifacts.metrics` | string | null | Repo-relative `metrics.json`; absent if parsing did not produce it |
| `event_loss.dropped_events` | integer | Number of event rows omitted by quota or row-size bound |
| `scenarios` | string[] | Scenario IDs exercised |
| `comparison_run_id` | string \| null | Paired run (e.g. eager vs recycle) |
| `operator` | string | null | `human`, `harness`, or `agent` |
| `workflow_status` | string | `PASS`, `FAIL`, or `INCOMPLETE` |
| `experiment_verdict` | string | `PASS`, `FAIL`, `UNMEASURED`, or `INCOMPLETE` |

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
