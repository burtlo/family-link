# Generic lab metrics (`family-link.lab-metrics/v2`)

This envelope is shared by lab-specific parsers. It does not prescribe metric
names, units, or reductions. Existing LAB-02 output remains
`family-link.lab-metrics/v1`; see [metrics.v1.md](metrics.v1.md).

## Common fields

| Field | Type | Meaning |
|---|---|---|
| `document` | string | `family-link.lab-metrics/v2` |
| `run_id` | string | Must match the run manifest |
| `schema_version` | string | Version of the lab parser's input/event schema |
| `parser_version` | string | Version or revision of the parser producing this file |
| `evidence.status` | string | `COMPLETE`, `INCOMPLETE`, or `UNKNOWN` |
| `evidence.trace_loss` | string | `NONE`, `OBSERVED`, or `UNKNOWN` |
| `evidence.firmware_trace_loss` | object | Parsed device-side loss fields, retaining `null` when absent |
| `evidence.missing` | string[] | Measurements or evidence sources not observed |
| `lab_metrics` | object | Lab-specific metrics, with their established names and semantics |

Missing measurements remain missing; they are not represented as zero. An
observed or unknown trace-loss state makes evidence incomplete unless the lab's
specification explicitly defines another truthful classification. Parser
success is not a workflow or experiment verdict. Those verdicts remain in the
run manifest and are authored independently from metric reduction.

## Example

```json
{
  "document": "family-link.lab-metrics/v2",
  "run_id": "20261017-transition-scripted-01",
  "schema_version": "transition-events/v1",
  "parser_version": "git:abc123",
  "evidence": {
    "status": "INCOMPLETE",
    "trace_loss": "UNKNOWN",
    "firmware_trace_loss": {
      "status": "UNKNOWN",
      "dropped_events": null,
      "oversized_events": null,
      "malformed_rows": null
    },
    "missing": ["resource_sample", "transition_end"]
  },
  "lab_metrics": {
    "transition_count": null,
    "peak_heap_bytes": null
  }
}
```
