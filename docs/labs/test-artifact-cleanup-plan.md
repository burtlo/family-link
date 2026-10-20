# Test artifact cleanup plan

## Goal

Keep synthetic run evidence created by the unittest suite available while each
assertion is checked, then remove only those test-owned artifacts when the test
finishes. Do not remove real lab captures, qualification reports, or files left
by other agents.

## What the current suite does

`make test` runs `python -m unittest discover -s scripts/tests`.

The known LAB-02 compatibility-path artifact writer is
`scripts/tests/test_lab02_canonical_run.py`. Its `CanonicalRunTests._local_run`
helper calls `lab02_canonical_run.run_canonical` without injecting an evidence
writer, so the production `EvidenceRun` creates its normal artifacts under:

- `docs/labs/evidence/lab-02/runs/<run_id>/`
- `logs/device/labs/lab-02/<run_id>/`

These tests use fake serial and build callbacks. Their manifests, event
journals, metrics, and serial logs are test fixtures, not device evidence, and
do not need to remain after their assertions run. The helper's callers include
`test_final_manifest_separates_host_and_firmware_trace_loss`, both
`test_new_ready_marker_*` cases, `test_previous_run_build_identity_cannot_pass_as_fresh_boot`,
`test_unterminated_uart_line_is_bounded_and_preserved_as_partial_evidence`,
`test_scripted_completion_requires_expected_build_and_step_count`,
`test_serial_queue_overflow_cannot_pass_scripted_capture`,
`test_pre_reset_marker_is_logged_but_cannot_satisfy_fresh_readiness`,
`test_identity_matching_marker_from_straddling_read_cannot_satisfy_freshness`,
`test_read_started_after_reset_can_establish_fresh_readiness`,
`test_reset_exception_finalizes_and_closes_uart`, and
`test_interrupt_during_reset_finalizes_incomplete_and_closes_uart`.

Several callers remove their run directory after assertions. That cleanup is
not protected by `finally`/`TestCase.addCleanup`, so an assertion failure can
leave its synthetic evidence behind. `test_preflight_failure_writes_partial_manifest_once`
and `test_unsafe_configured_log_root_fails_before_target_or_flash` also call
the production coordinator directly and clean up after assertions, with the
same failure-path gap. `test_managed_http_runs_server_build_flash_capture_reset_and_manifest`
and the local/eager shared-path test already use `finally` cleanup and should
retain that behavior or use the common cleanup helper.

The generic runner unit tests in `scripts/tests/test_lab_runner.py` already use
`TemporaryDirectory` roots. Evidence tests in `test_lab02_evidence.py` also use
temporary roots. Preserve those isolation patterns.

## Attribution and preservation boundaries

The dated directories named like `20261020-phaseb-run-*`,
`20261020-eager-run-*`, and `20261020-scripted-phaseb-run-*` are not produced by
the run IDs or paths used by the current unittest cases above. The checked-in
tests use explicit IDs such as `w7-*`/`w9-*`, or temporary roots. Do not treat
the dated directories as disposable test output without establishing their
provenance; retain them as existing evidence.

Likewise, preserve `docs/labs/evidence/lab-04/agent-run-accounting.md` as a
protected baseline. The LAB-04 run
`20261020-manual-run-973343bc` has a post-reset device capture and PASS/PASS
verdicts, so it is a real lab run rather than a synthetic unittest artifact.
Neither is in scope for test cleanup.

## Recommended implementation

1. Add a small helper to `CanonicalRunTests` that registers exact paths with
   `self.addCleanup` for a supplied test run ID. It should remove only that
   run's LAB-02 evidence directory and serial-log directory.
2. Register cleanup in `_local_run` before invoking the coordinator. This
   covers every helper caller even if an assertion later fails.
3. Register the same cleanup immediately after assigning IDs in the two direct
   coordinator tests (`test_preflight_failure_writes_partial_manifest_once`
   and `test_unsafe_configured_log_root_fails_before_target_or_flash`). Keep
   existing `finally` blocks for managed-HTTP and local/eager runs, or migrate
   them to the helper without weakening their cleanup-on-exception behavior.
4. Never glob/delete all LAB-02 run folders or clean by date/prefix alone. The
   cleanup must be scoped to IDs created by the current test instance; user
   captures and other test runs must remain untouched.
5. Keep artifact assertions before unittest cleanup runs. This preserves full
   manifest/serial evidence during a failing assertion while preventing the
   fake run from persisting after the test completes.

The LAB-02 v1 evidence writer currently enforces its canonical evidence and log
roots. Do not loosen that production path contract solely to redirect tests.
Per-test exact-path cleanup is the narrow change. If full temporary-root
isolation is pursued later, first design an explicit test-only repository-root
injection that preserves v1 manifest path semantics and still reads the real
source/configuration inputs.

## Acceptance checks

- Run `scripts.tests.test_lab02_canonical_run` and confirm it passes.
- Force an assertion failure after a synthetic run is written and confirm
  unittest cleanup still removes only that run's evidence and serial paths.
- Confirm a pre-existing unrelated LAB-02 run survives the test.
- Run `make test`; confirm no newly created synthetic LAB-02 run directories
  remain after successful completion.
- Do not remove or rewrite existing LAB-02/LAB-04 evidence as part of this work.
