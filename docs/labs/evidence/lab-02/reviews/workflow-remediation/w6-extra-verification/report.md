# W6 extra verification — terminal reason size cap

Outcome: the authorized host evidence-writer size repair independently passes.

The terminal `run_terminal` JSONL record stays at or below 16,384 encoded UTF-8 bytes for 20,000-character ASCII, JSON-escaped quote/backslash/control input, and multibyte Unicode input. The terminal event and manifest carry the same bounded sanitized reason; both truncation indicators are true when shortened and the reason ends with `[truncated]`. Injected atomic-write failure followed by retry with changed facts makes exactly two identical manifest payload attempts, preserves the first terminal status/reason, and leaves one terminal event.

The focused writer suite passed all 10 tests. Re-running the prior W6 quota/retry/artifact repro confirms the original repaired invariants: bounded event storage/file count (3 rows with quota 2 including marker), retry writes the manifest while preserving first FAIL facts, and noncanonical artifact references resolve to null. Original event oversize handling remains bounded. Scoped `git diff --check` passed. Current source hashes match the owner-provided extra-fix record.

**Verification level:** L1 model tests passed. L2 host-writer filesystem behavior was exercised in temporary roots, including JSONL and atomic manifest retry. W6 is verified at the standalone writer boundary; W7 host-runner integration and any end-to-end workflow remain unverified. No device, physical, or experiment-semantic claim is made.

**Fixed:** encoded terminal row cap; reason sanitization and shared truncation; frozen and retry-consistent terminal facts; exactly one terminal record; previously established quota and artifact-root writer invariants.

**Not fixed / unresolved:** none found within the authorized W6 writer scope.

**Unverified:** W7 runner integration, integrated host workflow behavior, target firmware/hardware behavior, and L3 experiment outcomes. No source, documentation, or repository evidence files were modified by this verification; raw results and hashes are in this `/tmp/lab02-w6-extra-verify` directory.
