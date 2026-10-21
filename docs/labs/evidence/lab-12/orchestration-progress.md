<!-- orchestrate-build:begin run_id=LAB12-20261020-software -->
# LAB-12 software orchestration progress

- Run ID: LAB12-20261020-software
- Status: in_progress; final candidate builds/tests pass; software qualification is not closed
- Map: `docs/labs/remaining-labs-implementation-plan.md`
- Map/package fingerprint: `fb4a84be6bb224591d04f4bd1fc9282e565f66f8eca4602814787af5455736f1`
- Specification: section 3.8, shared requirements sections 1–2, execution contract sections 8.1–8.8, and linked profile/UI/engineering documents
- Repository root / worktree: `/Users/lynnfrank/src/family-link`
- Baseline revision: `4f1011e7b9736aedefbcb3bedb65131a629c478f`
- Baseline staged/unstaged/untracked paths: none
- Baseline protected paths: none visible; plan and governing documents are read-only inputs
- Hard prerequisite: L06-03 geometry; current focused test passed: `.venv/bin/python -m unittest scripts.tests.test_lab06_model.Lab06ModelTests.test_lab06_geometry`; L06 report records 320×80 upper / 320×160 lower slot geometry
- Frozen exploratory layout: `split-pane` in `fixtures/layout.json`, exploratory only; no visual approval claimed
- Frozen fixture probes: `reject_preview=rollback`, `offline work=retained`, message-color values `follow-profile` and `frozen-on-message`; all are exploratory; profile decisions COM-OPEN-001–005 remain OPEN
- Started: 2026-10-20
- Updated: 2026-10-21
- Invocation budget: original hard maximum 4 (integrated implementer, initial reviewer, grouped repair, and repair recheck) was explicitly extended by the user for one fresh read-only verifier. Total authorized invocations: 5. No subagent delegation.
- Continuation: automatic through all four software packages; DEVICE excluded and remains pending.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| L12-MODEL | Visual/shared models, fixtures, server; L12-02–05, L12-09 | Split-pane prerequisite passed | completed | pass | 0 | Fresh verifier passed the model and fixture layer; focused suite passes (20 tests). |
| L12-FW | Manual/scripted visual and shared paths; four variants | L12-MODEL | completed | fail; post-review grouped repair not independently checked | 1 grouped repair | Reviewer found false acknowledgement and lower-pane inset defects; both corrected and rebuilt. Current built-in font is Montserrat-Medium, not required weight 600. Four builds pass; post-review source change awaits independent review. |
| L12-HOST | Parser, adapter, 8771 service lifecycle, registration; L12-01, L12-06–07, L12-10 | L12-MODEL, L12-FW | completed | pass | 1 grouped repair | Stale generated sdkconfig refresh regression and LAB-12 help wording are fixed. Visual variants do not start the service; shared variants own the port 8771 fixture. |
| L12-QUAL | Standards review, final suite, report/index; two phase ledgers | Prior three packages | completed | fail; candidate repair not rechecked | 1 grouped repair | All four builds, focused tests, registry/help checks, and final `make test` pass (441 tests / 94.884 s). User target evidence remains FAIL. Report hashes and test count are reconciled. |

## Package L12: LAB-12 integrated software candidate

- Definition fingerprint: `fb4a84be6bb224591d04f4bd1fc9282e565f66f8eca4602814787af5455736f1`
- Dependency evidence: current LAB-06 L06-03 focused test passed; report evidence confirms slot dimensions.
- Frozen flow: integrated implementation, independent review, one grouped repair, repair recheck, plus one user-authorized fifth fresh read-only verifier. All five invocations are now used; no further reviewer invocation is authorized. That verifier returned FAIL. A second grouped source repair fixed the stale conflict acknowledgement and hint inset, and the 20-test focused suite, all four builds, and full suite (441 tests / 94.884 s) pass. These latest changes have no independent recheck.
- The independent reviewer also found the current LVGL font is Montserrat-Medium, not foundation weight 600; this remains unresolved. Current visual phase verdict is FAIL. Shared phase's false-acknowledgement code is repaired but awaits independent recheck; latest target evidence remains FAIL. LAB-12 report hashes and suite result now identify the current source/builds.
- Target evidence: latest user-supplied target run `20261021-scripted-shared-run-409224e4` is workflow PASS / experiment FAIL. No rerun after the PATCH response fix, and no device run or flash by this agent. Visual/manual and shared/target measurements remain pending. Required target work is post-reset evidence, plus update-latency and repeated open/preview/cancel/close resource-retention measurement; no device qualification is authorized in this software-only stop.
- Scope: LAB-12 tree, LAB-12 fixture server, new LAB-12 adapter/tests, LAB-12 block in shared registry/schema/adapter allowlist/index, and LAB-12 report. Shared registry/index changes remain only in this worktree. No edits to LAB-07/08/09/11, product client, or archive.
- Required outcomes: visual settings cards/modal/local preview/cancel/focus preservation/static three-card inbox return; shared two-endpoint conflict/rejected patch/lost response/invalid values/missed hint/offline probe; failure never presented saved; profile accent isolated from sketch stroke color; on-screen guidance; exploratory branches frozen and labeled; exact phase verdict sets; server only on explicit shared variants, port 8771; four variant builds; focused development checks/incremental builds; final full suite once; independent report with separate visual/shared sections; DEVICE evidence pending. Supplement: distinct acknowledged state, preview, immutable in-flight update; bind results to user/session, operation and revision; exercise real fixture HTTP through firmware-used implementation, including lost-response uncertainty and delayed/out-of-order completions; preserve focus/modal and clean repeated lifecycle resources; reusable reconciliation API remains separable from exploratory settings/policy; document update-latency/resource-retention device measurements and that visual-only has no shared-save proof.
- Commands: `make lab.lab12.build`; `make lab.lab12.build VARIANT=scripted`; `make lab.lab12.build VARIANT=shared`; `make lab.lab12.build VARIANT=scripted-shared`; `.venv/bin/python -m unittest scripts.tests.test_lab12_model scripts.tests.test_lab12_host`; `make test` passed on the initial repaired candidate (441 tests / 98.314 s) and was repeated after relevant source/test changes (441 tests / 94.884 s).
- Commit/PR/publish/flash actions: none.

<!-- orchestrate-build:end run_id=LAB12-20261020-software -->
