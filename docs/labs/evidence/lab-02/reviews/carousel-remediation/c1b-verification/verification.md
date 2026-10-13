# C1b independent verification — LAB02CAR-F007

Disposition: **unverified overall; allocation terminal mechanism independently verified at L1, affected startup-path L2 and target L3 remain missing. L2 headless regression checks pass with a substituted provider.** No waiver. No code repair needed from this pass. No regression demonstrated in the assigned change.

## Boundary and provenance

Fresh read-only verifier `/root/c1b_allocation_verify`, separate from fixer. Main HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9`, baseline WIP + C0 + independently verified C1a + frozen C1b unit. Exact C1b `unit.patch`, 15 changed paths and after-hashes inspected. All 85 non-generated before/after packet paths match current raw SHA-256; tracked raw git diff unchanged across verification. Generated Python bytecode is a normal test artifact, excluded from source stability. No maintained source/doc edits, hardware, flashing, cleanup or subagents.

## Governing evidence

Read client-code-review skill and full canonical workflow, docs/AGENTS, full client coding standard, original review F007 and execution authorization/owner decisions, carousel remediation plan and session architecture, client README and documentation index, v2 implementation practices/resource budget guide. CAR-BUDGET allocation terminal clause; CAR-CONTRACT; standards 4/7/8 and evidence ladder 11 govern. The draft plan does not grant implementation authority; execution records the owner-authorized sequential remediation. Initial-load retry policy remains OPEN. F004 motion deferral and F006 retry remain separate open findings.

Original triggers: boot `xTaskCreate` failure rejected ERROR from SHELL with reserved boot work outstanding; background task failure after initial success logged without availability reconciliation, followed by prefetch admission without a consumer.

## Independent source assessment

`page_provider_http_start_io` reserves the initial envelope and emits startup admission. Failed boot-task creation reports unavailable then posts the reserved initial fetch failure. `lab02_session_pump` owns status reconciliation and accepts SHELL->ERROR, releases the boot terminal once, and leaves normal admission closed. The controller maps ERROR to existing `LAB-02  inbox unavailable` copy.

Boot I/O obtains/parses page zero, creates the background consumer before posting initial success, then reports first immutable startup availability. Task creation is absent from the owner pump. Worker admission is unavailable until the owner applies that report. On background creation failure, owner preserves valid page-zero data, terminalizes the truthful initial fetch success, reaches ERROR, emits startup failure once, suppresses HTTP_READY and queues no work. Held input cannot indefinitely defer that failed startup; ordinary successful-page deferral remains unchanged.

Availability reports compare session generation and accept only the first report. Duplicate/stale success cannot reopen failed admission. Initial completion uses reserved identity session/op/page/initial; duplicate/unadmitted completion cannot overwrite it. Storage is bounded (one startup mailbox plus existing terminal reservations), with no retry mechanism introduced.

## Commands and observed results

See commands.json and raw logs in this directory.

- Independent focused unittest run: 7 model/native/architecture tests PASS, no skips. Includes actual production HTTP provider with deterministic RTOS allocation and ESP HTTP adapters, six terminal scenarios and ownership checks.
- Production-provider boot-fail: ERROR from SHELL, one startup failure and one initial fetch failure, zero HTTP calls/queued requests, boot reservation reconciled, no HTTP_READY or automatic retry.
- Production-provider background-fail with held input: ERROR, one startup failure plus one truthful initial fetch success, m0/m1 retained with total count 64, zero queue depth/no admission/no HTTP_READY, no task creation in pump.
- Successful startup boundary: two task allocations, one startup success/HTTP_READY, initial success and usable-consumer prefetch. Later neighbor failure preserves page-zero content and READY.
- Additional temporary harnesses (outside repo): three startup modes each pass reset after reconciliation plus stale reports/duplicate first-report rejection; three modes each pass reset with startup report/boot completion pending, stale old-session completion rejection, and new-session closed admission until current first report. No extra startup success reopened a failed current session.
- All eight actual-LVGL headless runtime scenarios PASS: fast traversal, delayed focus, reversal, page during snap, push during snap, removal during snap, stale revision and queue pressure. They retain baseline known-defect expectations; this does not close other findings.
- Existing successful ESP HTTP firmware compile log assessed as L0 only. Not repeated because native/runtime checks showed no discrepancy.

## Evidence and remaining gap

L0: independent native compilation and architecture checks; assessed firmware build.
L1: independently executed actual production-C task-allocation injection/reducer tests, startup/initial exact terminal identity, retained neighbors, admission rejection, held-input and reset/stale boundaries.
L2: existing actual LVGL headless regression scenarios independently rerun with substituted provider. F007-specific allocation startup runtime/rendered evidence remains missing. The production provider startup tests use deterministic native HTTP/RTOS adapters; they do not establish FreeRTOS scheduling or actual ESP allocator/stack behavior, nor are startup-fault rendered visual evidence.
L3: missing representative target boot/background task-allocation/resource-pressure confirmation, stack high-water/largest free block and target UI/trace observation. Required resource/lifecycle evidence remains missing; no owner waiver exists. Keep F007 open for target evidence collection, while recording the source/host mechanism as independently verified.
