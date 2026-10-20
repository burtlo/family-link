# LAB-04/v1 — color sketch format and preview strategies

## Current qualification refresh — 20261020-LAB04-POSTRUN-QUAL-001

**Status: software-qualified at L0/L1; two current-build target captures now report workflow PASS and experiment PASS.** This does not mean the DEVICE package is complete. Current-parser sequence reconciliation is complete, but aggregate and firmware trace-loss statuses remain `UNKNOWN`. Performance is `UNMEASURED`; L04-07 and independent L04-DEVICE qualification remain pending. No visual evidence was supplied. The following prior report sections are retained as history and are superseded where they describe current status.

The report/index contradiction recorded as `LAB04-POSTRUN-F002` is corrected. Current-parser derivatives and the immutable raw artifact hash ledger are in [the postrun QUAL review](reviews/20261020-LAB04-POSTRUN-QUAL-001/standards-review.md) and [its evidence ledger](reviews/20261020-LAB04-POSTRUN-QUAL-001/raw-artifact-hashes.json). Raw manifests, event journals, runner metrics, and serial files were not changed. The original runner metrics predate the HOST parser fix and misleadingly classify aggregate `trace_loss=NONE` while firmware trace loss is `UNKNOWN`; retain them as captured, but use only the new parser derivatives for current interpretation.

| Run | Workflow | Experiment | Current parser / evidence |
|---|---|---|---|
| Manual `20261020-manual-run-99f0de33` | PASS | PASS | 35 operation rows; 39 sequenced trace rows reconcile completely to the footer; exact decided inputs PASS; aggregate and firmware trace loss UNKNOWN; performance UNMEASURED. |
| Scripted `20261020-scripted-run-047a3247` | PASS | PASS | Same scenario and reconciliation result; aggregate and firmware trace loss UNKNOWN; performance UNMEASURED. |

Both captures are post-reset, include the correct mode/build ready marker and `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0`, and report cleanup and parser success. They reproduce the current manual and scripted build IDs, sdkconfig hashes, and binary hashes. Capture-time project-tree SHA-256 is `4367754de387636e00a89bb47a9cd4b6f01d061fbeda478156b2c98d351bb8a9`; after the capture, only the host parser/test fix changed the LAB-04 project tree. Its current project-tree hash is `8017f2b39f1285e370cb7de3f26eda59eb5beddb7fbd4f3d85aefc6934564bfc`; fixture-tree hash remains `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`. The compiled firmware source/defaults, binary, and sdkconfig identities match; aggregate build fingerprints differ because the fingerprint includes the changed project-tree hash. The capture fingerprints remain preserved in each manifest.

The loss correction (`LAB04-POSTRUN-F001`) was independently verified: the current parser reports both aggregate and firmware trace loss `UNKNOWN`, not `NONE`, while confirming complete sequence/footer reconciliation. `COMPLETE` reconciliation is not a claim of loss-free capture. Firmware publication queue drops are zero and host-journal drops are zero; end-to-end loss across firmware publication, UART, capture, and parsing remains unknown.

Each run contains 2 resource samples and 2 resource peak records. The peak measurement kind is `decoder_maximum_output_capacity_reservation`, with `whole_system_temporal_peak=false`; this is not the simultaneous whole-system lifecycle peak required for L04-07. Raster counters are present on 35/35 rows. Timing series contain 9 encode, 8 decode, 8 raster, and 9 seek samples per run; use the linked derivatives for p50/p95/p99/worst. There are no approved performance budgets. `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain OPEN. No visual capture was provided.

| L04 criterion | Current disposition |
|---|---|
| L04-01 | L0 build identity observed in both current-build captures; build IDs, sdkconfig and binary hashes match current registered outputs. |
| L04-02 | L1 model semantics/roundtrip passed in prior verified package; no new device claim. |
| L04-03 | L1 one-image preview storage bound passed; current captures include preview operations, not product approval. |
| L04-04 | L1 malformed/oversized inputs reject without silent truncation. |
| L04-05 | **Observed L2 workflow PASS** for manual and scripted; post-reset marker, harness completion and cleanup are present. |
| L04-06 | L3 timing samples captured in both runs. Performance remains **UNMEASURED** because no budget is approved; independent DEVICE adjudication remains pending. |
| L04-07 | **Not achieved.** Reservation observations are not synchronized simultaneous whole-system peak evidence. |
| L04-08 | L1; no format or performance choice selected. `OPEN-SKP-01`, `OPEN-SKP-03`, `COM-OPEN-002` remain OPEN. |
| L04-09 | Observed L2 decided-input experiment PASS for exactly `F-malformed`, `F-overflow`, `F-cancel`; exploratory inputs do not affect the verdict. |
| L04-10 | L1 cancellation/terminal cardinality model checks pass; target captures include cancel, but are not independent DEVICE adjudication. |
| L04-11 | L1 corpus identities verified; fixture hash matches both current captures. |

| Shared criterion | Current disposition |
|---|---|
| S-07 | L1 fail-closed checks plus observed L2 post-reset ready/completion and cleanup on both runs. Concurrency/stale-result device claims remain pending. |
| S-13 | Capture provenance/build binding is observed: capture source hash is retained; fixture, build IDs, sdkconfig and binaries match current builds. Current host-inclusive tree hash changed after parser fix; DEVICE adjudication remains pending. |
| S-15 | Hardware timing series recorded with nearest-rank summaries in derivatives. No approved budgets; performance remains **UNMEASURED**. |

The prior manual failure `20261020-manual-run-87109e20` is preserved as evidence from the earlier 3,584-byte stack build. Neither current capture contains stack-overflow, panic, abort, or reboot markers. The user did not cause the earlier failure. Residual gaps include task stack high-water, end-to-end firmware trace loss, L04-07 simultaneous peak, visual evidence, independent DEVICE verification, and performance budget approval. See the [durable standards review](reviews/20261020-LAB04-POSTRUN-QUAL-001/standards-review.md) for findings and limitations.


## Qualification status — 20261019-LAB04-TRACE-PEAK-001

**Current source status: software-qualified at L0/L1 only. A new current-source scripted target run is required before making any L2 claim.** The current implementation source-tree SHA-256 is `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549`; fixture-tree SHA-256 remains `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`. Fresh manual and scripted builds are available and bind to the current source, but no target run has been captured for this source. The preserved user captures below were built from source SHA-256 `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4`; their workflow and experiment PASS outcomes remain historical observations for that earlier source and do not establish current-source behavior.

L0/L1 evidence includes the current manual/scripted ESP-IDF builds, the 11-test model suite with production-path allocation-failure injection, the 28-test HOST suite, schema and registry checks, and the final root test suite (321 passed). Finding `LAB04-F001` is fixed and independently verified at L1: each `resource_peak` is bound to exactly one successful `F-preview` terminal with matching operation ID, fixture, and variant; orphan, duplicate, failed, and wrong-variant peaks are rejected. `LAB04-F002` has an L1 software fix: allocation failures are returned as `no_memory`, terminalized once with zero live buffers, and cause the worker/harness failure path. Per coding standard §11, its target behavior remains unverified pending a current-source scripted run; it is not a device PASS.

Current registered software build identity (source and fixture hashes follow the LAB-04 adapter's source-tree framing):

| Variant | Source SHA-256 | Fixture SHA-256 | sdkconfig SHA-256 | Binary SHA-256 | Build fingerprint |
|---|---|---|---|---|---|
| Manual | `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549` | `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160` | `6f817f57e4ba43cd2605206ca1acfd3656e8fc820b3587df86b7fb54f896bb91` | `3715384a17bc207a8057267cd2a2d9bbb2ecde3c01f5023ce74d0ac917a55009` | `sha256:010d99b2388e57774da8a1f61ddcf54fe7803a6607afde0fdf969fb9ea037b14` |
| Scripted | `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549` | `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160` | `96f04388192ddd8ade0394ae45aa6a4f45a559e08e93f487933aef18e3aff184` | `09df28b9bf6d5a27bc34449d0e1800c590d6f6a4b2461d4b66c7a69bd1bf845e` | `sha256:6afd68552c5b48f798ee1d4a169982c41c0c4678bd7ac0d2ecef705a8be7efe4` |

Both effective sdkconfigs enable `CONFIG_COMPILER_CXX_EXCEPTIONS=y`, and registered compile commands include `-fexceptions`. These are fresh current-source build artifacts only; they have not been flashed or run. Their source and binary identities differ from the preserved run manifests documented in the historical section.

Current-source acceptance remains pending for L04-05 runtime behavior, L04-06 target timing, L04-07 target memory readings, end-to-end trace sequence/footer/loss reconciliation, out-of-memory worker behavior, and serial/capture behavior. `resource_peak` records the decoder's maximum-capacity reservation before any segments are inserted (zero segments at the observation); it is not a whole-system high-water or a completed L04-07 measurement. L04-07 remains **not achieved** until required target memory readings are independently verified. Performance remains **UNMEASURED**, with no approved budgets. L04-DEVICE is not selected. `OPEN-SKP-01`, `OPEN-SKP-03`, `COM-OPEN-002`, and related format, palette/wire, seek, checkpoint, and legacy choices remain OPEN.

The next sections preserve prior report history. In particular, the following capture refresh describes user runs bound to source SHA `1ffa7898…c1b4` at that time; it is retained for audit history only and is not current-source qualification.

## Historical qualification refresh — 20261019-LAB04-QUAL-CURRENT-CAPTURES-001

**Historical status at that report refresh: software-qualified at L0/L1; two reset-gated target captures were bound to source SHA `1ffa7898…c1b4` and reported workflow PASS and experiment PASS. They are not bound to the current source `93fa2323…c549` and provide no current-source runtime evidence.** The observations remain part of the historical runs. They do not qualify current-source preview accounting, bounded raster work, resource samples, timing, terminal behavior, or allocation failures. L04-DEVICE remains unselected; no full hardware qualification or performance PASS is claimed.

That refresh preserved every raw capture and the prior QUAL-SUPPLEMENT history. The implementation map is read-only at SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`. The capture identity and results below are historical; current implementation and qualification changes are uncommitted working-tree state.

### Historical source-bound capture identity and raw artifact hashes

The adapter's project-tree hashing routine independently reproduces source SHA `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4` and fixture-tree SHA `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`. For each variant, manifest identity matches the actual build directory's build ID, sdkconfig SHA, binary SHA, and recomputed build fingerprint. The serial READY marker matches the manifest build ID and mode. Hashes and byte lengths for every immutable raw manifest, events file, runner metrics file, and serial log are durable below and in [the raw artifact hash and capture audit ledger](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/raw-artifact-hashes.json). Current-parser outputs are separate derivatives.

| Run / raw artifact | Bytes | SHA-256 |
|---|---:|---|
| Manual manifest `20261019-manual-run-9cc65b45` | 2,792 | `8dab8d366edac394e9ae89b89ccf53a35fe21f329e0473c8fa1563015a88bd59` |
| Manual events | 1,210 | `f1f30e3340825270f087cc4941b702bde1ec2020e70b2ccbbe2779f47b2e1ae2` |
| Manual original metrics | 46,698 | `63901da533e9c328a90f1adad9cf1cecbcf81222eb1a137448da2ea81affe3d0` |
| Manual serial log | 25,672 | `74045dc407244abaa61d3cb0ac7d84675009396b86fb734cbb9d7703d48fcb12` |
| Scripted manifest `20261019-scripted-run-1acfdd59` | 2,806 | `d62eb93d8f063e1c8e972ec2c22378db5492038b2a0bfebc21af4a934cc21d76` |
| Scripted events | 1,216 | `3c8f12261b2034df095db42b5eaff57814b173866703763b8db74c7348076d72` |
| Scripted original metrics | 46,646 | `890f4a117924e43b13dc5ef621ec4d8bb8cfc64a0cc807914ec189f8f2355204` |
| Scripted serial log | 25,557 | `e9aeb251bf7caf3a4bb682e3509ec31db729ec2f9b0000349a21db85395a09cf` |

| Captured identity | Manual | Scripted |
|---|---|---|
| READY marker | `build=lab04-fw-v1-manual mode=manual` | `build=lab04-fw-v1-scripted mode=scripted` |
| Source tree SHA-256 | `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4` | same |
| Fixture tree SHA-256 | `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160` | same |
| sdkconfig SHA-256 | `c801c2f8544ef8540f8e09f74a8499c41465dd622e32110eeed299aa60f2aa3e` | `0c37fd773cf173eb21b996f35c37cf78ba89f505b9a2ddea0058d5fed17a40a4` |
| Firmware binary SHA-256 | `b3c24f8672fc5f1c0cfb26f9111c1d7196ae9922be906f379d2884336d0a9746` | `43090093579e2ea93fdbece969e4f48ba423352c9c6408dea8631934fa6522b6` |
| Build fingerprint | `sha256:5aa83c9d0ad9010b0c04da07619d20e4a36b1c76addddcfe91e0689268d35052` | `sha256:9dfa7e410264fd3a64f9bca4f7d062a0a801b4a10a6815a7946851b48e4cb79e` |

Both manifests report `workflow_status=PASS`, `experiment_verdict=PASS`, `capture.post_reset=true`, successful cleanup, and exact decided inventory `F-malformed`, `F-overflow`, `F-cancel`. Each serial log reparses to 35 trace rows, one matching READY marker, and one `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0` marker. These runs supplement and do not overwrite earlier failed or successful runs.

### Parser results recorded at the historical refresh

The parser independently reparsed both preserved serial logs. Outputs are [manual metrics](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/20261019-manual-run-9cc65b45-posthoc-metrics.json), [scripted metrics](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/20261019-scripted-run-1acfdd59-posthoc-metrics.json), and [capture audit](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/capture-audit.json). Both reparses reproduce experiment PASS for the decided input set. Exploratory `F-points`, `F-strokes`, `F-preview`, `F-checkpoint`, and `F-legacy` remain outside that verdict.

C-1 preview accounting is present: counters on 35/35 rows per trace; eight observations per run each account for one 28,800-byte preview image (230,400 bytes summed across those observations). C-3 raster accounting is present on 35/35 rows per run: 8,270 eligible units, 206 rendered, and 8,064 remaining; per-operation work cap is 64. These are instrumentation and operation counts, not an approved timing budget.

Each run includes two `resource_sample` events at `post_decode_pre_raster`, with the 28,800-byte image and decoded drawing both resident. The samples record Ledger A largest-free-block values and Ledger B image/decoded allocation fields. They are point observations at a selected phase; they do **not** establish the simultaneous lifecycle peak or satisfy L04-07. Each run's `firmware_publication_queue` event reports `dropped_events=0`, covering only firmware publication-queue rejection. Manifest firmware trace loss remains UNKNOWN; host journal loss is NONE. Queue count zero does not establish no loss across firmware publication, UART, capture, and parsing. Thus `evidence.status=INCOMPLETE`, `trace_loss=UNKNOWN`, and `firmware_trace_loss=UNKNOWN` remain correct.

| Hardware timing series (µs) | Manual (n) | Scripted (n) |
|---|---:|---:|
| `encode_us` p50 / p95 / p99 / worst | 568 / 25,021 / 25,021 / 25,021 (9) | 431 / 20,368 / 20,368 / 20,368 (9) |
| `decode_us` p50 / p95 / p99 / worst | 283 / 18,468 / 18,468 / 18,468 (8) | 87 / 15,396 / 15,396 / 15,396 (8) |
| `raster_us` p50 / p95 / p99 / worst | 137 / 4,233 / 4,233 / 4,233 (8) | 73 / 1,890 / 1,890 / 1,890 (8) |
| `seek_us` p50 / p95 / p99 / worst | 7 / 100 / 100 / 100 (9) | 7 / 43 / 43 / 43 (9) |

These are current-source hardware observations and provide the L04-06 measurement series, but no approved threshold exists and this is not a performance PASS. Small sample counts and p95/p99/worst coincidences are visible in the raw derivatives. No inferred resource peak is reported.

### Qualification checks and acceptance disposition

Reproduce the derivatives from repository root:

```sh
.venv/bin/python - <<'PY'
import importlib.util
from pathlib import Path
root = Path.cwd()
spec = importlib.util.spec_from_file_location(
    "lab04_parser", root / "client/labs/lab-04-sketch-format/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
out = root / "docs/labs/evidence/lab-04/reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001"
for mode, run_id in (("manual", "20261019-manual-run-9cc65b45"),
                     ("scripted", "20261019-scripted-run-1acfdd59")):
    parser.parse_file(
        root / "logs/device/labs/lab-04" / run_id / "serial.txt",
        out / f"{run_id}-posthoc-metrics.json", expected_run_id=run_id,
        expected_mode=mode, expected_variant=mode, post_reset=True,
        capture_context={"kind": "device_serial_capture", "target_id": "lynn-kit",
                         "post_reset": True})
PY
```

Inherited L0/L1 checks remain documented in implementation and prior qualification evidence. These captures do not select a DEVICE package or close product decisions.

| Acceptance | Current disposition |
|---|---|
| L04-01 | **PASS at L0:** registered manual/scripted builds passed earlier; captured source, fixture, sdkconfig, binary, build ID and fingerprint reconcile to current build artifacts. |
| L04-02 | **PASS at L1:** native roundtrip preserves corpus B geometry, colors and order. |
| L04-03 | **PASS at L1:** preview storage bound remains one 28,800-byte RGB565 image; this is not an experiment PASS of the preview strategy. |
| L04-04 | **PASS at L1:** malformed/oversized input rejects rather than silently truncating. |
| L04-05 | **Observed PASS at L2:** both current-source reset-gated captures report cleanup success and matching READY/completion markers; the scripted workflow criterion is satisfied. No visual/screenshot claim is inferred. |
| L04-06 | **Current-source L3 timing series captured:** encode/decode/raster/seek percentiles above. Performance remains **UNMEASURED** pending approved budgets and independent DEVICE qualification. |
| L04-07 | **Not achieved:** samples show image and decoded drawing resident together, but do not prove the required simultaneous lifecycle peak and largest-free-block measurement. |
| L04-08 | **PASS at L1:** `OPEN-SKP-03: OPEN`, `COM-OPEN-002: OPEN`; `OPEN-SKP-01` also remains OPEN; no `selected_format` or performance approval exists. |
| L04-09 | **PASS at L1 and observed in both current-source captures:** exact decided inventory and one terminal per decided scenario reparse successfully; workflow and experiment remain separate verdicts. |
| L04-10 | **PASS at L1; target observations present:** cancellation terminal cardinality and zero live buffers are preserved; model and host assertions passed earlier. |
| L04-11 | **PASS at L1:** Corpus A byte counts/hashes and fixture manifest were verified earlier; bound fixture-tree identity matches both new manifests. |

| Shared acceptance | Current disposition |
|---|---|
| S-01 | **PASS at L0:** LAB-04 ESP-IDF project and registered variant build paths exist and were built in HOST verification. |
| S-02 | **PASS at L1:** manifest and trace-schema contract checks passed in MODEL/HOST verification. |
| S-03 | **PASS at L1:** lab-owned parser contract and real capture reparses passed. |
| S-04 | **PASS at L1:** registry validation, config, and help commands passed. |
| S-05 | **PASS at L1:** distinct manual/scripted variant configuration was checked. |
| S-06 | **PASS at L1:** registered build-directory and variant selection checks passed in HOST verification. |
| S-07 | **PASS at L1; observed L2 completion:** host fail-closed marker checks pass; current manual/scripted records show post-reset completion and cleanup. Concurrency/stale-result device claims remain pending. |
| S-08 | **PASS at L1:** queue overflow handling and its regression checks passed. |
| S-09 | **PASS at L1:** exactly-one terminal and cancellation-release checks passed. |
| S-10 | **PASS at L1:** evidence redaction and payload rejection checks passed. |
| S-11 | **Not applicable:** LAB-04 has no server. |
| S-12 | **PASS at L1 / observed:** manifests preserve `server.path=none` and service not configured. |
| S-13 | **Observed L2 current-source binding:** both captures bind matching source, fixture, build ID, sdkconfig, binary, and fingerprint. |
| S-14 | **PASS at L1 and observed L2:** workflow and experiment stay distinct; both new manifests and reparses report PASS for each. |
| S-15 | **Current-source hardware timing observations captured; approval pending:** series are recorded above; no budget or performance PASS is asserted. |
| S-16 | **PASS at L1:** README calls resource values provisional probes, not approved budgets. |
| S-17 | **Not applicable:** no user-stage owner is involved. |
| S-18 | **PASS by scoped path review:** product bootstrap paths unchanged. |
| S-19 | **PASS by scoped path review:** POC archive paths unchanged. |
| S-20 | **PASS:** report, index, and review evidence are present. |
| S-21 | **PASS at L1:** backend and variant selection checks passed in HOST verification. |
| S-22 | **PASS at L1:** exploratory-vs-decided verdict regression checks passed in HOST verification. |

This refresh reparsed current captures, reconciled current source/build identities, and recorded durable SHA-256 values for every raw capture artifact, resolving the prior report's raw-hash audit gap. Implementer checks: `.venv/bin/python -m unittest scripts.tests.test_lab04_host scripts.tests.test_lab04_model` — 36 passed; `.venv/bin/python scripts/lab_registry.py validate` — passed; `make config.labs`, `make lab.help`, and `make lab.lab04.help` — passed; `git diff --check` — passed. `make test` first failed in the sandbox with 15 unrelated LAB-02/LAB-03 localhost-bind `PermissionError`s; rerun with loopback permission passed, 318 tests in 103.642 seconds. That suite created unrelated LAB-02 test evidence under the preexisting untracked area; it was left untouched. Registered variant builds were passed in the preceding independent HOST qualification and their actual artifacts were rehashed/reconciled in this refresh. The independent QUAL verifier must rerun the package matrix. No original LAB-04 capture directory or serial file was modified.

**OPEN decisions:** `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain OPEN. Related wire-color, legacy compatibility, checkpoint, seek, cache, and performance choices remain OPEN. No format or performance approval is recorded.

**Pending device criteria:**

- Independent DEVICE adjudication of current-source captures and applicable L2/L3 rows; L04-DEVICE remains unselected and has no pass here.
- End-to-end trace-loss determination across firmware publication, UART, serial capture, and parsing; publication queue `dropped_events=0` is insufficient.
- L04-07 synchronized simultaneous-peak evidence with Ledger A and Ledger B attribution while the decoded drawing and live 28,800-byte image overlap at the relevant lifecycle peak.
- Owner-approved performance budgets before interpreting current L04-06 timing percentiles as a performance verdict.
- Visual/startup-guide assertions requiring screenshots or explicit operator observations remain unclaimed by these serial traces.

### Standards review

The read-only client standards review, source fingerprints, applicable-specification matrix, findings, repair mapping, and residual limits are in [the current-captures standards review](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/standards-review.md). It found the stale unbound-capture summary and missing durable raw hashes to be confirmed in-scope report defects. This refresh updates the status and records every raw artifact hash. No client source was changed and no OPEN product choice or unproven peak/performance claim was converted into a decision.


## Prior QUAL-SUPPLEMENT-001 history and immutable captures

The previous qualification cycle recorded `LAB04-QUAL-SUP-001`, a confirmed stale-summary framing defect, and corrected the top-level report for the evidence contract then in effect. Its original read-only client standards review remains unchanged at [the QUAL-SUPPLEMENT review](reviews/20261019-LAB04-QUAL-SUPPLEMENT-001/standards-review.md). A subsequent QUAL attempt found that it referred to raw capture hash checks without recording the values; that audit gap is now closed for both the new current-source runs and the earlier pre-current-source runs. The prior manual `20261019-manual-run-243e8112` and scripted `20261019-scripted-run-4587ca24` artifacts remain immutable and unbound (their source/build identity fields were null). Their manifest, events, metrics, and serial-log byte lengths and SHA-256 values are preserved in the `prior_qual_supplement_captures` section of [the current raw artifact hash ledger](reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/raw-artifact-hashes.json). Those older PASS verdicts are not overwritten, and they are not attributed to current firmware.

## Historical report snapshot — 20261019-LAB04-QUAL-DEVICE-RECUT-001

The sections below preserve the prior report snapshot for audit history. Their source fingerprints, parser behavior, and capture interpretation describe the prior QUAL recut and are superseded by the current summary above.

**Historical status: software-qualified at L0/L1 with observed L2 manual and scripted workflow/experiment PASS; raw timing and heap observations were available, while L3/DEVICE qualification remained incomplete and performance remained UNMEASURED.**

## Identity and scope

| Item | Value |
|---|---|
| Lab version | `LAB-04/v1 = firmware lab04-fw-v1 + server none + fixture lab04-sketch-format/v1` |
| Target | ESP32-S3 / Box-3 BSP; ESP-IDF 5.4.2 |
| Source baseline | HEAD `a101f504907fc27a8862f321e1502c789f5f5c23`; implementation is the selected uncommitted LAB-04 package work |
| Controlling plan | `docs/labs/remaining-labs-implementation-plan.md`, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b` |
| Fixture | `lab04-sketch-format/v1`; Corpus A `short.sketch` 128 bytes, SHA-256 `d2efa7e48bcb2eb021211d208acd231a05919edfb00d783dd9221720771d12ec`; `dense.sketch` 40,968 bytes, SHA-256 `1bac193a2482c8a4f743d1bf6ada17080c87ca2d58f7964fb20d481664c50985` |
| Build outputs | `build/labs/lab-04-sketch-format-manual` and `build/labs/lab-04-sketch-format-scripted` (distinct). The scripted sdkconfig has `CONFIG_LAB04_SCRIPTED=y`; manual does not set it. |
| Current source fingerprints | `main/app_main.cpp` SHA-256 `1bd2df8e6fd91035a193dfd2f15403cdb57fa0b54598801dc6b53074dbd5296f`; `main/preview_accounting.hpp` `72b19ed12dac01d747fbe960ba2ca3493a987ac008f566150002ed146cf5f91f`; parser `9ecc6ae260da55d5a2cada37c124914cac4774fd3bf03494998bbae0381c2558`; trace schema `125c94daafaee3ca2f5c2180797a896fbf1a82c761e5e3cebd2413ef0638e7dc`; adapter `8d68f917fc8fb78b4464d9777d8aa8b92115bb2328dae967a186d03765039b73`; README `f44bca4a1bb820a4bffc80f9eddb8de8d61767c0a9f65ddcfae210ce9399e307`; host test `a6ea1d4b557ffde56e54ed46c169953c243279cbb9e9dae52e00a8ef8f0b6719`; model test `af46ea2a471730c229cae474df3330e9319f2a1ff31bac34c97f5c1bfd1fea15`. |
| Current registered build artifact fingerprints | Manual binary SHA-256 `1f2712bbf2c35a96eea3cd4bf5ba4b52165f8f8fb7c44b2edb115e7a41058fb4`; scripted binary SHA-256 `2146d44e60130d2b2ff02a0b9af6efe2a22402bfdc44d515b4720bb4b7522815`. These are current successful registered builds; run manifests do not bind them to the captures. |
| Preserved target runs | Earlier captures remain unchanged: [manual](runs/20261019-manual-run-a28075c4/run-manifest.json), [scripted](runs/20261019-scripted-run-167d48e9/run-manifest.json). This recut adds [manual](runs/20261019-manual-run-243e8112/run-manifest.json) and [scripted](runs/20261019-scripted-run-4587ca24/run-manifest.json), each with events, original metrics, and serial-log references. New manifests report workflow `PASS`, experiment `PASS`, parser `parsed`, 35 trace rows, and harness complete. Their source/build/fixture identity fields are null; firmware trace loss is `UNKNOWN`; current-parser derivatives remain evidence `INCOMPLETE`. |

This lab compares timed colored points, per-stroke color records, and segment records with a final-image preview. Checkpoints, legacy handling, encoded size, and performance remain exploratory. This report makes no LAB-03 playback parity claim.

## Measured, triggers, and decision

The two new target captures report workflow `PASS` and decided-input experiment `PASS` for `F-malformed`, `F-overflow`, and `F-cancel`; exploratory observations remain separate. Each has 35 trace rows and `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0`. Current-parser reparses are saved as post-hoc derivatives in the [new QUAL recut review](reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001/standards-review.md). Both include `preview_bytes`/`preview_buffers` and all four bounded raster counters on 35/35 rows. Eight rows per run report one 28,800-byte preview; eight rows have eligible raster work, with a 64-work cap. This closes the earlier field-presence gap for C-1/C-3 in the raw trace, but null run source/build identities prevent attribution to the corrected current source or binaries.

The reparsed traces also provide hardware-provenance timing series and internal, PSRAM, and LVGL largest-free-block readings. For example, manual/scripted `raster_us` has 8 eligible samples each (p50 122/70 us; p95 3818/1851 us); `encode_us`, `decode_us`, and `seek_us` are also summarized in the derived metrics. These are measured observations, with exact percentiles in the review outputs, not budget approvals. The parser preserves `evidence.status=INCOMPLETE`, `trace_loss=UNKNOWN`, and `firmware_trace_loss=UNKNOWN`. `live_buffers` is zero on all rows, so the heap readings do not establish L04-07's required simultaneous decode-plus-live-image peak. The values 28,800 bytes (one 120×120 RGB565 image) and 64 raster work units are implementation counters/probe sizes, not approved performance or product budgets. No current-source validation or accepted DEVICE verdict follows from these unbound captures.

All format, preview, checkpoint, seek, cache, palette-wire, and legacy decisions remain **OPEN**. In particular:

```text
OPEN-SKP-03: OPEN
COM-OPEN-002: OPEN
```

The original and derived `metrics.json` files have no `selected_format` key. The post-hoc derivatives are not replacements for original run artifacts. Measured timing/heap observations now exist, while qualification against an approved threshold remains unmeasured.

## Verification levels

| Level | Disposition | Evidence |
|---|---|---|
| L0 — project/build | Achieved | Both registered ESP-IDF builds completed successfully in their distinct directories. |
| L1 — model, parser, adapter, regression | Achieved | At this preserved prior snapshot: native LAB-04 model and failure-publication check; 24 focused LAB-04 host/model tests; registry/help checks; root suite (306 tests). Superseded final-state checks passed 39 focused HOST/MODEL tests and 321 root tests in 105.301 seconds. |
| L2 — target workflow | Observed PASS for manual and scripted | New post-reset captures include the complete harness marker, parsed trace, successful cleanup, and no-server manifest fields. Exact decided inputs reproduce PASS. C-1/C-3 fields are present, but source/build identity is unbound. |
| L3 — target timing/resources | Candidate raw measurements present; DEVICE qualification incomplete | Current-parser derivatives include timing percentiles, heap readings, and raster eligibility/work counters. Source/build binding and L04-DEVICE independent verification are absent; simultaneous decode-plus-live-image peak is not established. Performance stays UNMEASURED. |

## Standards review findings

A fresh read-only client standards review assigned these stable IDs. Each confirmed in-scope finding was addressed in this QUAL implementation and covered by focused checks:

| Finding | Fix | Check |
|---|---|---|
| LAB04-F001 — scripted completion could pass while decided variant repetitions were absent | Parser now checks the required per-scenario variant inventory before it can classify a decided input as complete. Missing rows produce `INCOMPLETE`; invalid variants or complete inventories with wrong terminals cannot pass. | `test_decided_inventory_is_required_for_pass_and_wrong_variants_fail_closed`; full focused host suite. |
| LAB04-F002 — adapter asserted firmware trace loss was observed without device evidence | Adapter copies the parser's actual firmware-loss fields. The parser reports `UNKNOWN` and null counts when no device loss row exists; an explicit parsed loss row is retained as observed. | `test_reported_firmware_loss_is_parsed`; successful scripted adapter path and absent-loss envelope assertion. |
| LAB04-F003 — v2 metrics omitted the shared evidence envelope | Metrics include `schema_version`, `parser_version`, `evidence.status`, `trace_loss`, `firmware_trace_loss`, and `missing`. Unknown loss and absent hardware measurements keep evidence `INCOMPLETE`; no missing value is converted to zero. | `test_scripted_success_and_exact_experiment_inputs`; focused parser tests for absent and reported loss. |
| LAB04-F004 — cross-task `volatile bool` failure flag was unsynchronized | All worker, app-task, and callback publications use a sticky `std::atomic<bool>` latch with release stores and acquire loads. Failure still gates completion and emits the failed terminal marker. | Native `failure_publication_test.cpp` launches concurrent publishers and checks the sticky read; both ESP-IDF variants compile the integration. |
| LAB04-F005 — callbacks could reach an absent or deleted worker queue during startup/cleanup | Queues, publication channel, preview memory, and worker are initialized before controls are exposed. Teardown takes the BSP display lock, which the pinned LVGL port also holds around input reads and `lv_timer_handler`; this drains an in-flight callback. It removes both queue callbacks and makes controls non-clickable before queue deletion. Initialization failures occur before control exposure or follow the same drain/remove/stop/cleanup ordering. | Focused source lifecycle checks; inspected `bsp_display_lock` delegation and the `lvgl_port` timer/input critical section in the pinned managed component; both registered ESP-IDF variants build. Target concurrency remains unmeasured. |
| LAB04-F006 — queue-full reporting performed console I/O in LVGL callbacks | Callbacks now only update a sticky failure latch and atomic queue-overflow counter. The app task emits the overflow evidence and failed harness marker. | Focused callback source check proves no `printf`/`puts` in queue callbacks; native concurrent publisher test checks sticky failure and overflow count; both registered builds compile the app-task reporter. |
| LAB04-F007 — preview accounting could report a false count (C-1) | Standards review identified that preview accounting could count an uncompleted/invalid operation as rendered. The emitted count must reflect completed preview work. | Source fix in `app_main.cpp` and `preview_accounting.hpp`; fresh L1 focused verification passed. New runs contain coherent `preview_bytes`/`preview_buffers` values (8 rows at one 28,800-byte buffer, remaining rows zero), closing field-presence confirmation. Manifest source/build identity is null, so attribution to the fix remains unverified. |
| LAB04-F008 — bounded raster time omitted work counts (C-3) | Standards review found bounded raster duration lacked eligible/rendered/remaining work counts. The trace must expose bounded-work accounting needed to interpret elapsed time. | Source fix emits bounded raster counters; fresh L1 focused verification passed. New runs contain counters on 35/35 rows and coherent cap/eligible/rendered/remaining totals; source/build identity is unbound, so attribution remains unverified. |
| LAB04-F009 — parser lacked nearest-rank summaries/provenance and rejected legacy captures (C-2) | Standards review found absent nearest-rank summaries and explicit provenance, plus incompatibility with successful legacy captures. Parser must preserve older captures, state provenance, and withhold ineligible raster timing. | Parser now emits nearest-rank summaries and provenance, accepts the legacy captures, and marks raster accounting missing with zero eligible raster samples. Fresh L1 verification passed; fresh read-only reparses of both preserved serial files passed. |

Residual risk, not a confirmed review finding: model/vector/string allocation failures may not always produce a terminal event. There is no allocator fault injection or target evidence for this path. It remains an evidence gap; no speculative architecture or policy change was made.

## Acceptance ledger

| Acceptance ID | Owner | Required level | Implementation / actual check | Achieved level | Disposition |
|---|---|---:|---|---:|---|
| S-01 | L04-FW | L0 | LAB-04 ESP-IDF project; `make lab.lab04.build`; `make lab.lab04.scripted.build` | L0 | Achieved; both builds passed. |
| S-02 | L04-MODEL | L1 | `manifest.lab.json`; `test_manifest_trace_and_scenario_contract` | L1 | Achieved. |
| S-03 | L04-HOST | L1 | Registry parser path and trace fixture tests in `test_lab04_host.py` | L1 | Achieved; v2 document and run ID are asserted. |
| S-04 | L04-HOST | L1 | `.venv/bin/python scripts/lab_registry.py validate`; `make lab.help` | L1 | Achieved; registry validates and lists `lab04`, `LAB-04`, manual/scripted variants. |
| S-05 | L04-HOST | L1 | `make lab.lab04.help` | L1 | Achieved; operator instructions and 30-second default printed. |
| S-06 | L04-HOST | L0 | Both registered builds; inspect the two build directories and sdkconfigs | L0 | Achieved; directories differ and only scripted sets `CONFIG_LAB04_SCRIPTED=y`. |
| S-07 | L04-FW / HOST | L1 + L2 | Firmware fail-closed init/completion paths; host reset-marker tests; new manifests, events, and serial captures | L2 observed | Both post-reset captures parsed 35 rows, recorded a complete harness marker, and report workflow PASS. C-1/C-3 fields are present; run source/build binding is absent. |
| S-08 | L04-MODEL | L1 | `test_lab04_roundtrip_and_fault_contracts` | L1 | Achieved; queue overflow is explicit, queued count is preserved, and the native model test passes. |
| S-09 | L04-MODEL | L1 | Native operation-envelope assertions; focused model suite | L1 | Achieved for model operations; device concurrency remains pending under S-07/L04-05. |
| S-10 | L04-HOST | L1 | `test_redact_line_redacts_url_password_and_bearer`; sensitive serial pre-write tests | L1 | Achieved; raw PIN/media payload is rejected before evidence write. |
| S-11 | L04-HOST | L1 + L2 | `manifest.lab.json` declares `server_path: none` | — | Not applicable: LAB-04 has no server fixture. |
| S-12 | L04-HOST | L2 | Adapter no-server path and preserved run manifests | L2 observed | Both manifests record service/scenario `not_configured`; workflow and experiment verdicts remain separate. |
| S-13 | L04-HOST / DEVICE | L1 vs L3 | Current-parser reparse of raw serial with explicit post-reset context; manifest firmware identity | L2 provenance observed; L3 attribution pending | Hardware provenance is derived from `device_serial_capture` context for `lynn-kit`. Timing/resource samples now exist, but manifest source/build identity is null; source binding and DEVICE verification remain pending. |
| S-14 | L04-HOST / DEVICE | L2 | New manifests, event journals, serial captures; adapter keeps workflow and experiment separate | L2 observed | Both runs report workflow `PASS`, experiment `PASS`, parser parsed, post-reset capture, and cleanup passed. |
| S-15 | L04-DEVICE | L3 | Post-hoc metrics with timing percentiles and internal/PSRAM/LVGL largest-free-block series | Candidate raw samples present; DEVICE verification pending | Real hardware-provenance samples are recorded in both derivatives. Performance verdict remains `UNMEASURED` because no approved budget exists; this QUAL refresh does not pass DEVICE. |
| S-16 | L04-MODEL | L1 | LAB-04 README `ResourceBudget` | L1 | Achieved; values are explicitly labeled provisional lab probes. |
| S-17 | L04-MODEL | L1 | LAB-04 has no user session stages or stage writer | — | Not applicable. |
| S-18 | All packages | L0 | Scoped change review; no `client/device/` or bootstrap paths changed | L0 | Achieved. |
| S-19 | All packages | L0 | Scoped change review; no `poc-v1/` paths changed | L0 | Achieved. |
| S-20 | L04-QUAL | L2 | This report and [evidence index](../README.md) | L2 | Achieved as the required qualification artifact; does not mean L2 target execution passed. |
| S-21 | L04-HOST | L1 | `test_backend_and_variant_mismatch_reject_before_io` | L1 | Achieved; scripted default selection is asserted, unsupported backend rejects before I/O, and workflow is not passed. |
| S-22 | L04-HOST | L1 | `test_exploratory_rows_never_change_decided_verdict` | L1 | Achieved; only decided inputs affect verdict and exploratory observations remain available. |
| L04-01 | L04-HOST / QUAL | L0 | Both registered make builds | L0 | Achieved; exit 0 and distinct build directories. |
| L04-02 | L04-MODEL | L1 | `test_lab04_roundtrip_and_fault_contracts` | L1 | Achieved; all three encodings reproduce Corpus B geometry, order, colors and pen-up boundaries. Does not set an experiment verdict. |
| L04-03 | L04-MODEL | L1 | `test_lab04_preview_bytes` | L1 | Achieved; one preview buffer is at most 28,800 bytes by arithmetic only. |
| L04-04 | L04-MODEL | L1 | `test_lab04_no_silent_truncate` | L1 | Achieved; oversized input fails rather than succeeding with shorter geometry. |
| L04-05 | L04-FW / HOST | L2 | Reset-gated marker, cleanup, and new manual/scripted runs | L2 observed | Both target manifests/logs show complete post-reset captures and successful cleanup. C-1/C-3 fields are present; source/build binding is still pending. |
| L04-06 | L04-DEVICE | L3 | `encode_us`, `decode_us`, `raster_us`, `seek_us` with eligible work counts | Candidate raw samples present; DEVICE verification pending | Both derivatives report nonempty timing percentiles; `raster_us` has 8 eligible samples and counters on 35/35 rows. Performance remains UNMEASURED; no DEVICE pass is claimed. |
| L04-07 | L04-DEVICE | L3 | Internal, PSRAM and LVGL largest-free-block at simultaneous decode plus live 28,800-byte image peak | Candidate heap samples present; DEVICE verification pending | All three series are present in both derivatives, but simultaneous peak is not demonstrated (`live_buffers=0` throughout) and no independent DEVICE verification is selected. |
| L04-08 | L04-QUAL | L1 | This report preserves the exact OPEN lines; focused tests assert no `selected_format` | L1 | Achieved. Original and new post-hoc metrics contain no selected-format decision. |
| L04-09 | L04-HOST / QUAL | L2 | Current-parser reparses and new run manifests for decided inputs | L2 observed | Manual and scripted experiment verdicts reproduce `PASS` for exactly `F-malformed`, `F-overflow`, `F-cancel`; exploratory scenarios remain exploratory and excluded from inputs. This does not imply performance PASS. |
| L04-10 | L04-MODEL | L1 | `test_lab04_cancel` and native model executable | L1 | Achieved; one cancelled operation, `terminal_count=1`, `live_buffers=0`; duplicate terminal fails. |
| L04-11 | L04-MODEL | L1 | `test_lab04_corpus_hashes` | L1 | Achieved; copied Corpus A hashes and byte lengths match the source manifest. |

## Commands and outcomes

| Command | Result | Evidence / notes |
|---|---|---|
| `.venv/bin/python -m unittest scripts.tests.test_lab04_host scripts.tests.test_lab04_model` | Passed | Final focused result: 39 HOST/MODEL tests passed, including parser, provenance/raster, queue lifecycle, callback I/O, inventory and adapter checks. |
| Current-parser reparse of both new raw captures | Passed | Exact invocation and capture context are recorded in the [new QUAL recut review](reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001/standards-review.md); outputs are [manual metrics](reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001/20261019-manual-run-243e8112-posthoc-metrics.json) and [scripted metrics](reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001/20261019-scripted-run-4587ca24-posthoc-metrics.json). Both parsed 35 raw serial rows and reproduced exact decided-input PASS; metrics preserve evidence `INCOMPLETE` and performance `UNMEASURED`. |
| `.venv/bin/python scripts/lab_registry.py validate` | Passed | `valid: config/labs.registry.yaml`. |
| `make config.labs` | Passed | `family-link.labs-registry.snapshot/v1` includes `lab04`. |
| `make lab.help` | Passed | Lists LAB-04 and both variant keys. |
| `make lab.lab04.help` | Passed | Operator help prints. |
| `make lab.lab04.build` | Passed | ESP-IDF 5.4.2; `build/labs/lab-04-sketch-format-manual`; current binary SHA-256 is recorded above. Initial sandboxed attempt was denied by macOS process enumeration; retry passed with process enumeration permission. |
| `make lab.lab04.scripted.build` | Passed | ESP-IDF 5.4.2; `build/labs/lab-04-sketch-format-scripted`; current binary SHA-256 is recorded above. |
| `make test` | Passed | Final root suite: 321 tests in 105.301 seconds. The initial sandbox run failed on 15 localhost-bind permission errors; the permission-enabled rerun passed. |
| `git diff --check` | Passed | No whitespace errors at the time of the check. |

The two captures were supplied as target evidence; no device action was performed in this package. The final-state independent checks supersede the earlier 306-test root result: 39 focused HOST/MODEL tests passed, and root `make test` passed 321 tests in 105.301 seconds after the initial sandbox run encountered 15 localhost-bind permission errors. Current-parser derivatives were written only under the new review directory; original run artifacts were preserved. Current source and build hashes were recomputed for reference and are not attributed to the runs because their manifest identity fields are null.

## Pending target work

L04-DEVICE remains unselected. A future DEVICE verification can use these raw captures as candidate evidence, but must bind source/build identity, reconcile firmware trace-loss evidence, establish the simultaneous decode-plus-live-image heap peak for L04-07, and independently verify all required L2/L3 claims. L04-06 timing samples and C-3 raster counters are present in the new traces; no timing budget is approved, so performance remains **UNMEASURED**. The exact OPEN lines remain:

```text
OPEN-SKP-01: OPEN
OPEN-SKP-03: OPEN
COM-OPEN-002: OPEN
```
