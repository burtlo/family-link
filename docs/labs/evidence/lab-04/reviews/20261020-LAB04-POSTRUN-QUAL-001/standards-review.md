# LAB-04 client standards review — 20261020-LAB04-POSTRUN-QUAL-001

## Review provenance and bounded scope

- Package: `L04-QUAL` after the independently verified `L04-HOST` parser repair.
- Read-only review basis: HEAD `a101f504907fc27a8862f321e1502c789f5f5c23`; implementation map SHA-256 `22384571a65773284e391fe668e28a1bc9fc1acc5f27f5050b4abf923dc12e3b`.
- Source identity recorded in the captures: project tree `4367754de387636e00a89bb47a9cd4b6f01d061fbeda478156b2c98d351bb8a9`; fixture tree `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`.
- Current project-tree identity after the HOST parser/test change: `8017f2b39f1285e370cb7de3f26eda59eb5beddb7fbd4f3d85aefc6934564bfc`; fixture tree remains unchanged. The source hash is a project-tree hash that includes the host parser. Only host parser/test files changed after capture; firmware compiled sources/defaults did not. Captured build ID, sdkconfig, and binary hashes match current build outputs. Recomputed aggregate build fingerprints differ because their input includes the changed project-tree source hash; the manifests correctly retain the fingerprint calculated at capture time.
- Scope: LAB-04 report, evidence index, current-parser derivative metrics, raw artifact hash ledger, and this review. All raw run artifacts remain immutable.

## Findings and disposition

| ID | Severity | Finding | Outcome / evidence |
|---|---|---|---|
| LAB04-POSTRUN-F001 | P2 | The pre-repair parser classified aggregate `trace_loss` as `NONE` while firmware trace loss was `UNKNOWN`. | Fixed in HOST. Fresh independent verifier reparsed both preserved serial logs using the current parser in temporary output and confirmed aggregate and firmware loss are `UNKNOWN`; sequence reconciliation is `COMPLETE`. The prior runner metrics remain unchanged and their erroneous aggregate classification is explicitly identified in the report. Current-parser derivatives are saved beside this review. |
| LAB04-POSTRUN-F002 | P2 | Report summary and evidence-index row said there was no current-source target run after two new runs had completed. | Fixed in this QUAL pass. Report and index now cite both current-build manual/scripted runs, preserve separate workflow and experiment verdicts, state exact remaining DEVICE criteria, and link the raw hash ledger and derivatives. Raw manifest facts and parser outputs were cross-checked. |

No additional confirmed queue, callback, or cleanup defect was found in this evidence review.

## Capture reconciliation

| Run | Raw manifest verdicts | Current parser | Trace and scenario evidence |
|---|---|---|---|
| `20261020-manual-run-99f0de33` | workflow `PASS`; experiment `PASS`; post-reset; cleanup/parser passed | experiment `PASS`; performance `UNMEASURED`; aggregate trace loss `UNKNOWN`; firmware trace loss `UNKNOWN` | 39 sequenced rows reconcile to footer (`expected_rows=39`), no gaps/duplicates/out-of-range; 35 operation rows; exact decided inputs `F-malformed`, `F-overflow`, `F-cancel`; 2 resource samples and 2 decoder reservation observations. |
| `20261020-scripted-run-047a3247` | workflow `PASS`; experiment `PASS`; post-reset; cleanup/parser passed | experiment `PASS`; performance `UNMEASURED`; aggregate trace loss `UNKNOWN`; firmware trace loss `UNKNOWN` | Same reconciliation and scenario inventory as manual; 2 resource samples and 2 decoder reservation observations. |

Both serial captures contain the expected ready and complete harness markers. Neither contains stack-overflow, panic, abort, or reboot markers. The previous failed manual run `20261020-manual-run-87109e20` remains preserved as historical evidence for the earlier 3,584-byte stack configuration; the current captures use the repaired build and do not reproduce that failure. This prior operator-visible failure was not caused by user error.

The capture-time source hash is `4367754d…bb8a9`, while current project source hash is `8017f2b3…64bfc`. Manual and scripted binaries are respectively `187704014b5e3f02ab4f29587f387e51b69f214c6c8ca418b214bdeed0a7c409` and `234c6c69c7cf5e485a3152d9eef0f222f700920e9e36b9a0f9132ea9593f82e9`; sdkconfig hashes are respectively `d381908414a53ba2b3dab2a64488a0a7f0ef6a21cd6cd70cf965585452ccb3ec` and `bb599926bda166b4ed7710ff62a7a7314727fc24a7704350d2592752935c4747`. They match the current registered build outputs. Current recomputed fingerprints are `sha256:13cdc83482231bfbce9a74f75f2dee6fb44333e272cc59dbc647bcd013fa7262` and `sha256:e591baabecc6ac91f7a5d39b5211a6bb2b38890059a30746008b9df02865cd9c`; capture fingerprints remain in the manifests and are not rewritten.

## Measurement and qualification limits

Each run has two `resource_sample` events and two `resource_peak` events. The peaks are `decoder_maximum_output_capacity_reservation`; `whole_system_temporal_peak=false`. They do not establish the simultaneous system lifecycle peak or satisfy L04-07. Raster counters are present on 35/35 operation rows. Hardware timing samples exist (encode 9, decode 8, raster 8, seek 9 per run); detailed nearest-rank values are in the current-parser derivative metrics and raw original metrics. No owner-approved timing budget exists, so performance stays `UNMEASURED`.

The parser's `trace_reconciliation.status=COMPLETE` describes row/footer and sequence reconciliation. It does not mean the end-to-end trace is proven loss-free: aggregate and firmware trace-loss status remain `UNKNOWN`, with null device loss counts. Firmware publication queue drops are zero and host-journal drops are zero, but those observations cannot prove no loss across firmware, UART, capture, and parsing. The parser evidence envelope reports `COMPLETE` while exposing this unknown loss field; the report does not infer loss-free capture from that envelope.

No visual capture was supplied. L04-DEVICE was not selected. `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain open. Residual gaps include unmeasured task stack high-water, L04-07 synchronized simultaneous-peak attribution, independent DEVICE adjudication, end-to-end trace loss, and performance budget approval.

## Checks performed for this review

- Current parser reparsed each original serial log to a new derivative under this review directory; no raw run file was written.
- Raw SHA-256 and byte lengths were computed for manifest, events, original runner metrics, and serial capture for both runs; ledger saved as `raw-artifact-hashes.json`.
- Capture manifest source/fixture/build identities and current manual/scripted binary/config identities were compared; fixture, build ID, sdkconfig, and binary matched. Source-tree hash difference is attributed to the host parser/test change, and aggregate build fingerprint difference is disclosed above.
- Serial logs were searched for stack overflow, panic, abort, and reboot markers; none found.
- Independent L04-HOST verifier reported PASS: 28 tests passed, current parser reparses of both runs, and all eight raw artifact hashes unchanged.
- This QUAL report/index refresh does not rerun the full implementation suite; earlier package checks remain linked from prior report history. No hardware action was performed by this review.

## Final-state package checks

The later independent final-state checks passed after the documentation refresh. The focused HOST/MODEL command passed 39 tests. Root `make test` passed 321 tests in 105.301 seconds. Its initial sandbox run failed on 15 localhost-bind permission errors; the permission-enabled rerun passed. These results supersede the earlier 31-focused/306-root counts in the report's package check table; no parser or source changes were part of this count correction.
