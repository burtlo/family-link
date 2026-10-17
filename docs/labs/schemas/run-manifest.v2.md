# Generic lab run manifest (`family-link.lab-run-manifest/v2`)

This version is the shared host-run envelope for registered labs. It does not
change the meaning or layout of [`family-link.lab-run-manifest/v1`](run-manifest.v1.md),
which remains the LAB-02 compatibility contract.

## Identity and versioning

Each run has one unique `run_id`, one registered `lab_id`, and one registered
`evidence_slug`. The slug determines the run directory:

`docs/labs/evidence/<evidence_slug>/runs/<run_id>/`

`document` is exactly `family-link.lab-run-manifest/v2`. A producer must not
write a v2 shape under the v1 identifier. `firmware.build_fingerprint` records
the identity of compiled inputs (source, target, build options, and compiled-in
runtime configuration). It must not include `run_id`, selected kit identity,
or other capture-only context. `run_id` identifies the capture and is recorded
separately at the manifest top level.

## Common fields

| Field | Type | Meaning |
|---|---|---|
| `document` | string | `family-link.lab-run-manifest/v2` |
| `lab_id` | string | Registered lab identifier, such as `LAB-05` |
| `evidence_slug` | string | Registered safe path segment used in artifact roots |
| `run_id` | string | Unique safe path segment for this capture |
| `started_at_utc`, `ended_at_utc` | ISO 8601 string | Observed run bounds |
| `firmware.build_fingerprint` | string or `null` | Fingerprint of compiled inputs only |
| `artifacts` | object | Repo-relative references to existing regular files under approved roots |
| `event_loss.host_journal.status` | string | `NONE` or `OBSERVED`; loss in the bounded host journal only |
| `event_loss.host_journal.dropped_events` | integer | Host journal rows omitted by its bound |
| `event_loss.firmware_trace.status` | string | `NONE`, `OBSERVED`, or `UNKNOWN`; parsed device-side trace loss |
| `event_loss.firmware_trace.dropped_events` | integer or `null` | Device trace rows dropped, or `null` when not reported |
| `workflow_status` | string | `PASS`, `FAIL`, or `INCOMPLETE` |
| `experiment_verdict` | string | `PASS`, `FAIL`, `UNMEASURED`, or `INCOMPLETE` |
| `terminal_reason` | string | Sanitized reason observed at finalization |

Other common context may include source, build and capture stage summaries,
hardware, fixture, server, scenarios, operator, and verification levels. Unknown
facts stay `null` or are listed as missing; the writer must not infer them.
Lab-specific values stay in their existing lab-specific fields or namespaces.

## Artifact safety

The writer receives its evidence and serial roots from trusted registered lab
configuration. Roots must be repo-relative and resolve beneath the repository.
For this run, event and metrics references are limited to the run directory;
serial references are limited to that run's directory beneath the approved
serial root. Absolute paths, traversal, paths outside those roots, symlink
escapes, missing files, and non-files are omitted as `null`. The event journal
is created by the writer in the run directory.

## Partial runs and verdicts

Create the run directory before preflight. Finalize a manifest on failure,
interruption, and incomplete capture with the evidence actually observed.
Workflow completion and experiment outcome are independent: a workflow can
complete while the experiment fails or remains unmeasured. Workflow `PASS`
requires explicit caller completion and successful cleanup. Parser success
alone cannot promote either verdict.

The event sink retains the v1 safety properties: sanitized values, bounded
regular event count and encoded row size, one reserved truncation marker, one
reserved terminal row, and atomic manifest replacement. If manifest writing
fails, retry uses the original frozen terminal facts.

Every `events.jsonl` row has a `source` field: `host`, `server`, `device`, or
`unknown`. Host journal counts apply only to this journal. LAB-02 firmware loss
is parsed from its serial markers and reported separately under
`firmware_trace`; absent or unparseable device loss evidence stays `UNKNOWN`
with a `null` count. Zero host drops do not imply zero device trace loss. Snap
trace rows include `snap_origin` (`touch`, `harness`, `programmatic`, or
`unknown`) on both start and end records.

## Example

```json
{
  "document": "family-link.lab-run-manifest/v2",
  "lab_id": "LAB-05",
  "evidence_slug": "transition-resource",
  "run_id": "20261017-transition-manual-01",
  "started_at_utc": "2026-10-17T14:00:00Z",
  "ended_at_utc": "2026-10-17T14:04:00Z",
  "firmware": {
    "build_fingerprint": "sha256:compiled-inputs",
    "build_id": null
  },
  "artifacts": {
    "events": "docs/labs/evidence/transition-resource/runs/20261017-transition-manual-01/events.jsonl",
    "serial_log": null,
    "metrics": null,
    "device_log": null
  },
  "event_loss": {
    "host_journal": {"status": "NONE", "dropped_events": 0},
    "firmware_trace": {"status": "UNKNOWN", "dropped_events": null}
  },
  "workflow_status": "FAIL",
  "experiment_verdict": "UNMEASURED",
  "terminal_reason": "preflight rejected"
}
```
