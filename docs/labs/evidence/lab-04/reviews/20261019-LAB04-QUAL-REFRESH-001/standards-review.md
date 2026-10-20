# LAB-04 client standards review refresh — 20261019-LAB04-QUAL-REFRESH-001

## Boundary and sources

This is the saved client standards review outcome for the QUAL refresh. Scope is the LAB-04 qualification report, evidence index, review artifact, and post-hoc parser derivatives. The controlling plan is `docs/labs/remaining-labs-implementation-plan.md`, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b` (read-only). The implementation baseline is HEAD `a101f504907fc27a8862f321e1502c789f5f5c23`.

Reviewed current client sources: `client/labs/lab-04-sketch-format/main/app_main.cpp`, `main/preview_accounting.hpp`, `tools/parse_lab_trace.py`, `trace-schema.json`, `README.md`; `scripts/lab_adapter_lab04.py`; and `scripts/tests/test_lab04_host.py` / `test_lab04_model.py`. Current hashes are recorded in the qualification report. Findings C-1, C-3, and C-2 map to stable report IDs LAB04-F007, F008, and F009.

The fresh manual and scripted captures are [manual](../../runs/20261019-manual-run-a28075c4/run-manifest.json) and [scripted](../../runs/20261019-scripted-run-167d48e9/run-manifest.json). Original run files and serial logs were retained. Both run manifests report workflow `PASS`, experiment `PASS`, parser `parsed`, and `firmware_trace_loss=UNKNOWN`; source and build identity fields are null. The runs predate the C-1 and C-3 source fixes.

## Ownership map

| Concern | Owner | Evidence owned here |
|---|---|---|
| Preview completion accounting and bounded raster work fields | L04-FW | Firmware trace rows and registered build identities |
| Nearest-rank summaries, provenance, legacy compatibility, eligibility filtering | L04-HOST | Parser output and focused host tests |
| Decided scenario experiment verdict | L04-HOST / L04-QUAL | Run manifest and exact decided input inventory |
| Current target capture identity and resource/timing evidence | DEVICE operator | Post-reset serial, source/build-bound manifest, L3 ledgers |
| Qualifications and OPEN decisions | L04-QUAL | This report and evidence index |

## Confirmed findings and outcomes

| ID / trigger | Requirement | Fix | Verification and outcome |
|---|---|---|---|
| **LAB04-F007 / C-1:** review found preview accounting could report a false count. | Count only preview work that actually completed. | Firmware source corrected preview accounting in `app_main.cpp` / `preview_accounting.hpp`. | Fresh L1 source/focused verification passed. **Pass at L1; L2 emitted-field confirmation pending** because both target captures predate the fix. |
| **LAB04-F008 / C-3:** review found bounded raster time lacked work counts. | Trace eligible, rendered, and remaining work with the raster cap so duration can be interpreted. | Firmware emits bounded raster work counters in its trace rows. | Fresh L1 source/focused verification passed. **Pass at L1; legacy capture confirmation pending**: all 35 rows in each run lack these counters. |
| **LAB04-F009 / C-2:** review found no nearest-rank summaries or parser provenance, and parser incompatibility with legacy captures. | Summarize eligible measurements by nearest rank, identify provenance, and parse older successful captures without inventing missing values. | Parser accepts legacy rows, reports provenance and nearest-rank summaries, and excludes raster timing where work eligibility is absent. | Fresh L1 parser tests passed. Fresh read-only reparses of both preserved serial captures passed with the exact decided input set. **Pass at L1 and legacy-compatibility reparse; no performance qualification.** |

## Post-hoc derivatives

The following derived outputs were generated with the current parser. They are explicitly post-hoc and do not replace the original run metrics or update the manifests:

- [Manual post-hoc metrics](20261019-manual-run-a28075c4-posthoc-metrics.json)
- [Scripted post-hoc metrics](20261019-scripted-run-167d48e9-posthoc-metrics.json)

Reproduction command (from repository root):

```sh
.venv/bin/python - <<'PY'
import importlib.util
from pathlib import Path
root = Path.cwd()
spec = importlib.util.spec_from_file_location(
    "lab04_parser", root / "client/labs/lab-04-sketch-format/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
for mode, run_id in (("manual", "20261019-manual-run-a28075c4"),
                     ("scripted", "20261019-scripted-run-167d48e9")):
    parser.parse_file(
        root / "logs/device/labs/lab-04" / run_id / "serial.txt",
        root / "docs/labs/evidence/lab-04/reviews/20261019-LAB04-QUAL-REFRESH-001"
             / f"{run_id}-posthoc-metrics.json",
        expected_run_id=run_id, expected_mode=mode, expected_variant=mode,
        post_reset=True,
        capture_context={"kind": "device_serial_capture", "target_id": "lynn-kit",
                        "post_reset": True})
PY
```

Both outputs report 35 rows, experiment `PASS` for `F-malformed`, `F-overflow`, `F-cancel`, provenance `hardware` from the explicit capture context, evidence `INCOMPLETE`, trace loss `UNKNOWN`, and performance `UNMEASURED`. Raster work accounting is `MISSING` for 35/35 rows, and `raster_us` has zero eligible samples. No L3 performance PASS is supported.

## Residual risk and evidence gaps

- Allocation failure may not always produce a terminal event. No allocator fault injection or target evidence exists; retain as an unproven risk.
- The run manifests have null firmware source/build hashes, build ID, and IDF version. The registered current binaries have hashes in the report, but they are not bound to these capture runs.
- C-1/C-3 output fields need a fresh capture from current firmware to confirm at L2.
- L04-06 is incomplete because legacy captures lack raster eligibility/work accounting; the reparse correctly provides no eligible raster timing samples.
- L04-07 lacks verified simultaneous-peak internal, PSRAM, and LVGL largest-free-block ledgers.
- Firmware trace loss remains `UNKNOWN`; no target loss row or zero-loss proof exists.
- Keep `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` open. No selected format or product policy is inferred.

## Execution order and outcomes

1. **Prerequisites:** current L04-MODEL/FW/HOST software pass records were present before qualification work (software run 004, three fresh firmware follow-ups, HOST unchanged). Fresh read-only standards review/fixes for C-1/C-3/C-2 and fresh L1 verification were complete.
2. **Capture review:** preserved manual and scripted run manifests and logs. Both show workflow `PASS`, experiment `PASS`, parser success, post-reset capture, and cleanup success; the exact decided set is identical.
3. **Compatibility review:** reparsed both actual legacy serial files with current parser into this review directory. Both remained experiment `PASS`; missing L3 evidence remained explicit.
4. **Qualification update:** refreshed source/build fingerprints, L0/L1 result, applicable L2 ledger outcomes, L04-06/L04-07 limitations, OPEN lines, and evidence index wording.

Latest focused result recorded for this refresh: 31 host/model tests passed and both registered manual/scripted ESP-IDF builds passed. Full root `make test` is assigned to the fresh package verifier and was not run in this implementation handoff.
