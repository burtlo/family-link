# Independent LAB-02 W4 verification

Date: 2026-10-12. Read-only inspection at HEAD `0b02b30086f1c8c8e985458912646b0d473b967b` plus working-tree WIP. Governing sources read: `docs/AGENTS.md`, full `docs/standards/client-code-review-agent.md`, full `docs/standards/client-application-coding-standards.md`, and W4/F001/O05 section of `docs/labs/lab-02-workflow-remediation-plan.md`. Scope: `scripts/lab02_server.py`, `server/labs/lab-02-paging/server.py`, `scripts/tests/test_lab02_server.py`. Exact fixer snapshot separately recorded in `/tmp/lab02-w4-fix/w4.diff`; current relevant source hashes in `source-sha256.txt`.

## Commands and evidence

- `.venv/bin/python -m unittest scripts.tests.test_lab02_server scripts.tests.test_lab02_paging_server -v` — PASS, 8 tests, 1.510s. Required approved loopback execution. Covers real managed subprocess and seed, foreign listener refusal, wrong health run identity timeout, early child exit, mocked seed timeout, action timeout/cancel, idempotent cleanup, paging endpoints.
- `.venv/bin/python -m py_compile scripts/lab02_server.py server/labs/lab-02-paging/server.py scripts/tests/test_lab02_server.py` — PASS (same approved command invocation).
- Focused instrumented boundary harness (output in `raw-harness.txt`) — confirms KeyboardInterrupt during seed produces no `server_seed` terminal event; `{ok:false,error:unknown_script}` from action is returned and emitted as success; forced cleanup timeout after terminate+kill loses `self._process` handle while emitting cleanup failure.

## Disposition

**Partially fixed; F001/W4 not fully verified (L1 with limited L2 subprocess evidence).**

Fixed by evidence:
- Real child is started with a unique run ID; health requires matching run ID, seed checks run ID, fixture SHA-256 and item count.
- Occupied listener is rejected without taking ownership or stopping it.
- Health and action requests have configured bounded request timeouts; seed timeout is applied to its request. Startup exit, health identity mismatch, seed timeout and ordinary action timeout produce stage failures and owned-child cleanup in exercised cases.
- Normal cleanup is idempotent and stops the owned child. State-changing script requests are serialized with a lock.

Not fixed (concrete regressions):
- `deadlines_seconds["startup"]` is required/configurable but never read in lifecycle helper. A `Popen` that stalls (or a blocked process creation) has no startup deadline. Regression trigger: inject/encounter a blocked process spawn; the configured startup bound has no effect. See `lab02_server.py:93-110`.
- `KeyboardInterrupt` during `_seed` escapes its `except Exception`; `start` closes the child but no terminal `server_seed` outcome is emitted. The same interruption during `post_action` has no terminal action event because only selected ordinary exceptions are caught. Regression trigger: Ctrl-C during seed/action; no terminal outcome for accepted seed/action. Verified by harness.
- `post_action` treats any JSON response as success, including HTTP error response bodies `{ok:false,...}`; it does not validate `ok` or action/run receipt. Regression trigger: unknown script or rejected `remove_at`; host emits success while server reports failure. Verified by harness. This is needed for truthful W4 action terminal status; W5 can add action-specific receipt/telemetry, but W4 must at least reject non-ok responses.
- `close()` clears `_process` before termination/wait completes and suppresses cleanup exceptions. On repeated timeout/failure the owned process reference is lost, and caller receives no surfaced cleanup failure beyond event. Regression trigger: `terminate`/`kill`/`wait` failure; cannot retry/inspect owned child and startup failure can be raised as if cleanup completed. Harness confirms lost handle. See `lab02_server.py:158-172`.

Unverified:
- End-to-end Ctrl-C/cancel during managed server lifecycle against real child (current tests mock cancellation before request and seed timeout; no Ctrl-C scenario).
- Seed response mismatch and actual seed HTTP rejection path. Existing test exercises timeout only.
- Action response correctness under real endpoint rejection.
- No manual server, curl, hardware, or L3 checks performed. No source/doc changes made.

## Evidence limits

The 8 existing tests pass, but do not cover the four regressions above. Do not mark W4/F001 verified fixed yet. A focused retry should add/fix only startup bound, interrupted action terminal handling, action response validation, and cleanup handle/error behavior, followed by a fresh verifier. W5-specific correlation/telemetry remains future scope.
