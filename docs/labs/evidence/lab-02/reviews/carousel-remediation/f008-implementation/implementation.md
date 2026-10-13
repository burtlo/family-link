# LAB02CAR-F008 focused implementation handoff

Source frozen; fixer self-checks only. Fresh independent verification remains required.
Original complete finding: docs/labs/evidence/lab-02/reviews/carousel-remediation/resume-memory-review/report.md; stable ID appended in review.md. HEAD remains 7ecc594861c1e6fbc9ad647ac591bb44df3acde9 with accepted extensive staged/unstaged WIP.

## Result and ownership

Actual ERROR status click now retains one coalesced presentation-owner retry intent. It never takes the worker delivery mutex or starts a task. Callback's existing pump tries admission with zero wait. Busy mutex or unreleased boot terminal preserves the intent for later pump. Admission increments attempt once, creates the new immutable boot envelope, clears initial-page failure, queues SHELL and one restart command. Lifecycle session task invokes provider service after lab02_board_lvgl_unlock. This service consumes the command with a try-lock, copies the existing boot request, and launches boot outside presentation. Creation failure reporting can wait on worker delivery mutex only in that lifecycle context.

The boot-running atomic guard is set before creation and cleared after final result posting and last scratch/request use. No task handle is used for a concurrently self-deleting boot worker. While a prior boot remains active the broker leaves the command untouched. An existing background worker is retained. Session generation is atomic for broker reset/stale comparison; reset clears unadmitted owner intent and old admitted command cannot dispatch against a new generation. Existing terminal ledger stale/duplicate rejection remains authoritative. No new task, queue, page-sized stack buffer, automatic failure retry, or numeric product budget.

Scope: seven implementation/header files, existing/new test harness support, two test runners, session architecture description. See changed-files.json and unit.patch; readable patch represents only this unit against pre-unit WIP, not HEAD.

## Self-check evidence

- Original characterization compiled the frozen pre-unit actual session: retry remains blocked during injected 100ms producer admission-mutex hold, returns after release. characterization-command.json, characterization-build.log, characterization.log.
- L1: 23 native/model/architecture/host checks PASS (integrated-final.log). Actual provider allocation and terminal checks preserved. No skipped native checks.
- L2: actual vendored LVGL normal: 11 prior scenarios plus 6 F008 click/provider scenarios PASS (normal-frozen/ and runner log). Slow120: 12 prior scenarios plus the same 6 F008 scenarios PASS (slow120-final/).
- F008 actual click gates hold the real host-adapted session admission mutex across two LVGL status clicks, owner pump and broker service. They return while lock stays held, admit once after release, and show attempt2. xTaskCreate asserts presentation-context flag is clear; owner pump cannot allocate. Provider is actual production C, not no-op. HTTP/RTOS task execution remain explicit fault adapters. retry-success, retry-boot-fail, retry-background-fail, retry-reset-pending, retry-reset-admitted and retry-retained-worker logs/screens cover truthful failure/copy, no automatic repeated retry, stale prior completion, superseded command, exactly-once and worker reuse.
- L0: actual ESP32-S3 HTTP phaseb compile-only PASS twice, final firmware-final.log says Project build complete. No hardware access/flash. Current effective CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=50 (unchanged). Existing LVGL deprecation warnings remain.

## Harness repairs, separated from product fix

Frozen original-session run (original-session.log) reproduces the existing explicit-refresh assertion after real nanosleep loops cross the unchanged five-second background refresh policy. Long-timeout native run shows the same, without deadlock. Policy clock now freezes only for that race test, preserving actual pthread loops, delivery contention, and >1-second held motion; its subprocess guard becomes10s to accommodate existing3.2 seconds requested sleeps plus macOS overshoot. This is no product timing waiver. Before/after source and original raw traces are retained.

The pre-unit slow failed-page case asserts BOUND immediately after successful retry submit with no timer advance (original frozen excerpt in before/.../test_carousel_baseline.c). slow120/failed-page.log shows valid loading state, not product wrong content. Added advance(CONFIG_LAB02_SLOW_BIND_MS+20), consistent with adjacent delayed-binding assertions. No binding implementation changed. F008 executable receives the same slow-bind configuration.

## Provenance and preservation

before/ contains selected pre-unit lab bytes only, not recursive historical evidence. Scripts/architecture pre-unit contents are reconstructed from HEAD plus captured before-wip.diff, verified against reverse incremental edits (baseline-script-reconstruction.json). before-index.txt equals after-index.txt exactly; no staging, commit, reset or stash. preservation.json confirms unrelated tracked WIP diff identical and75 of88 pre-unit lab files byte-identical; all13 changed baseline lab files are enumerated in unit patch, alongside3 existing files outside lab and1 new runtime source. unit-byte.diff.json retains exact before/after bytes including CRLF; after/ and SHA256 manifests freeze current unit. lab_main existing CRLF bytes retained; git diff --check reports CRLF whitespace in that added line and baseline README/sdkconfig defaults, not other new whitespace.

Canonical commands:
python3 -m unittest scripts.tests.test_lab02_model_native scripts.tests.test_lab02_architecture scripts.tests.test_lab02_host -v
python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-f008-fix/normal-frozen
python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-f008-fix/slow120-final --slow-bind-ms 120
source /Users/lynnfrank/esp/esp-idf/export.sh; idf.py -C client/labs/lab-02-bounded-inbox -B build/labs/lab-02-bounded-inbox-phaseb build

## Limits / verifier gates

No L3 target touch/timing, resource-pressure, stack or FreeRTOS scheduling confirmation; no waiver. Host callback elapsed_us is injected-lock self-check, not representative device latency. Runtime uses context flag assertions for presentation ownership plus source proof that lifecycle service follows actual board unlock; it does not run physical BSP locking/task scheduling. Prior boot terminal-to-exit guard is source-verified; host adapters execute boot synchronously, so physical late-exit interleaving remains target verification. HTTP adapters return bounded fixture data and inject allocation failure, not real network transport. F008 is implemented, self-checked L1/L2, overall unverified pending fresh independent verification and L3. Root outcome docs untouched. C4 remains separate.
