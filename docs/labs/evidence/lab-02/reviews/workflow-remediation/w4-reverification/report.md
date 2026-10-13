# Independent LAB-02 W4 retry verification

Date: 2026-10-12. Source boundary: HEAD `0b02b30086f1c8c8e985458912646b0d473b967b` plus the current W1-W3 worktree changes and W4 repair. Read the complete `docs/AGENTS.md`, workflow plan F001/W4/O05, `docs/standards/client-code-review-agent.md`, and full `docs/standards/client-application-coding-standards.md`. Target was read-only: `scripts/lab02_server.py`, `scripts/tests/test_lab02_server.py`, and managed server interface. Exact prior W4 and retry patches were inspected from `/tmp/lab02-w4-fix/w4.diff` and `/tmp/lab02-w4-repair/lab02_server.patch`.

## Results

**F001 / W4 retry: FIXED at L2 for the focused managed server lifecycle and request/action boundaries. L3 was not attempted and is not claimed.**

The four defects from the first independent review are addressed in the current source:

- Startup now uses the configured absolute startup deadline to bound waiting for process creation; the late-spawn test confirms a child produced after timeout is terminated/reaped and startup emits one failure terminal.
- Seed and action catch interruption and emit exactly one cancelled terminal before propagating `KeyboardInterrupt`.
- Seed and action require HTTP 200 plus `ok is True`; seed also checks child liveness, run identity, fixture hash and count. A real invalid action endpoint response fails rather than returning success.
- Cleanup retains the child handle and raises `cleanup_failed` when stopping fails, permitting a successful retry; normal cleanup is idempotent. Foreign listener remains open and unowned.

The request path uses nonblocking socket operations with one monotonic deadline across connect/send/headers/body. A slow-drip response test confirms deadline expiration, and a real owned child covers healthy start, health, seed, and cleanup.

Command: `.venv/bin/python -m unittest scripts.tests.test_lab02_server -v` — PASS, 13 tests, 1.808s. Required approved loopback execution; covers normal real child, foreign listener, late spawn timeout, seed and action interruption/rejection, cleanup failure/retry, actual endpoint rejection, slow-drip deadline, health mismatch, startup exit, and seed timeout.

No source or maintained document was edited. No hardware, manual-server, curl, W5 scenario, or canonical W7 workflow checks were done. The W4 module test provides scripted runtime evidence for W4, not integrated workflow/hardware proof.

## Provenance

Current hashes and patch hashes are recorded in `sha256.txt`.
