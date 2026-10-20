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

Before this change, several callers removed their run directory only after
assertions, so an assertion failure could leave synthetic evidence behind.
`test_preflight_failure_writes_partial_manifest_once` and
`test_unsafe_configured_log_root_fails_before_target_or_flash` had the same
failure-path gap. `test_managed_http_runs_server_build_flash_capture_reset_and_manifest`
and the local/eager shared-path test already use `finally` cleanup; that
behavior is retained.

The generic runner unit tests in `scripts/tests/test_lab_runner.py` already use
`TemporaryDirectory` roots. Evidence tests in `test_lab02_evidence.py` also use
temporary roots. LAB-03, LAB-04, LAB-06, and LAB-10 host/model tests likewise
write fixtures and generated evidence under temporary roots or class-scoped
`TemporaryDirectory` instances. Preserve those isolation patterns; LAB-06
adapter temporary roots now also get explicit `addCleanup` callbacks. The two
LAB-02 CLI tests that route through the generic runner now patch `lab_host.ROOT`
to a per-test temporary directory as well, so their synthetic run envelopes
cannot land in the repository.

## Attribution and preservation boundaries

The three dated directories named `20261020-phaseb-run-ce049047`,
`20261020-eager-run-caf8b4b3`, and
`20261020-scripted-phaseb-run-faf572aa` do not use the explicit IDs of the
canonical-run unit tests below. The user identified them as output from their
`make test` run, and their manifests matched the generic runner's
`<date>-<variant>-run-<hash>` format. They were removed after that provenance
was established. Do not clean other dated directories by prefix alone.

Likewise, preserve `docs/labs/evidence/lab-04/agent-run-accounting.md` as a
protected baseline. The LAB-04 run
`20261020-manual-run-973343bc` has a post-reset device capture and PASS/PASS
verdicts, so it is a real lab run rather than a synthetic unittest artifact.
Neither is in scope for test cleanup.

## Implemented cleanup

`CanonicalRunTests` registers exact evidence and serial-log directories with
`TestCase.addCleanup` before each synthetic run starts. The helper covers
`_local_run` and both direct coordinator tests that allocate canonical
evidence. Existing `finally` cleanup for managed-HTTP and local/eager cases is
preserved. Lab 06 adapter tests now register each temporary root for cleanup,
including roots holding partial captures and evidence fixtures.

Cleanup is limited to the current test's run ID or temporary root; it never
glob-deletes evidence. Assertions can inspect the artifacts before unittest
runs the registered cleanup callbacks. No test output needs to remain in the
working repository, so a broad `make test-clean` target is unnecessary and
would risk deleting real lab evidence.

The three dated run directories named above were identified by the owner as
artifacts of the test run and removed. No corresponding serial-log directories
existed. Other existing LAB-02 and LAB-04 evidence remains out of scope.
The current LAB-04 and LAB-06 dated runs are tracked device-capture evidence
with serial logs and build identities; they are not outputs of the unit-test
fixtures and were preserved. No other untracked test-run evidence directories
were present during that audit. The later directories
`20261020-eager-run-fc108050`, `20261020-phaseb-run-46dc44a9`, and
`20261020-scripted-phaseb-run-3fb97d87` each contained only a PASS envelope
and terminal event with no build or serial artifacts; they came from those
LAB-02 CLI test doubles and were removed after redirecting their test roots.

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
