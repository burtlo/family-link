# LAB-02 C5a trace and metrics contract

Status: reviewed implementation contract for C5b safe trace transport and C5c
instrumentation/parser/resource evidence. This document authorizes no firmware,
host-script, test, configuration, or product-policy change.

Reviewed source revision: `44cd461e36f3de260a1ac3fb3f0858b8c794ca10`
(`main`, abbreviated `44cd461`). Contract revision: `c5a-2`.

Superseded historical provenance, retained for traceability and not current
authority, is: C5a contract `c5a-1` reviewed at `589f261`
(`589f261e3cf26cf5d7d06c7bdac9d31ce6f5c22b`); historical C5 baseline
`03b5e42` (`03b5e42d15bf8c788bf80b9b55a3c103b832c32d`); and paused execution
snapshot `7ecc594` (`7ecc594861c1e6fbc9ad647ac591bb44df3acde9`).

## 1. Boundary and authority

This is an additive extension of `family-link.lab-metrics/v1`. The existing
`document`, `event_counts`, `snap_events`, legacy distribution names, legacy
peak names, legacy count names, and `metadata.fixture_size`/`metadata.mode`
remain readable with their existing meanings. New fields are optional until
observed. `run-manifest.v1` remains the run-identity authority; this contract
does not change its status semantics.

Normative sources are:

- `docs/standards/client-application-coding-standards.md` — bounded
  presentation, operation envelopes, resource budgets, observability, and the
  L0–L3 ladder.
- `docs/labs/lab-02-carousel-remediation-plan.md` — CAR-ARCH, CAR-GESTURE,
  CAR-BIND, CAR-PAGING, CAR-IDENTITY, CAR-BUDGET, CAR-OBS, and C5.
- `docs/labs/schemas/metrics.v1.md` and
  `docs/labs/schemas/run-manifest.v1.md` — compatibility and provenance.
- `docs/labs/evidence/lab-02/reviews/carousel-remediation/c5-plan/report.md`
  — historical C5 baseline at `03b5e42` — and
  `docs/labs/evidence/lab-02/reviews/carousel-remediation/execution.md` — F011
  transport prerequisite and phase boundaries.

The C5a deliverable is the contract only. C5b must make admission bounded and
nonblocking; C5c may then implement instrumentation and parsing against this
contract. Neither phase may turn an OPEN product decision into behavior.

## 2. Value, unit, and compatibility rules

- A scalar `null` means not observed, not applicable, or invalidated by missing
  required provenance. It MUST NOT be changed to zero by a parser default.
- A numeric zero means the value was observed as exactly zero during a declared,
  complete observation interval. A counter MUST remain `null` when the interval
  or capture completeness is unknown.
- An applicable distribution has `{ "unit": ..., "n": 0, "p50": null,
  "p95": null, "p99": null, "worst": null }` when no valid samples exist.
  A not-applicable distribution is `null`. Invalid samples are not included in
  `n`; they are reported in `evidence.invalid`.
- Duration fields use milliseconds in `distributions`; raw event timestamps and
  elapsed values use integer microseconds (`*_us`). Heap, allocation, queue,
  node, and stack fields use integer bytes/counts as named.
- Percentiles are deterministic nearest-rank over exact valid samples: sort
  ascending, then use zero-based index `ceil(p * n) - 1` for p50, p95, and p99.
  `worst` is the maximum valid sample. No average or interpolated percentile
  substitutes for these fields. Successful, stale, cancelled, timed-out, and
  failed operation samples are never pooled; each outcome class is either
  separately represented or excluded with a stated reason.
- Missing wire values are `-`; missing JSON values are `null`. Empty strings,
  zero IDs, and overloaded fields are not substitutes for unknown identity.
- `metrics.json` parser success is not a workflow or experiment PASS. Existing
  legacy fields remain present; additive fields may be null without breaking
  v1 consumers.

## 3. Provenance and effective configuration

`run_id` comes from the validated run manifest/coordinator context. Device
firmware MUST NOT invent or rewrite a server run ID. If a device event has no
run ID, the host reader owns attribution and records the source of that join.
The metrics object records, when observed:

```text
run_id: string|null
metadata:
  fixture_size: integer|null       # existing v1 field
  mode: "recycle"|"eager"|null    # existing v1 field
  backend: "local"|"http"|null
  scenario: string|null
  build_id: string|null
  trace_schema: string|null
  comparison_run_id: string|null
  effective_config:
    draw_buf_height_lines: integer|null
    draw_buf_count: integer|null
    draw_buf_bytes: integer|null
    color_depth_bits: integer|null
```

These values must come from validated manifest/build/config evidence or an
observed runtime marker. A mismatch between event, manifest, and effective
configuration is `evidence.invalid`, not a best-effort merge. `mode` remains
only `recycle` or `eager`; HTTP startup text does not create a third rendering
mode. Server receipt IDs, server monotonic timestamps, fixture identity,
source/binary/config hashes, board identity, and artifact paths remain in the
manifest or raw evidence. Device and server clocks are different domains and
MUST NOT be subtracted.

Resource provenance includes:

- `resources.static_bytes` for named static allocations plus `static_source`
  identifying the ELF/map/hash evidence;
- `resources.stacks[owner]` with configured bytes, minimum free bytes, method,
  and sample count; task-owned/self-sampled handles only;
- `resources.envelopes.declared` and `.measured_maxima` as separate objects;
- typed `resource_samples` retaining the timestamp and phase needed to support
  simultaneous-envelope claims; reductions alone are insufficient.

The provisional draw-buffer choice is evidence to record, not an acceptance
threshold. A display-start snapshot MUST NOT be reported as an exact allocator
measurement. Exact allocation fields require a real allocation observation
with before/after heap state, request size, alignment, capability mask, and
success result.

## 4. Trace row and event identity

The existing `LAB_TRACE` prefix remains recognizable. C5b emits one complete,
versioned row per admitted trace record, after bounded immutable producer-side
copy/admission. Formatting and output wait happen in the drain context, never
in presentation callbacks, animation ticks, session locks, or operation
terminal delivery. A row contains the applicable subset of:

```text
trace_schema, t_us, event, run_id, session_id,
op_kind, op_id, attempt, snap_id, gesture_id,
bind_generation, card_slot, page_index, revision,
message_id, stage_before, stage_after,
focus_before, focus_after, focus_msg_before, focus_msg_after,
state, submitted_us, started_us, terminal_us, elapsed_us,
outcome, reason, queue_depth, queue_high_water, overflow
```

Rows MUST contain no credentials, private URLs, or message content beyond the
stable test identifier needed for correlation. Render-publication rows carry a
bounded fixed-slot summary: slot/index, state, message ID, and revision, plus
focused ID and scroll position. They do not carry message text. The recycle
and eager slot bounds are the existing implementation bounds; this contract
does not approve a different view count.

Correlation keys are exact and scoped; rows MUST NOT be paired by adjacency:

| Operation/evidence | Required key |
|---|---|
| Fetch | host `run_id` + `session_id` + `op_kind` + `op_id` + `attempt` |
| Snap | `run_id` + `session_id` + `snap_id` |
| Bind/unbind | `run_id` + `session_id` + `card_slot` + `bind_generation` + `revision` |
| Gesture | `run_id` + `session_id` + `gesture_id` |
| Visible content | `run_id` + `session_id` + `card_slot` + `revision` + `message_id` + `publication_after_apply` |
| Server receipt | preserved receipt/run ID and server-clock fields, joined only within their clock domain |

`session_id` is owned by the session lifecycle and changes at a session reset;
an incidental telemetry accessor is not an authority. `bind_generation` is a
card-slot binding identity, not a universal operation ID. Required unknown IDs
invalidate the affected join/sample rather than matching an empty value.

## 5. Terminal, loss, and incomplete semantics

An accepted operation has one start/admission record and exactly one terminal
record. Terminal outcomes are the operation-envelope outcomes `success`,
`failure`, `cancelled`, `stale`, or `timed_out`; `no_motion` is a success
reason, not a sixth outcome. A retry has a new attempt identity or an
incremented attempt value. A bind supersession or invalidated timer is a
`stale` terminal, not a publication.

The parser MUST count and invalidate affected samples for duplicate terminals,
orphan terminals, negative/non-monotonic elapsed time, malformed rows,
session-reset violations, and accepted starts without terminals. A rejected
admission (for example queue-full or busy) is not an accepted operation and
does not require a fabricated terminal; its rejection and reason are counted.
An archived callback that never had an accepted start is not an orphan accepted
operation.

Transport loss is separate from functional terminal delivery. C5b MUST report
trace admission loss without silently dropping functional operation outcomes.
Metrics include `counts.trace_dropped`, `counts.malformed_trace`, duplicate,
orphan, and unterminated counts, plus an evidence loss record with:

```text
trace_loss:
  status: "NONE"|"OBSERVED"|"UNKNOWN"
  dropped_events: integer|null
  oversized_events: integer|null
  malformed_rows: integer|null
```

`OBSERVED` loss makes the affected evidence `INCOMPLETE`; `UNKNOWN` loss is
also `INCOMPLETE` and MUST NOT be treated as zero loss. Missing visual,
timing, resource, identity, or provenance evidence is listed in
`evidence.missing`; invalid correlations and terminal violations are listed in
`evidence.invalid`.

The evidence object is additive and uses:

```text
evidence:
  status: "COMPLETE"|"INCOMPLETE"
  missing: string[]
  invalid: string[]
  timing_status: "MEASURED"|"UNMEASURED"|"INCOMPLETE"
  visual_status: "CONFIRMED"|"UNMEASURED"|"INCOMPLETE"
  budget_status: "OPEN"|"PASS"|"FAIL"|"UNMEASURED"
```

`COMPLETE` means the required observations and capture accounting for the
claimed evidence are present; it does not mean the product passed. Workflow
status, experiment verdict, semantic scenario outcomes, and C5 evidence
status remain separate.

## 6. Metrics and resource fields

Retain `distributions.ui_tick_ms`, `snap_ms`, `bind_ms`, and `unbind_ms`, and
add the same distribution shape for `input_callback_ms`,
`animation_tick_ms`, `presentation_update_ms`, `page_apply_ms`, `parse_ms`,
`fetch_queue_wait_ms`, `fetch_attempt_ms`, `input_to_visible_ms`, and optional
`bind_wait_ms`. Definitions are precise: UI tick is consecutive owner-wake
period, not callback duration; snap duration is successful completed snap
duration; bind duration excludes configured asynchronous delay; input-to-visible
requires the matching gesture, apply, and publication identities.

Retain legacy `peaks` names, but label their reduction in the manifest. Used
and high-water values are maxima. Largest-free values are the associated value
at a documented sample, not a fabricated maximum largest-free safety margin.
When available, `resource_samples[]` includes phase, timestamp, session/op
identity, internal free/min-free/largest-free, DMA free/largest-free, PSRAM
free/largest-free, LVGL used/high-water/biggest-free, live card roots/nodes,
active/admitted fetches, fetch queue depth, and terminal-pending count. Missing
members are null.

`resources.draw_allocations[]` contains before/after timestamps, requested and
alignment bytes, capability mask, success, DMA largest-free before/after, and
internal largest-free before/after. `resources.envelopes` records the exact
same dimensions in both `declared` and `measured_maxima`:

```text
max_live_card_nodes, max_transient_bytes, max_persistent_bytes,
max_concurrent_operations, max_stack_bytes, maximum_input_bytes,
fetch_queue_capacity, terminal_capacity, model_capacity, page_capacity
```

`declared` contains engineering bounds; `measured_maxima` contains the maximum
observed values for the same dimensions. They remain separate, and neither is
a product performance gate. A free-heap before/after delta is not a transient
allocation peak.

Peak reduction is explicit: `peaks.lvgl_mem_used` and
`peaks.lvgl_mem_high_water` are maxima of valid observed samples over the
declared complete interval. `peaks.lvgl_biggest_free`,
`peaks.internal_largest_free`, and `peaks.psram_largest_free` are associated
sample values copied from the documented full resource sample selected for the
corresponding simultaneous peak; they are not independently reduced maxima or
a fabricated largest-free safety margin. The associated sample retains its
timestamp and phase, and same-timestamp resource fields remain available for
simultaneous-envelope claims.

## 7. Explicit OPEN gates and non-goals

This contract deliberately leaves all numeric, product, and L3 gates OPEN,
including numeric frame, snap, input-to-visible, memory, stack,
allocation-margin, blank-duration, and whole-attempt deadline gates; any
accepted policy for those values; full transient allocation attribution where
no faithful scoped measurement exists; physical visual/touch confirmation; and
equivalent eager-versus-recycle managed traversal. The draft `p95 < 33 ms`
reference is not an approved gate.

No C5a/C5b/C5c change may tune draw-buffer height, add retry policy, change
carousel behavior, claim HTTP/eager parity, or change workflow PASS semantics.
Unavailable transient peaks remain null with an evidence gap. A successful
build or parser run reaches only the applicable lower evidence level and never
closes a target L3 gate.

## 8. Independent verification checklist

The C5a verifier reviews only this directory and the cited source documents,
then reports the exact contract revision and source revision reviewed.

- [ ] `family-link.lab-metrics/v1` and all existing minimum/legacy keys remain
      compatible and additive fields are optional/null-safe.
- [ ] Null/unobserved, observed zero, empty distribution, invalid sample, and
      not-applicable semantics are distinguishable.
- [ ] Percentiles reproduce the specified nearest-rank calculation, including
      `n=0`, ties, one sample, and outcome-class separation.
- [ ] Every identity join uses the declared scoped key; no adjacency, empty-ID,
      focus/generation overloading, or cross-clock subtraction is accepted.
- [ ] Accepted operations have exactly one terminal; rejected admissions,
      stale/cancelled/timed-out paths, duplicate/orphan/unterminated rows, and
      reset boundaries have explicit accounting.
- [ ] Transport loss, malformed rows, and unknown loss produce INCOMPLETE
      evidence without erasing functional terminal semantics.
- [ ] Manifest/build/fixture/server/config/resource provenance is preserved;
      mismatches are invalid, and no private configuration is required.
- [ ] Resource snapshots, stack units, static attribution, and allocator
      bracketing distinguish observed measurements from declared bounds.
- [ ] All numeric product gates and whole-attempt deadline decisions remain
      OPEN; no invented threshold or behavior is present.
- [ ] The diff is documentation-only and contains no firmware, host script,
      test, private configuration, generated run, or unrelated file.

## 9. Change summary

Revised this C5a-2 contract and its machine-readable field manifest against
current committed source `44cd461e36f3de260a1ac3fb3f0858b8c794ca10`. The
change freezes the reviewed additive metrics shape, publication identity,
resource-envelope dimensions, peak reductions, and trace correlation/loss
rules for C5b/C5c while preserving v1 consumers. All numeric, product, and L3
decisions remain OPEN. No implementation or maintained source was changed.
