# Independent F010 verification

Target: LAB-02 bounded inbox. Baseline HEAD 21a37df4b220eabcf6b3717601753cc8cf804360. Fresh read-only verifier distinct from fixer. No implementation, maintained-document, index, private-config or hardware changes. Commands/artifacts and exact source hashes are adjacent.

## Disposition

F010 implementation is correct and independently verified by exact source trace plus actual-LVGL header/runtime checks. Physical centered-overlay disappearance remains **unverified at L3**; no waiver is inferred. Suitable for the owner-authorized scoped phase commit provided the outcome explicitly retains that target evidence gap. Do not mark full physical/lifecycle closure.

## Requirements and authority

Read full client-code-review skill/canonical workflow, docs/AGENTS.md, full client coding standard, target README/manifest, carousel remediation plan, session architecture, original F010 review and execution owner decisions. Coding standard sections5/7/11/12 govern property updates, truthful failure UX, evidence levels and coherent change scope. The plan is draft; explicit owner F010 removal request and approved APP-O-01/04/05 govern this narrow change. APP-O-01 loading/error cards and APP-O-04 initial error click-retry remain required. No new product behavior introduced. Other identity/resource/timing findings stay outside this unit.

## Original trigger and causal proof

Original lab_main.c calls lab02_board_status_set("loading inbox…") immediately after successful non-eager HTTP carousel initialization. board.c:43-60 lazily creates a separate active-screen child, aligns it at LV_ALIGN_CENTER and changes its text. There is no normal successful manual HTTP completion call to clear it. Thus the label survives while the controller header updates to ready/cached information. Independently inspected every remaining call: manual startup now only creates board status on eager init failure, carousel init failure or session-pump task failure. Auto harness calls show AUTO step text and clear at terminal; they do not reintroduce loading-inbox text. The separate controller count label is aligned TOP_MID and clicks retry only from HTTP ERROR.

Exact-byte proof shows production diff equals only deletion of successful-start loading branch and substitution of shell header text with LAB-02. Failure calls, error/header branches, READY/cached branches, card loading/failure/ellipsis fallback, initial retry callback, snapshots, geometry and buffer configuration are unchanged. Removing the label creation adds no handle dereference, terminal outcome, allocation, blocking path or work-loss mechanism.

## Independent commands and results

Fresh /tmp CMake build uses vendored real LVGL and current production controller/session/provider plus explicit host RTOS/HTTP adapters. Configure/build PASS. Runtime commands in runtime-commands.json all exit0:
- retry-success: initial and clicked-retry shell labels exactly LAB-02; actual label click admits new attempt; terminal header 1/64 (2 loaded) One; READY reached.
- retry-boot-fail: retry shell exactly LAB-02; terminal inbox unavailable preserved, no automatic retry.
- delayed-focus: existing normal loading-card render gate PASS, loaded terminal frame saved.
- failed-page: independently added temporary boundary assertion verifies session remains READY while focused failed-page header shows 17/64 tap card to retry; visible error card and retained valid neighbor checks PASS. No loading-inbox copy remains.

Assessed final firmware-build.log, exact after-source hashes and successful link/image/size result: HTTP manual non-eager ESP-IDF build PASS. No change or unexplained failure warrants repeating that build. git diff --check PASS. Tracked target/evidence hashes and porcelain status match before/after; index binary diff unchanged. Frozen source and root-owned WIP preserved.

## Evidence levels and limitations

L0: production firmware build and exact source/caller proof. L1/L2: actual LVGL shell/retry/ready/error/header and focused-page failure paths, production session/provider with task/HTTP adapters, plus delayed-card rendering. Harness does not execute lab_main.c, physical board.c startup or FreeRTOS/device scheduling. Centered-board-label absence is source proof, not a scripted hardware startup reproduction. No physical screenshot/flash/owner confirmation was collected after the fix. F010's physical overlay claim therefore requires target confirmation and remains unverified; no L3 claim or waiver. Cached header behavior is unchanged by exact diff; direct boundary run covers READY with focused-page failure, not a separate cached-refresh scenario.
