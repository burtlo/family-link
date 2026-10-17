<!-- orchestrate-build:begin run_id=F82FB320-BDF8-4788-8F2E-FE4515FBBCD2 -->
# Orchestration progress

- Run ID: F82FB320-BDF8-4788-8F2E-FE4515FBBCD2
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md (SHA-256 3fe5eecd7a1bdec4f9f36d7c2724c14812a92c7fcfd98402e281170930904ae6)
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline protected paths: none visible
- Started: 2026-10-17 17:37:19 UTC
- Updated: 2026-10-17 17:54:01 UTC (P2 failed after repair cap; dependent packages not started)
- Auto-continue: true (user approved)

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | completed | pass | 1 | Fresh verifier pass after v1 root-compatibility repair; evidence/metrics suites pass. |
| P2 | Shared runner and adapter dispatch | P1 | failed | fail | 1 | After repair, verifier found actual no-server execution imports lab02_evidence from run_with_adapter; repair cap exhausted. |
| P3 | Owned service logs | P2 | not_started | not_started | 0 | Not started; P2 dependency failed. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | not_started | not_started | 0 | Not started; P2 failed and P3 is not started. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Not started; dependencies unmet. Scope decision required before handoff: agree bounded proof behavior from LAB-05 plan. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Not started; dependencies unmet. |

## Package P1: Shared evidence contracts

- Definition fingerprint: 354da25ef5073308700352be0c844b3d3f5548d0a118ec6d132682f281edf787
- Dependency evidence / waivers: none; no dependencies
- Implementer role and invocation: generic implementation agent; initial and one repair invocation completed
- Implementer report: completed; four in-scope paths changed; focused evidence and metrics suites passed (43 initially; 15 evidence tests after repair); make test ran 199 tests with 15 sandbox socket-bind errors; syntax and diff checks passed.
- Baseline and changed paths: baseline revision a60e368937594a03d711145029d2ffd82dfec18d; product paths scripts/lab02_evidence.py, scripts/tests/test_lab02_evidence.py, docs/labs/schemas/run-manifest.v2.md, docs/labs/schemas/metrics.v2.md. Orchestrator progress file excluded.
- Verifier role and fresh invocation: independent read-only verifier; first and post-repair invocations completed in fresh contexts
- Verifier verdict: pass after repair
- Findings and reproducible evidence: Initial fail reproduced EvidenceRun custom evidence_root under the v1 document ID. Repair rejects noncanonical roots for v1. Fresh verifier confirmed v1 custom-root rejection, v2 contained custom-root support, 15 evidence tests and 29 LAB-02 metrics tests passed, and diff/scope review passed.
- Repair attempts: 1 of 1; addressed only the v1 custom-root compatibility defect.
- Commit/PR/publish actions: none
- Final state: pass

## Package P2: Shared runner and adapter dispatch

- Definition fingerprint: 2f508da94acdd04529491cd3febd4b64b42a943ce1686d89da41c6c53090f978
- Dependency evidence / waivers: P1 accepted pass; fresh verifier confirmed v1 compatibility and generic v2 contracts.
- Implementer role and invocation: generic implementation agent; initial and one repair invocation completed
- Implementer report: completed; 11 P2 product paths initially changed; repair changed scripts/lab_runner.py and scripts/tests/test_lab_runner.py. Focused runner/host/registry tests passed (33 initially; 35 after repair), registry validation, Make dry-runs, import smoke, and CRLF-aware diff checks passed; full suite had 15 sandbox socket errors.
- Baseline and changed paths: P1 paths scripts/lab02_evidence.py, scripts/tests/test_lab02_evidence.py, docs/labs/schemas/run-manifest.v2.md, docs/labs/schemas/metrics.v2.md and orchestration progress remain protected. P2 paths reported: Makefile, config/labs.registry.yaml, config/labs/labs.registry.schema.v1.json, scripts/lab_host.py, scripts/lab_registry.py, scripts/lab_runner.py, scripts/lab_adapter_registry.py, scripts/lab_runner_adapter.py, scripts/lab_adapter_lab02.py, scripts/tests/test_lab_host.py, scripts/tests/test_lab_runner.py.
- Verifier role and fresh invocation: independent read-only verifier; initial and post-repair invocations completed in fresh contexts
- Verifier verdict: fail after repair
- Findings and reproducible evidence: Initial verifier found no registry-driven service/no-server mock coverage and a module-load LAB-02 evidence import. Repair added registry CLI tests and delayed import. Final verifier confirms no-server runtime still imports `GENERIC_DOCUMENT` and `EvidenceRun` from `lab02_evidence` inside `run_with_adapter`, so generic execution remains dependent on a LAB-02 module. It accepts actual LAB-02 migration as P4 scope. Focused suite (35), registry validation, Make dry-runs/help, and generic import smoke passed; runtime dependency probe failed.
- Repair attempts: 1 of 1; repaired registry-dispatch coverage and module-load import behavior, but runtime import finding remains. No further repair permitted in this run.
- Commit/PR/publish actions: none
- Final state: fail

<!-- orchestrate-build:end run_id=F82FB320-BDF8-4788-8F2E-FE4515FBBCD2 -->

<!-- orchestrate-build:begin run_id=A2C9C9A7-8088-4C3A-8CE3-CF8DD36C22E6 -->
# Orchestration progress

- Run ID: A2C9C9A7-8088-4C3A-8CE3-CF8DD36C22E6
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline paths: inherited P1/P2 work and the prior orchestration record are present. User explicitly authorized another P2 implementation; P2 in-scope paths from the prior attempt may be revised. P1 verified paths (scripts/lab02_evidence.py, scripts/tests/test_lab02_evidence.py, docs/labs/schemas/run-manifest.v2.md, docs/labs/schemas/metrics.v2.md) and this progress file are protected. No unrelated dirty paths observed.
- Started: 2026-10-17 18:15:35 UTC
- Updated: 2026-10-17 18:31:54 UTC (P3 pass recorded; P4 awaits scope-overlap direction)
- Auto-continue: true (approved in prior run; continues after each pass)
- Rerun authorization: user explicitly authorized another P2 implementation and independent verification after prior run's repair cap.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | Same map/package fingerprint; prior fresh verifier pass remains current; P2 verifier reported P1 paths protected. |
| P2 | Shared runner and adapter dispatch | P1 | completed | pass | 1 | Fresh verifier pass after adding required v2 firmware.build_fingerprint; no-server execution loads no LAB-02 modules. |
| P3 | Owned service logs | P2 | failed | fail | 1 | After repair, verifier found a late child whose wait() fails is dropped from manager ownership, preventing cleanup retry. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | not_started | not_started | 0 | Not started; P3 failed. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Not started; dependencies unmet. Resolve bounded LAB-05 proof scope before handoff. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Not started; dependencies unmet. |

## Package P2: Shared runner and adapter dispatch (rerun)

- Definition fingerprint: 2f508da94acdd04529491cd3febd4b64b42a943ce1686d89da41c6c53090f978
- Dependency evidence / waivers: P1 pass in prior run F82FB320-BDF8-4788-8F2E-FE4515FBBCD2, independently verified after repair; P2 verifier did not find P1 paths changed.
- Implementer role and invocation: fresh generic implementation agent; initial and one repair invocation completed
- Implementer report: completed; P2 initial change added standalone generic writer and runtime import check; repair added required firmware.build_fingerprint with null for unknown and populated-value coverage. Focused suites passed (30), registry/Make/help/syntax checks passed.
- Baseline and changed paths: baseline is prior P1/P2 working tree at revision a60e368937594a03d711145029d2ffd82dfec18d; P2 in-scope paths explicitly authorized for revision. P1 path mtimes (12:38–12:43 local) predate this run (13:15 local), confirming protected P1 files were not modified. Progress record remains protected.
- Verifier role and fresh invocation: fresh independent read-only verifier; initial and post-repair invocations completed
- Verifier verdict: pass after repair
- Findings and reproducible evidence: Initial verifier found missing v2 `firmware.build_fingerprint`. Repair supplies string or null; fresh verifier confirmed schema contract, 30 host/runner tests, service and no-server registry dispatch, manual/scripted envelopes, post-execution import isolation, allowlist, registry and help checks. Map and package fingerprints match; protected P1 files remained untouched.
- Repair attempts: 1 of 1; added the missing v2 contract field and tests.
- Commit/PR/publish actions: none
- Final state: pass

## Package P3: Owned service logs

- Definition fingerprint: 8bcff002937189083bf66f6db5f3036f9ec79de70ecd7afcb5fc3215239fe4e1
- Dependency evidence / waivers: P2 accepted pass in this run, freshly verified after one repair.
- Implementer role and invocation: generic implementation agent; initial and one repair invocation completed
- Implementer report: completed; added optional run-scoped log destination, concurrent stdout/stderr drain, chunk-safe redaction, byte/line caps, exit codes and event path/truncation. Repair records startup-timeout cleanup success/failure and adds late-spawn test. Focused tests and syntax passed; full server suite had 14 socket PermissionErrors.
- Baseline and changed paths: inherited P1/P2 paths and progress file protected. P3 paths scripts/lab02_server.py, scripts/server_log.py, scripts/tests/test_lab02_server.py; no unexpected paths.
- Verifier role and fresh invocation: independent read-only verifier; initial and post-repair invocations completed in fresh contexts
- Verifier verdict: fail after repair
- Findings and reproducible evidence: Initial verifier found no `server_cleanup` event after a late child was successfully reaped; repair added the event and test. Final verifier reproduced delayed Popen plus child `wait()` raising OSError: `start_error=cleanup_failed`, but the manager discarded the child handle (`manager._process=None`, `_spawn_thread=None`), so cleanup could not be retried. Bounded output, split-secret/URL redaction, truncation, exit status and owner-only cleanup checks passed. Real socket health test was unavailable under sandbox bind restrictions. Canonical caller wiring remains P4.
- Repair attempts: 1 of 1; cleanup event gap repaired, but retained ownership/retry gap remained; no further repair permitted in that run.
- Commit/PR/publish actions: none
- Final state: fail

## Package P4: LAB-02 adapter migration

- Definition fingerprint: pending capture before dispatch
- Dependency evidence / waivers: P2 and P3 failed after their repair caps; dependencies unmet.
- Implementer role and invocation: not dispatched
- Implementer report: pending
- Baseline and changed paths: unchanged; package not started because P2 and P3 failed.
- Verifier role and fresh invocation: fresh independent read-only verifier; pending
- Verifier verdict: pending
- Findings and reproducible evidence: pending
- Repair attempts: 0
- Commit/PR/publish actions: none
- Final state: not_started

<!-- orchestrate-build:end run_id=A2C9C9A7-8088-4C3A-8CE3-CF8DD36C22E6 -->

<!-- orchestrate-build:begin run_id=B09F738B-D97A-4EA2-94F6-DDFA9ADE6F5C -->
# Orchestration progress

- Run ID: B09F738B-D97A-4EA2-94F6-DDFA9ADE6F5C
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline paths: inherited P1/P2/P3 work and prior orchestration records are present. User explicitly authorized another P3 implementation; P3 in-scope paths from prior attempts may be revised. P1/P2 paths (scripts/lab02_evidence.py, scripts/tests/test_lab02_evidence.py, schema docs, Makefile, registry, host/runner/adapters, generic writer/tests), map/spec, and this progress file are protected. No unrelated dirty paths observed.
- Started: 2026-10-17 18:29:05 UTC
- Updated: 2026-10-17 19:13:47 UTC (user supplied manual/scripted kit runs; follow-up artifact verification completed)
- Auto-continue: true (approved in prior run; continues after each pass)
- Rerun authorization: user explicitly authorized another P3 implementation and independent verification after prior run's repair cap.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | Same map/package fingerprint; prior fresh verifier pass remains current. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | Fresh verifier pass in run A2C9C9A7-8088-4C3A-8CE3-CF8DD36C22E6; no-server runtime import isolation confirmed. |
| P3 | Owned service logs | P2 | completed | pass | 1 | Fresh verifier confirmed retained ownership and successful retry after a late-child wait failure. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | completed | blocked | 0 | Manual/scripted device runs confirm v2 workflow PASS and UNMEASURED verdict; scenario proof/receipt path remains unexercised. Run blocked; P5/P6 not started. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Resolve bounded LAB-05 proof scope before handoff. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | |

## Package P3: Owned service logs (rerun)

- Definition fingerprint: 8bcff002937189083bf66f6db5f3036f9ec79de70ecd7afcb5fc3215239fe4e1
- Dependency evidence / waivers: P2 pass in prior run A2C9C9A7-8088-4C3A-8CE3-CF8DD36C22E6; independent verifier accepted it.
- Implementer role and invocation: fresh generic implementation agent; initial and one repair invocation completed
- Implementer report: completed; retained late child handle when initial reap fails, recorded cleanup failure, then allowed later close retry to record success and exit status. Four focused tests and syntax/diff checks passed.
- Baseline and changed paths: baseline is prior P1/P2/P3 working tree at revision a60e368937594a03d711145029d2ffd82dfec18d; P3 in-scope files were authorized for revision. P1/P2 files and progress record remained protected.
- Verifier role and fresh invocation: fresh independent read-only verifier; initial and post-repair invocations completed
- Verifier verdict: pass after repair
- Findings and reproducible evidence: Initial run found child handle dropped after wait failure. Repair's fail-then-succeed test passed: ownership remained, cleanup=failure stayed recorded, later close cleared handle, recorded cleanup=success and exit_code=0. Bounded/redacted logs, split secrets, startup timeout outcomes and scope checks passed. Real socket health test was unavailable under sandbox restrictions.
- Repair attempts: 1 of 1; retained-ownership and cleanup-retry fix independently verified.
- Commit/PR/publish actions: none
- Final state: pass

## Package P4: LAB-02 adapter migration

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1, P2 and P3 accepted passes; P3 was independently verified after one repair.
- Implementer role and invocation: first invocation stopped before edits while awaiting scope approval; second fresh generic implementation invocation completed after user authorized overlap in `scripts/lab_adapter_lab02.py`, `config/labs.registry.yaml`, and `scripts/lab_runner.py`.
- Implementer report: completed; changed `scripts/lab_adapter_lab02.py`, `scripts/lab_runner.py`, `scripts/lab02_canonical_run.py`, and `scripts/tests/test_lab02_canonical_run.py`. Routes manual/scripted LAB-02 runs through shared v2 `EvidenceRun`, preserving canonical readiness/scenario/parser paths; build fingerprint excludes run/kit context and incorporates compiled config.
- Baseline and changed paths: previously protected P2 adapter/runner paths were explicitly authorized by the user; P4-specific changed paths are the adapter, runner, canonical coordinator, and canonical tests. Inherited P1/P2/P3 paths and generated evidence directories remain untouched by the orchestrator. No unrelated source paths changed.
- Verifier role and fresh invocation: fresh independent read-only verifier; initial host-only verification and follow-up device-artifact verification completed.
- Verifier verdict: blocked.
- Findings and reproducible evidence: Initial actual-Adapter smoke produced a v2 manifest with PASS workflow, UNMEASURED verdict, and adapter event. Follow-up inspected user-provided manual run `20261017-phaseb-run-61611444` and scripted run `20261017-scripted-phaseb-run-47d66309`. Both use v2 manifests, report workflow PASS / experiment UNMEASURED, have matching fresh LAB02_READY markers and build IDs, scripted HARNESS_DONE, non-empty referenced serial logs, metrics, host/device events, and cleanup success. Metrics correctly remain incomplete and do not promote experiment verdict. However both are nominal runs with scenario stage `not_configured`, and neither includes scenario actions or receipt/proof artifacts; therefore the required P4 scenario-proof/receipt comparison is not exercised. Their manifests also leave `artifacts.serial_log` null (the `device_log` references exist), and server exit `-15` is labeled failure before cleanup success, making stop semantics ambiguous. Focused host suite ran 103 tests with one sandbox localhost-bind error and no assertion failures; `make test` ran 212 tests with 14 sandbox socket/paging errors and no assertion failures. Scenario proof evidence remains the blocking gap.
- Repair attempts: 0; verifier verdict is blocked, not fail.
- Commit/PR/publish actions: none
- Final state: blocked

<!-- orchestrate-build:end run_id=B09F738B-D97A-4EA2-94F6-DDFA9ADE6F5C -->

<!-- orchestrate-build:begin run_id=A7E6BEE7-6D17-419D-A365-A89F1D39B2FE -->
# Orchestration progress

- Run ID: A7E6BEE7-6D17-419D-A365-A89F1D39B2FE
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline paths: inherited P1/P2/P3/P4 code and all prior run evidence/progress are present and protected except the P4 scope explicitly authorized for this rerun. User authorized LAB-02 scenario execution/implementation and independent verification. Preserve all unrelated dirty paths and existing run evidence; new scenario evidence may be added under its unique run IDs. No commits/pushes/PRs.
- Started: 2026-10-17 19:16:28 UTC
- Updated: 2026-10-17 19:18:59 UTC (both S-push attempts stopped at fixture-server startup)
- Auto-continue: true (approved in prior run)
- Rerun authorization: user explicitly authorized execution/implementation and independent verification for a LAB-02 scenario run after P4 was blocked on missing scenario proof artifacts.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | Current prior independent verification; no P1 changes since. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | Current prior independent verification; no P2 changes since. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | Current prior independent verification; no P3 changes since. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | blocked | not_started | 0 | Both S-push attempts passed kit preflight but fixture-server child exited code 1 before health/build/capture; no scenario evidence produced. Repository registry and source unchanged. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Depends on P4 pass. |
| P6 | Documentation and registry guidance | P4, P5 | pass | repair | 1 | Independent review found scripted-variant wording too broad for LAB-02 (`scripted-phaseb`) and plain diff-check flags CRLF lines. Correcting the wording and using CRLF-aware whitespace validation without whole-file normalization. |

## Package P4: LAB-02 scenario proof rerun

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1/P2/P3 prior passes retained; P4 implementation and host-side checks from B09F738B-D97A-4EA2-94F6-DDFA9ADE6F5C remain current. This rerun addresses only missing physical scenario proof evidence.
- Implementer role and invocation: fresh generic implementation/execution agent; completed.
- Implementer report: blocked; both manual and scripted S-push runs passed kit preflight, but the owned fixture-server child exited code 1 before health, build, flash, or capture. New failure manifests/events: `docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-6a992c9d/` and `docs/labs/evidence/lab-02/runs/20261017-p4-spush-scripted-6a992c9d/`. No source changes; temporary registry at `/private/tmp/family-link-p4-s-push-6a992c9d.yaml` was used only for the commands; repository registry remained unchanged.
- Baseline and changed paths: preserve all inherited dirty source and run artifacts. P4 touchpoints previously authorized include `scripts/lab_adapter_lab02.py`, `config/labs.registry.yaml`, `scripts/lab_runner.py`, `scripts/lab02_canonical_run.py`, `scripts/lab02_scenarios.py`, parser and relevant tests. Use a temporary registry copy for scenario selection so the repository registry is not changed for this run. New evidence belongs to unique LAB-02 run directories.
- Verifier role and fresh invocation: not dispatched; implementation report was blocked before scenario execution, so the orchestration stop condition applies.
- Verifier verdict: not_started.
- Findings and reproducible evidence: both failure manifests report `workflow_status=FAIL`, `experiment_verdict=INCOMPLETE`, `terminal_reason=child_exited`; stages build/activity/scenario are not_started, artifacts contain only event journals, and cleanup passed. No scenario receipt or proof is available to verify. The child exit cause is unknown because no server log was attached.
- Repair attempts: 0; blocked is not a verifier failure and does not authorize a repair cycle.
- Commit/PR/publish actions: none.
- Final state: blocked.

<!-- orchestrate-build:end run_id=A7E6BEE7-6D17-419D-A365-A89F1D39B2FE -->

<!-- orchestrate-build:begin run_id=E4C1AC32-7E72-4C41-9B51-8EDCCBF9B334 -->
# Orchestration progress

- Run ID: E4C1AC32-7E72-4C41-9B51-8EDCCBF9B334
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline paths: inherited implementation changes, prior orchestration records, and all LAB-02 evidence directories are protected except P4-scoped paths the user has already authorized. The previous attempt added only two partial failure run directories and made no source changes. Preserve all unrelated dirty paths and prior run artifacts. User explicitly reauthorized P4 implementation/execution and independent verification. No commits/pushes/PRs.
- Started: 2026-10-17 19:20:40 UTC
- Updated: 2026-10-17 19:44:36 UTC (independent verification completed; manual scenario proof remains missing)
- Auto-continue: true (approved in prior run)
- Rerun authorization: user explicitly reauthorized implementation/execution and independent verification after the previous scenario attempt stopped before build because the owned server child exited.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | No P1 source changes since independently verified pass. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | No P2 source changes since independently verified pass. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | P3 pass remains current; P4 must wire its run-scoped log capture. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | completed | blocked | 0 | Scripted S-push scenario proof independently passes; manual receipt arrived after snap_end and no correlated effect was produced. Stop before P5/P6. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Depends on P4 pass. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Depends on P4 and P5 passes. |

## Package P4: LAB-02 scenario proof rerun with server diagnostics

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1/P2/P3 prior passes remain accepted; P4's shared-engine implementation is current, but its hardware proof was blocked in two previous attempts.
- Implementer role and invocation: fresh P4 implementation/execution agent; completed for this authorized retry.
- Implementer report: completed; run `20261017-p4-spush-scripted-e4c1ac32-r3` passed scripted S-push; run `20261017-p4-spush-manual-e4c1ac32-r3` completed workflow but scenario proof was incomplete. Added run-scoped server log linkage, firmware run-id sentinel normalization at the coordinator boundary, moving-snap dispatch gating, and canonical run ID on the metrics envelope while retaining raw serial and parser evidence status. Focused canonical-runner suite: 24 tests passed. No commit/PR.
- Baseline and changed paths: preserve inherited source and artifacts except P4 changes explicitly authorized by the user. Ensure `ManagedLab02Server` receives a run-scoped log path under the current EvidenceRun; do not edit repository registry for scenario selection. Use a temporary registry copy set to `S-push`. New run evidence may be added only under unique run IDs.
- Verifier role and fresh invocation: fresh independent read-only verifier after implementation; completed.
- Verifier verdict: blocked (P4 not accepted; manual scenario proof requirement remains unmet).
- Findings and reproducible evidence: Scripted r3 manifest reports workflow PASS / experiment PASS with all lifecycle stages passed and fresh `LAB02_READY` build ID matching `firmware.build_id`; `server.log` is manifest-linked and records health/seed/script HTTP 200 and BOX-3 paging requests. Its scenario action/effect chain matches run/action/snap IDs and page receipts/operations and selects/views `msg-0001`. Metrics envelope run ID is attributed while `evidence.status=INCOMPLETE` remains; raw serial retains firmware `run_id=-` values. Manual r3 reports workflow PASS / experiment INCOMPLETE; the HTTP action receipt arrived after `snap_end` (`receipt_after_snap_end`), has no required snap/view identity, and no correlated `scenario_effect` exists. Independent verifier confirmed these observations and did not edit or run tests/device actions.
- Repair attempts: 0 within this run; verifier classified the missing manual proof as blocked, not fail. No dependent packages started.
- Commit/PR/publish actions: none.
- Final state: blocked.

<!-- orchestrate-build:end run_id=E4C1AC32-7E72-4C41-9B51-8EDCCBF9B334 -->

<!-- orchestrate-build:begin run_id=F9A42D98-9F66-4686-83B2-92AF341611BE -->
# Orchestration progress

- Run ID: F9A42D98-9F66-4686-83B2-92AF341611BE
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline protected paths: all visible modified/untracked source and evidence paths reported by `git status --short` at 2026-10-17 19:53 UTC remain protected, except user-authorized P4 scope. Do not reset, stage, clean, or overwrite any unrelated paths. Existing run artifacts are protected; new evidence must use unique run IDs. No commits/pushes/PRs.
- Started: 2026-10-17 19:53:45 UTC
- Updated: 2026-10-17 19:59:09 UTC (manual retry and independent verification completed; device interaction was absent)
- Auto-continue: true (approved previously)
- Rerun authorization: user authorized a fresh execution/implementation and independent verification after the prior P4 run remained blocked on manual S-push effect proof.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | completed | blocked | 0 | New manual run passed workflow/build/readiness/service/cleanup but recorded zero snaps/actions/effects; independent verifier confirms manual proof is still absent. Stop before P5/P6. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Depends on P4 pass. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Depends on P4 and P5 passes. |

## Package P4: manual S-push evidence retry

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1/P2/P3 accepted prior passes remain applicable; prior scripted S-push r3 independently passed. P4 remains blocked only on manual proof.
- Implementer role and invocation: fresh P4 execution/implementation agent; completed.
- Implementer report: no source changes. Initial `r1` attempt failed before build/capture on sandbox port bind; escalated retry `20261017-p4-spush-manual-f9a42d98-r2` completed workflow PASS / experiment INCOMPLETE with fresh readiness, managed service, and cleanup passing. Its 120-second serial/event capture has no snap/action/receipt/effect. Existing scripted r3 remains a separate scenario PASS. No commit/PR.
- Baseline and changed paths: captured with HEAD a60e368937594a03d711145029d2ffd82dfec18d and visible dirty-path inventory at dispatch. Preserve all preexisting changes except explicitly user-authorized P4 scope; do not rewrite prior artifacts.
- Verifier role and fresh invocation: fresh independent read-only verifier; completed.
- Verifier verdict: blocked; manual S-push scenario requirement remains unmet.
- Findings and reproducible evidence: Verifier inspected r2 manifest, 1,147 event rows, referenced device serial, metrics and server log. The manifest reports `workflow_status=PASS`, `experiment_verdict=INCOMPLETE`, `scenarios=[S-push]`, fresh boot, and passed build/service/activity/cleanup; the event journal and serial contain zero `snap_start`, `snap_end`, `scenario_action`, or `scenario_effect`. The server log shows health/seed HTTP 200 and 38 device inbox GETs but no push action. `metrics.json` has `snap_started=null` and incomplete evidence. Prior scripted r3 passes with receipt/effect/page correlation but does not substitute for manual mode. Verifier requires a manual moving snap, receipt before snap end, correlated run/action/snap effect and page fetch/apply, and scenario PASS.
- Repair attempts: 0.
- Commit/PR/publish actions: none.
- Final state: blocked.

<!-- orchestrate-build:end run_id=F9A42D98-9F66-4686-83B2-92AF341611BE -->

<!-- orchestrate-build:begin run_id=73F63C39-F6AF-4C6C-B9BF-7717D2E6E90E -->
# Orchestration progress

- Run ID: 73F63C39-F6AF-4C6C-B9BF-7717D2E6E90E
- Status: blocked
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline protected paths: all visible modified/untracked source and evidence paths reported by `git status --short` at 2026-10-17 20:00 UTC remain protected except previously user-authorized P4 scope. Do not reset, stage, clean, or overwrite unrelated paths. Preserve prior artifacts; new evidence uses unique run IDs. No commits/pushes/PRs.
- Started: 2026-10-17 20:00:00 UTC
- Updated: 2026-10-17 20:22:17 UTC (manual capture and independent verification completed; same-snap effect missing)
- Auto-continue: true (approved previously)
- Rerun authorization: user authorized execution/implementation and independent verification and confirmed readiness for the manual S-push run.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | No relevant changes since prior independent pass. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | completed | blocked | 0 | Manual action receipt correlated to snap 1, but updated revision 2 page fetch/apply happened under snap 5; no scenario_effect. Independent verifier confirms P4 remains blocked. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Depends on P4 pass. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Depends on P4 and P5 passes. |

## Package P4: coordinated manual S-push run

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1/P2/P3 accepted prior passes remain current; prior scripted S-push r3 passed independently. Previous manual attempts were incomplete because receipt arrived after snap end or because no device navigation was detected. User is ready to interact during this capture.
- Implementer role and invocation: fresh P4 execution/implementation agent; completed.
- Implementer report: completed, no source changes. `LABS_REGISTRY=/private/tmp/labs.registry.spush-73f63c39.yaml RUN_ID=20261017-p4-spush-manual-73f63c39 SECONDS=120 make lab.lab02` completed workflow PASS / experiment INCOMPLETE with fresh readiness and cleanup. It recorded a successful same-run HTTP 200 S-push receipt before snap 1 ended, but the revision-2 page fetch/apply occurred during snap 5; no effect was classified.
- Baseline and changed paths: captured with HEAD a60e368937594a03d711145029d2ffd82dfec18d and visible dirty-path inventory at dispatch. Preserve all preexisting changes except P4-scoped changes previously authorized. Use a temporary registry override with `scenario_default: S-push`; do not edit repository registry.
- Verifier role and fresh invocation: fresh independent read-only verifier after manual execution; completed.
- Verifier verdict: blocked; manual S-push effect proof requirement remains unmet.
- Findings and reproducible evidence: Manifest has workflow PASS / experiment INCOMPLETE, all lifecycle stages passed, and fresh boot/build identity. `events.jsonl` includes 95 snap start/end pairs; action `6e0171184c6948d0a39526f7dd351ddc` has HTTP 200 receipt for run `20261017-p4-spush-manual-73f63c39`, snap 1, viewed `msg-0001`, pushed `msg-push-0064`, revision 2; receipt arrived before snap 1 end. The matching revision-2 page fetch/apply pairs occur later under snap 5, and there is no `scenario_effect`, so classifier remains incomplete. Independent verifier checked source classifier and correlated artifacts; metrics remain INCOMPLETE and raw serial run-id sentinels are preserved. Prior scripted S-push r3 remains independently PASS but cannot replace manual proof.
- Repair attempts: 0.
- Commit/PR/publish actions: none.
- Final state: blocked.

<!-- orchestrate-build:end run_id=73F63C39-F6AF-4C6C-B9BF-7717D2E6E90E -->

<!-- orchestrate-build:begin run_id=57C53BAC-7823-4CA7-97FE-3AC7E9D714E3 -->
# Orchestration progress

- Run ID: 57C53BAC-7823-4CA7-97FE-3AC7E9D714E3
- Status: complete
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline protected paths: all visible modified/untracked source and evidence paths from `git status --short` at 2026-10-17 20:22 UTC remain protected except previously user-authorized P4 scope. No reset/clean/overwrite. Existing run artifacts retained; new evidence uses unique run IDs. No commits/pushes/PRs.
- Started: 2026-10-17 20:22:55 UTC
- Updated: 2026-10-17 20:27:55 UTC (manual S-push passed independent verification)
- Auto-continue: true (approved previously)
- Rerun authorization: user said “let's do it” after the previous independent verifier found manual receipt and updated-page application on different snaps.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | No relevant changes. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | No relevant changes. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | No relevant changes. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | completed | pass | 0 | Manual S-push r2 independently passes with same-run/action/snap receipt before snap end and same-snap revision fetch/apply/effect. Evidence-quality caveats recorded below. |
| P5 | No-server integration proof | P2, P4 | not_started | not_started | 0 | Depends on P4 pass. |
| P6 | Documentation and registry guidance | P4, P5 | not_started | not_started | 0 | Depends on P4 and P5 passes. |

## Package P4: coordinated manual S-push retry

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P1/P2/P3 prior passes accepted; prior scripted S-push r3 passes. Previous manual r3 captured 95 snaps and a valid receipt on snap 1, but page revision 2 applied under snap 5, so no effect. User authorized another attempt and will limit interaction to one deliberate swipe followed by pause.
- Implementer role and invocation: fresh P4 execution agent; completed, no source changes.
- Implementer report: manual S-push r2 (`20261017-p4-spush-manual-57c53bac-r2`) completed with workflow PASS / experiment PASS; all stages passed, fresh boot marker true, and host event loss zero. One deliberate swipe produced receipt-before-snap-end and same-snap revision 2 page apply and scenario effect. Existing scripted r3 also independently passed.
- Baseline and changed paths: HEAD a60e368937594a03d711145029d2ffd82dfec18d; full visible dirty-path inventory at dispatch protected except P4 paths previously authorized. Use temporary registry copy selecting S-push; no repository registry edits.
- Verifier role and fresh invocation: fresh independent read-only verifier after run; completed.
- Verifier verdict: pass.
- Findings and reproducible evidence: Manual manifest reports workflow and experiment PASS with preflight/build/service/activity/scenario/cleanup passed and fresh_boot_marker true. Action `7540a449aaff4c8aa61695bae012f4d5` has HTTP 200 receipt for run `20261017-p4-spush-manual-57c53bac-r2`, snap 1, viewed `msg-0002`, pushed `msg-push-0064`, revision 2; `receipt_before_snap_end=true`. Revision-2 page_fetch/page_apply op 4 share snap 1 and page receipt `730fcf6588204b5e9474b4091593917d`; scenario_effect matches run/action/snap/op/page receipt/viewed message. Server log records health, seed, paging and action requests. Independent verifier compared the classifier predicates and prior scripted r3 proof; it could not import the classifier because PyYAML was unavailable. Caveats: raw serial reports one queue-full trace drop after page apply, host event loss remains zero; metrics evidence remains honestly INCOMPLETE for raw legacy trace identity; `artifacts.serial_log` is null while `artifacts.device_log` references retained serial.
- Repair attempts: 0.
- Commit/PR/publish actions: none.
- Final state: pass for selected P4 rerun; P5/P6 were not selected and remain not_started.

<!-- orchestrate-build:end run_id=57C53BAC-7823-4CA7-97FE-3AC7E9D714E3 -->

<!-- orchestrate-build:begin run_id=7195F08C-1782-47F3-9D1F-FAF1F4D26932 -->
# Orchestration progress

- Run ID: 7195F08C-1782-47F3-9D1F-FAF1F4D26932
- Status: complete
- Map: docs/labs/reusable-lab-runner-impl.md
- Map SHA-256: d05d6de632180d2fb022bad26b8b7e1df498115fc14c13703f0565bffdd470ef
- Specification: docs/labs/reusable-lab-runner.md
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Baseline protected paths: visible modified/untracked source and evidence paths at 2026-10-17 20:40 UTC remain protected, except P5 overlap explicitly authorized by the user for `config/labs.registry.yaml`, `scripts/lab_runner.py`, and `scripts/tests/test_lab_runner.py`. New LAB-05 files may be added under package scope. Do not reset/clean/overwrite unrelated paths. No commits/pushes/PRs.
- Started: 2026-10-17 20:40:53 UTC
- Updated: 2026-10-17 21:13 UTC (P5 final evidence and P6 repair independently verified)
- Auto-continue: true (approved previously)
- Rerun authorization: user authorized continuation with the no-server/lab-independence implementation and overlap in the named P5 files.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1 | Shared evidence contracts | none | reused prior pass | pass | 0 | Current prior independent pass. |
| P2 | Shared runner and adapter dispatch | P1 | reused prior pass | pass | 0 | Current prior independent pass. |
| P3 | Owned service logs | P2 | reused prior pass | pass | 0 | Current prior independent pass. |
| P4 | LAB-02 adapter migration | P1, P2, P3 | reused prior pass | pass | 0 | Current independent pass for manual and scripted S-push; see run 57C53BAC. |
| P5 | No-server integration proof | P2, P4 | pass (implementation and builds) | pass (independent end-to-end verification) | 2 | LAB-05 manual and scripted runs both pass with linked serial, common v2 manifest/event shapes, and service/scenario not_configured. Each records one transition. Scripted serial lacks HARNESS_DONE; recorded as a limitation, not a P5 criterion failure. |
| P6 | Documentation and registry guidance | P4, P5 | pass | pass | 1 | Four guidance/index docs corrected and independently verified; CRLF-aware whitespace and link checks pass. |

## Package P5: no-server lab independence proof

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P2 and P4 are accepted current passes. User explicitly authorized P5 implementation, overlap in the registry/shared runner files, and the required generic adapter selection change after its protected-path dependency was identified.
- Bounded behavior decision: implement only enough LAB-05 behavior to exercise manual and scripted no-server runner paths and produce a small transition/activity trace; do not implement the full LAB-05 resource research or present this proof as a product performance result.
- Implementer role and invocation: fresh P5 implementation agent; completed. A separate authorized adapter repair agent then updated the generic variant selection.
- Implementer report: registered a minimal LAB-05 two-state local transition proof with manual and scripted firmware variants; no service/workflow object or LAB-02 settings. Removed explicit false Kconfig setting because IDF omits false bools from generated sdkconfig and that caused repeated stale-config invalidation. No LAB-02 files or evidence were changed by P5.
- Baseline and changed paths: HEAD a60e368937594a03d711145029d2ffd82dfec18d and full visible dirty-path list captured before dispatch. User authorized `config/labs.registry.yaml`, `scripts/lab_runner.py`, `scripts/tests/test_lab_runner.py`, new LAB-05 project files, and subsequently `scripts/lab_runner_adapter.py` for the mode-selected variant fix. All unrelated dirty paths remain preserved.
- Verifier role and fresh invocation: fresh independent read-only code/build verifier, adapter dispatch verifier, serial-artifact verifier, and final read-only device-evidence verifier; all completed. No verifier accessed hardware.
- Verifier verdict: pass for the P5 acceptance criteria. The independently verified manual and scripted runs both have common v2 artifacts, successful workflow lifecycle, server `not_configured`, and a transition marker. The missing scripted `LAB05_HARNESS_DONE` is a limitation but does not invalidate these stated P5 criteria.
- Findings and reproducible evidence: `.venv/bin/python scripts/lab_registry.py validate` passed; `.venv/bin/python -m unittest scripts.tests.test_lab_runner -v` passed 9/9; both manual/scripted firmware builds passed. Adapter verification confirms explicit variant precedence, mode matching, fallback, and consistent build/capture/evidence variant selection; `make -n lab.lab05.scripted` dispatches `run lab05 --mode scripted`. Final manual run `20261017-manual-run-fd3cc46d` and scripted run `20261017-scripted-run-5a9c9dd1` both report workflow PASS, service/scenario `not_configured`, build/activity/cleanup passed, `event_loss.dropped_events=0`, and `experiment_verdict=UNMEASURED`. Both v2 manifests link the corresponding run-scoped serial file. Manual serial has `LAB05_READY build=lab05-manual-v1` and one touch transition; scripted serial has `LAB05_READY build=lab05-scripted-v1`, `HARNESS_BEGIN`, and one harness transition. Each marker's build ID matches the corresponding variant's firmware. Scripted serial does not include `LAB05_HARNESS_DONE`; this remains a telemetry caveat, while the P5 criteria are still satisfied. P5 does not claim resource/performance results. The earlier manual run `20261017-manual-run-94493dfc` captured three transitions before artifact normalization; it is retained but not relied on for the final linked proof.
- Repair attempts: 2 (adapter mode-to-variant selection and serial artifact path normalization; each independently verified).
- Commit/PR/publish actions: none.
- Final state: P5 implementation, builds, and independent code/build/dispatch/artifact/device verification pass. Caveat: scripted firmware's completion marker was not observed; no experiment/performance claim is made.

## Package P6: registry and author documentation

- Definition fingerprint: 6dc9b8a6e12f1c6e404d8e46450f278514193284ad7ad0450e4bffc071e3cf87
- Dependency evidence / waivers: P4 independently passed (manual and scripted S-push); P5 independently passed the bounded no-server acceptance criteria. P5 caveats remain explicit: LAB-05 is UNMEASURED; scripted serial lacks `LAB05_HARNESS_DONE`.
- Implementer role and invocation: fresh P6 documentation implementation agent; completed.
- Implementer report: updated only `docs/standards/labs-configuration.md`, `docs/labs/assistant-guide.md`, `docs/labs/README.md`, and `docs/labs/evidence/README.md`. Documentation now distinguishes shared `.run` from lower-level `.capture`, describes generic/lab02 allowlisted adapters and optional workflow/server stages, references v2 schemas, and links accepted P4/P5 runs with honest P5 caveats. Registry YAML, schema, source, P1 schema artifacts, run evidence, and this progress file were not edited by the P6 implementer.
- Baseline and changed paths: P6 wrote four previously clean guidance/index files only. All dirty evidence/source paths from earlier packages are preserved. The P1-created untracked `docs/labs/schemas/run-manifest.v2.md` and `metrics.v2.md` remain unchanged and are linked.
- Verifier role and fresh invocation: fresh independent read-only P6 verifier after repair; completed with PASS.
- Verifier verdict: pass.
- Findings and reproducible evidence: `.venv/bin/python scripts/lab_registry.py validate` passed; `make config.labs` passed and listed two labs; `make lab.help` passed and listed manual/scripted entries for LAB-02/LAB-05; elevated `make test` passed 221 tests in 68.688 seconds. Initial independent review caught that generic scripted selection falls back to the default when no literal `scripted` variant exists, while LAB-02 managed HTTP scripted mode defaults to `scripted-phaseb`; the standards and assistant guide now describe both accurately. `make -n lab.lab02.scripted` and `make -n lab.lab05.scripted` confirm dispatch. All Markdown links resolve, and CRLF-aware whitespace validation passes. The only documentation changes are the four P6 guidance/index files; schemas, registry, source, and evidence remain untouched by P6.
- Repair attempts: 1 (variant-selection wording corrected; independently rechecked).
- Commit/PR/publish actions: none.
- Final state: P6 implementation, validation, repair, and fresh independent verification pass; run complete.

<!-- orchestrate-build:end run_id=7195F08C-1782-47F3-9D1F-FAF1F4D26932 -->


<!-- orchestrate-build:begin run_id=20261017-E2FB475C804748C1A85146C39A4A2A2A -->
# Orchestration progress — event provenance follow-up

- Run ID: 20261017-E2FB475C804748C1A85146C39A4A2A2A
- Status: complete
- Parent map: docs/labs/reusable-lab-runner-impl.md (P1–P6 complete; this is a user-authorized follow-on package derived from the event-count diagnosis)
- Follow-on package: P7 — Event provenance and per-source loss accounting
- Package definition fingerprint: f279c56f5d091172ec08c47f7336bae705bbe5e01d90052d5430c3a835454c5e
- Specification: docs/labs/reusable-lab-runner.md; event/source requirements in sections “Correlation and artifacts” and “Common metrics envelope”
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a60e368937594a03d711145029d2ffd82dfec18d
- Started: 2026-10-17 22:23 UTC
- Authorization: user authorized implementation and independent verification using the same implementer/verifier approach. The authorization covers the necessary overlap in LAB-02 runner/evidence paths and the v2 schema artifacts.
- Auto-continue: true for this single package; do not begin unrelated packages.
- Protected baseline: all paths listed below are pre-existing user work. Only P7 in-scope paths may be edited; preserve every other path/artifact exactly. No commits/pushes/PRs.

### Frozen P7 brief

P7 — Event provenance and per-source loss accounting. In scope: shared v2 event journal, run manifest and metrics shape, LAB-02 adapter/event relay, LAB-02 firmware snap trace origin, targeted tests/docs. Every event journal row must identify its source (host/server/device) or use an explicit unknown; host wrapper stage rows and device trace rows must remain distinguishable. Manifest must report host journal drops separately from firmware trace loss from LAB-02 markers, preserving UNKNOWN when device loss evidence is absent. snap_start/snap_end must carry a stable origin (touch/harness/programmatic/unknown) so traces can distinguish human gestures from harness/programmatic actions. Preserve existing event meaning, P1-P6 behavior, v1 compatibility, raw serial evidence, bounded queues and no-secret handling. Add tests for source tags, separate/unknown loss counters, snap-origin propagation and backward compatibility. Out of scope: changing experiment semantics, queue size/drop policy, unrelated lab source/evidence, hardware run. Verification: focused Python evidence/runner/scenario tests; firmware native trace transport tests or equivalent host build; registry/docs checks as affected; inspect sample manifests to prove loss semantics and all source tags. Fresh read-only verifier independently checks the implementation and runs relevant checks. No commit/PR.

### Baseline `git status --short` captured at dispatch

```text
 M Makefile
 M config/labs.registry.yaml
 M config/labs/labs.registry.schema.v1.json
 M docs/labs/README.md
 M docs/labs/assistant-guide.md
 M docs/labs/evidence/README.md
 M docs/standards/labs-configuration.md
 M scripts/lab02_canonical_run.py
 M scripts/lab02_evidence.py
 M scripts/lab02_server.py
 M scripts/lab_host.py
 M scripts/lab_registry.py
 M scripts/tests/test_lab02_canonical_run.py
 M scripts/tests/test_lab02_evidence.py
 M scripts/tests/test_lab02_server.py
 M scripts/tests/test_lab_host.py
?? client/labs/lab-05-transition-proof/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-1f04c485/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-22f0b6e4/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-235f1095/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-5c9a0410/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-7bb3d507/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-86ed3ff0/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-8b8c9a8a/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-cefb859f/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-e6285936/
?? docs/labs/evidence/lab-02/runs/20261017-eager-run-ee60a3d5/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-57c53bac-r1/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-57c53bac-r2/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-6a992c9d/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-73f63c39/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-e4c1ac32-r2/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-e4c1ac32-r3/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-e4c1ac32/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-f9a42d98-r1/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-manual-f9a42d98-r2/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-scripted-6a992c9d/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-scripted-e4c1ac32-r1/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-scripted-e4c1ac32-r2/
?? docs/labs/evidence/lab-02/runs/20261017-p4-spush-scripted-e4c1ac32-r3/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-010f7aad/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-2694dcb2/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-36782eac/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-45dea1f6/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-4c65ef88/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-61611444/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-6a300c3d/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-7af03834/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-a58db4b5/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-b8dd78d7/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-d2a35186/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-ec267cf6/
?? docs/labs/evidence/lab-02/runs/20261017-phaseb-run-f84e3089/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-11a3cd2e/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-43e5a8e2/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-47d66309/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-71a33431/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-80a10ce2/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-86a387ac/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-afb8a19a/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-b4f51dc4/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-d6fccc16/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-ee504aa9/
?? docs/labs/evidence/lab-02/runs/20261017-scripted-phaseb-run-f6ca2dbb/
?? docs/labs/evidence/lab-05/
?? docs/labs/orchestration-progress.md
?? docs/labs/schemas/metrics.v2.md
?? docs/labs/schemas/run-manifest.v2.md
?? scripts/lab_adapter_lab02.py
?? scripts/lab_adapter_registry.py
?? scripts/lab_run_evidence.py
?? scripts/lab_runner.py
?? scripts/lab_runner_adapter.py
?? scripts/server_log.py
?? scripts/tests/test_lab_runner.py
```

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| P1–P6 | Existing reusable lab runner packages | none for P1; sequential through P6 | pass | pass | recorded in prior run 7195F08C | Current accepted passes remain intact. |
| P7 | Event provenance and per-source loss accounting | P1–P6 | pass | pass | 0 | Implemented and independently verified; evidence below. |

## Package P7: event provenance and per-source loss accounting

- Definition fingerprint: f279c56f5d091172ec08c47f7336bae705bbe5e01d90052d5430c3a835454c5e
- Dependency evidence: P1–P6 accepted in orchestration run 7195F08C-1782-47F3-9D1F-FAF1F4D26932; their implementation and evidence are protected except the currently authorized P7 overlap.
- Implementer role: fresh implementation agent, one package only.
- Implementer report: pass. Journal rows identify host/server/device/unknown; manifest reports host journal drops separately from firmware trace loss parsed from LAB-02 markers, with UNKNOWN/null when absent; snap traces carry touch/harness/programmatic/unknown origin. Updated v2 schema docs and focused tests.
- Baseline/current scope: authorized overlap is limited to LAB-02 canonical runner/evidence and adapters, shared event/evidence implementation, v2 schema docs, LAB-02 snap tracing and its focused tests. All other baseline files and run artifacts remain protected.
- Verifier role: fresh independent read-only verifier after implementation.
- Verifier verdict: pass (fresh read-only verifier). It independently ran 40 focused evidence/scenario/runner/canonical/native transport tests and 29 LAB-02 trace metrics tests; validated registry and `make lab.help`; reviewed source tags, snap-origin call paths, and compatibility.
- Repair attempts: 0 (limit 1).
- Commit/PR/publish actions: none.
- Limitations: no hardware run or full ESP-IDF firmware build. The managed-server integration test could not bind to 127.0.0.1 in this environment; the targeted canonical loss-propagation test passed separately. Native trace transport compiled and passed.
- Commit/PR: none.
- Final state: complete.

<!-- orchestrate-build:end run_id=20261017-E2FB475C804748C1A85146C39A4A2A2A -->

## Follow-up: queue-complete capture and final BOX-3 verification

- Authorization: user authorized continued implementation/execution and independent verification, then explicitly asked to commit and push the work.
- P8 implementation: increased the firmware trace queue from 8 to 10 records to absorb the observed snap burst; added saturation coverage. Managed server shutdown now distinguishes a delivered SIGTERM followed by exit -15 from a failed or forced termination.
- P8 verification: independent code review and lifecycle/native transport checks passed. Hardware runs confirmed no trace drops with the larger queue; the latest verification below records current firmware memory headroom.
- P9 implementation: after `HARNESS_DONE`, stop and join the serial reader and parse all chunks already buffered in its bounded queue through the normal parser. Record reset and boot-ready events as host events. If the reader reports a queue overflow or other error, force workflow and experiment status to INCOMPLETE, even if `HARNESS_DONE` was seen. Added regressions for post-completion chunks spanning queued reads and for overflow after valid completion.
- P9 verification: fresh independent read-only review passed the queue-drain and fail-closed paths. The reviewer’s sandbox blocked its managed HTTP integration subtest at loopback bind; the maintainer reran the complete focused suite with local access: 26 tests passed.
- Final hardware run: `20261017-scripted-phaseb-run-36b925e5`; `make lab.lab02.scripted KIT=lynn` completed with workflow PASS / experiment UNMEASURED. An independent runtime verifier confirmed all 294 raw serial resource samples match both `events.jsonl` and `metrics.json`; 40 `snap_start`/`snap_end` pairs match by snap ID and use `snap_origin=harness`; fresh `LAB02_READY` build identity matches manifest and metrics; `HARNESS_DONE` is present; host and firmware loss counters are zero; managed server expected shutdown and cleanup passed. Minimum `internal_min_free_bytes` was 8,819 bytes.
- Evidence limitation: this was a scripted nominal lifecycle run. It is UNMEASURED and makes no performance or manual-touch claim.
- Commit and push: pending.
