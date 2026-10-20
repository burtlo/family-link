<!-- orchestrate-build:begin run_id=20261019-LAB04-SOFTWARE-001 -->
# Orchestration progress

- Run ID: 20261019-LAB04-SOFTWARE-001
- Status: blocked
- Map: docs/labs/remaining-labs-implementation-plan.md (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Map/package fingerprint: plan SHA-256 above; selected definition LAB-04 section 3.1 plus sections 2, 8.1–8.8 and common package briefs
- Specification: docs/product/experiences/sketch-record-and-playback.md; docs/product/communication-contracts/message-delivery.md (OPEN-SKP-03 / COM-OPEN-002 context)
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a101f504907fc27a8862f321e1502c789f5f5c23
- Baseline protected paths: docs/labs/remaining-labs-implementation-plan.md (preexisting untracked, read-only); no other visible dirty paths
- Started: 2026-10-19 America/Chicago
- Updated: 2026-10-19 America/Chicago
- Scope: software only; selected L04-MODEL → L04-FW → L04-HOST → L04-QUAL; L04-DEVICE not selected
- External prerequisite: L04-11 corpus A verified from manifest and actual bytes: short.sketch 128 bytes / d2efa7e48bcb2eb021211d208acd231a05919edfb00d783dd9221720771d12ec; dense.sketch 40,968 bytes / 1bac193a2482c8a4f743d1bf6ada17080c87ca2d58f7964fb20d481664c50985. Source manifest SHA-256 e5a2bca61e22453f9cddea26fc2f0167a3de257510a45d780b7fc24c6282f82c.
- No commits, pushes, PRs, merges, publication, device flashing, or product-decision closure authorized or performed.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| L04-MODEL | Bounded model, fixtures, experiment contract | L04-11 external corpus verified | completed | pass | 1 | Fresh verifier passed focused production-C++ tests, malformed destination preservation, S-02 keys, hashes, unsupported checkpoint behavior, colored sentinel rejection, S-08/S-09/S-16; S-10 explicitly deferred to HOST parser boundary. |
| L04-FW | Device integration and instrumentation | L04-MODEL pass | completed | fail | 1 | Repair verifier reproduced success-marker mismatch (`variants=9 checkpoints=9` vs `variants=3 checkpoints=<0\|1>`), missing duplicate operation-id rejection, and README/schema mismatch with emitted trace fields. Repair cap exhausted; HOST and QUAL blocked. |
| L04-HOST | Parser, adapter, registry, builds | L04-FW pass | not_started | not_started | 0 | Blocked: prerequisite L04-FW has final fail verdict. |
| L04-QUAL | Standards review, regressions, report | L04-HOST pass | not_started | not_started | 0 | Blocked: prerequisite L04-HOST not started because L04-FW failed. No qualification report or standards review produced. |
| L04-DEVICE | Target execution/evidence | not selected | not_started | not_started | 0 | Not in execution scope; L04-05–L04-07 and other L2/L3 evidence remain pending |

## Package L04-MODEL: Bounded model, fixtures, experiment contract

- Definition fingerprint: selected brief = plan SHA-256 above + section 3.1, section 2, section 8.4 MODEL, section 8.5 L04-MODEL.
- Dependency evidence / waivers: L04-11 passed against actual LAB-03 manifest and asset byte hashes; no waivers.
- Implementer role and invocation: two fresh MODEL implementation invocations (initial + one repair).
- Implementer report: initial implementation added scaffold/model/fixtures/tests; one repair completed manifest keys, explicit unsupported checkpoint behavior and strict colored-format sentinel rejection.
- Baseline and changed paths: HEAD a101f504907fc27a8862f321e1502c789f5f5c23; only preexisting untracked map protected.
- Verifier role and fresh invocation: two fresh read-only verifier invocations, final after repair.
- Verifier verdict: pass after one repair.
- Findings and reproducible evidence: initial verifier failed on checkpoint, S-02 manifest keys and colored uncolored-sentinel acceptance. Repair verifier independently compiled/ran `python3 scripts/tests/test_lab04_model.py` (7 tests); a temporary C++ harness confirmed malformed coordinate decode preserves destination; JSON keys/scenario classes/ResourceBudget were checked; corpus fixture hashes and plan hash reproduced. Checkpoint reports unsupported at early/middle/late for every encoding. S-10 redaction/parser coverage explicitly remains HOST-owned.
- Repair attempts: 1/1; all in-scope findings addressed; no S-10 parser implementation was added outside scope.
- Commit/PR/publish actions: none.
- Final state: pass.

## Package L04-FW: Device integration and instrumentation

- Definition fingerprint: selected brief = plan SHA-256 above + section 3.1, section 2, section 8.4 FW, section 8.5 L04-FW.
- Dependency evidence / waivers: current independent L04-MODEL pass above; no waiver.
- Implementer role and invocation: two fresh FW implementation invocations (initial plus one repair).
- Implementer report: integrated model with manual/scripted ESP-IDF harness; repair added scenario terminal/count qualification, Corpus A fixture use, early/mid/late checkpoint observations, monotonic timestamps, and raw timing samples.
- Baseline and changed paths: baseline HEAD a101f504907fc27a8862f321e1502c789f5f5c23; changes stayed in LAB-04 firmware tree; protected plan/progress untouched by agents. No tracked/staged changes.
- Verifier role and fresh invocation: two fresh independent read-only verifier invocations, final after repair.
- Verifier verdict: fail after one repair; both v5.4.2 variant builds and 7 model tests pass.
- Findings and reproducible evidence: final verifier found success marker prints `variants=9 checkpoints=9` instead of frozen `variants=3 checkpoints=<0|1>`; event validator checks nonzero operation ID and terminal_count but not ID uniqueness; README still documents percentile/provenance fields unlike firmware raw trace, and trace schema expects `scenario_id`/`encoding` while firmware emits `scenario`/`variant`. Reproduce by inspecting success marker, `observe_completion`, `emit_trace`, README, and trace-schema.json.
- Repair attempts: 1/1 exhausted; no further package repair is permitted in this run.
- Commit/PR/publish actions: none.
- Final state: fail; dependent HOST and QUAL are blocked/not started.

## Package L04-HOST and L04-QUAL

- Definition fingerprints: frozen LAB-04 definitions in plan SHA-256 above, sections 3.1, 8.4–8.5.
- Dependency evidence: L04-HOST requires L04-FW pass; L04-QUAL requires L04-HOST pass. L04-FW final verifier verdict is fail, so neither dependent package was dispatched.
- Final state: not_started; blocked by failed prerequisite. The QUAL standards review, report, evidence-index row, and qualification ledger were not produced.
- Device status: L04-DEVICE not selected; L2/L3 capture, display, timing, memory, and performance criteria remain pending. No hardware verification or experiment/performance PASS is claimed.

<!-- orchestrate-build:end run_id=20261019-LAB04-SOFTWARE-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-SOFTWARE-002 -->
# Orchestration progress

- Run ID: 20261019-LAB04-SOFTWARE-002
- Status: blocked
- Map: docs/labs/remaining-labs-implementation-plan.md (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Map/package fingerprint: same frozen LAB-04 definitions as run 001; plan hash above
- Specification: docs/product/experiences/sketch-record-and-playback.md; docs/product/communication-contracts/message-delivery.md (OPEN-SKP-03 / COM-OPEN-002 context)
- Repository root: /Users/lynnfrank/src/family-link
- Baseline revision: a101f504907fc27a8862f321e1502c789f5f5c23
- Baseline visible paths: prior LAB-04 project and focused model test are untracked; prior orchestration progress and controlling plan are untracked. Ignored generated state includes root `build/`, LAB-04 build outputs, `dependencies.lock`, and `managed_components/`.
- Reauthorization: user explicitly authorized one further L04-FW implementation + fresh verification cycle after the previous repair cap. Only L04-FW source scope is reauthorized for that cycle; preserve the prior failed run record and other package evidence. If the next FW verifier still fails, stop with no additional FW repair in this run.
- Dependency evidence: L04-MODEL remains pass; plan SHA above and current model hashes are unchanged: model.cpp `97f797621fe154b1498606fbed70e57f6b131abe023a1ed0b58c72c0789aefd3`, model.hpp `ed26f1cd8dbc611d7ce2edc91480d9e241580dbe56e62aa28f2023aefc003f32`, model_test.cpp `63587614bf72d2a0fe3784ba7aff21f0ad15f78b93a1e87cafd83805794549b3`, Python harness `643b3e0b5c3924a286cf7bd0375c0ca4e5997e3873a2a819f62b37922678b09f`.
- Scope: software; resume at L04-FW, then L04-HOST → L04-QUAL if dependencies pass. L04-DEVICE remains not selected. `auto_continue=true`; `repair_limit=1` for newly started HOST and QUAL packages; one additional FW cycle is explicitly capped at this next implement/verify pair.
- No commits, pushes, PRs, merges, publication, device flashing, or product-decision closure authorized or performed.

| Package | Title | Dependencies | Implement | Verify | Additional repairs | State / evidence |
|---|---|---|---|---|---:|---|
| L04-FW | Device integration and instrumentation | Current L04-MODEL pass | completed | fail | 0 further after this cycle | Marker, duplicate IDs, and normal trace contract now pass. Verifier found one remaining preview-allocation failure trace with empty fixture, invalid under schema; this authorized implement/verify cycle is exhausted. |
| L04-HOST | Parser, adapter, registry, builds | L04-FW pass | not_started | not_started | 0/1 | Continue only on a current L04-FW pass. |
| L04-QUAL | Standards review, regressions, report | L04-HOST pass | not_started | not_started | 0/1 | Includes the required fresh read-only client standards review and confirmed in-scope fixes. |
| L04-DEVICE | Target execution/evidence | not selected | not_started | not_started | 0 | L2/L3 criteria remain pending. |

## Package L04-FW — additional authorized cycle

- Definition fingerprint: unchanged LAB-04 section 3.1, section 2, section 8.4 FW, section 8.5 L04-FW under plan SHA above.
- Dependency evidence: current L04-MODEL independent pass; hashes listed above.
- Prior verifier findings: completion marker currently emits `variants=9 checkpoints=9` instead of `variants=3 checkpoints=<0|1>`; operation-ID uniqueness is not enforced; README/schema disagree with emitted raw trace field names and percentile/provenance description.
- Implementer and fresh verifier: fresh implementation and fresh read-only verification completed.
- Baseline/protected state: prior LAB-04 implementation remained baseline; only `main/app_main.cpp`, `trace-schema.json`, `README.md`, and focused `scripts/tests/test_lab04_model.py` changed in this cycle. Model production files, controlling plan, and prior progress were preserved.
- Verifier verdict: fail. Reproduced one invalid trace row on the preview allocation failure path: `fixture` is left empty, but schema requires one of `corpus-a-short`, `corpus-a-dense`, or `corpus-b`. The row fails schema validation. Normal traces, marker contract, duplicate operation-id rejection, builds and model suite passed.
- Actual checks: 7 focused model tests pass; ESP-IDF v5.4 manual and scripted builds pass in existing isolated outputs; only scripted sets `CONFIG_LAB04_SCRIPTED=y`; JSON and `git diff --check` pass; map and MODEL source fingerprints unchanged.
- Additional implementation cycles authorized: 1; consumed. Stop with L04-HOST and L04-QUAL not started because L04-FW failed.
- Final verdict: fail; dependent HOST and QUAL remain blocked/not started.

## Packages L04-HOST and L04-QUAL

- L04-HOST dependency: L04-FW must pass. Current additional-cycle verifier verdict is fail, so HOST was not dispatched.
- L04-QUAL dependency: L04-HOST must pass. QUAL was not dispatched; required standards review, qualification report, acceptance ledger, and evidence-index row were not produced.
- L04-DEVICE remains not selected. L2/L3 evidence and target execution are pending; no hardware verification or experiment/performance PASS is claimed.

<!-- orchestrate-build:end run_id=20261019-LAB04-SOFTWARE-002 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-SOFTWARE-003 -->
# Orchestration progress

- Run ID: 20261019-LAB04-SOFTWARE-003
- Status: blocked
- Map: docs/labs/remaining-labs-implementation-plan.md (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Package fingerprint: same frozen LAB-04 section 3.1 and sections 2, 8.4–8.5
- Repository root: /Users/lynnfrank/src/family-link
- Baseline HEAD: a101f504907fc27a8862f321e1502c789f5f5c23
- Baseline working state: LAB-04 project and test additions from previous runs remain untracked; controlling plan and progress are protected untracked files; ignored ESP-IDF build output/dependency files are retained.
- Reauthorization: user explicitly authorized one further L04-FW implementation + fresh verification cycle. The only open FW finding is the preview-allocation failure trace's missing fixture identity. If the next verifier fails, stop with no additional FW cycle.
- Current prerequisite: L04-MODEL remains pass; production model source hashes unchanged (model.cpp 97f797621fe154b1498606fbed70e57f6b131abe023a1ed0b58c72c0789aefd3; model.hpp ed26f1cd8dbc611d7ce2edc91480d9e241580dbe56e62aa28f2023aefc003f32; native test 63587614bf72d2a0fe3784ba7aff21f0ad15f78b93a1e87cafd83805794549b3). Updated Python harness hash 5ba39450b97a569caa16fe5d8802a8b3344d216430dc1cb3d30726a6dd8983dc; the latest verifier re-ran all seven tests successfully.
- Scope: resume at L04-FW, then continue to L04-HOST → L04-QUAL only if dependencies pass. L04-DEVICE not selected. `auto_continue=true`; repair limit one per newly started package. No commits, PRs, publication, flash, or product decision changes.

| Package | Dependencies | Implement | Verify | Repairs | State |
|---|---|---|---|---:|---|
| L04-FW | Current MODEL pass | completed | fail | 0 further after this cycle | Preview allocation failure now has valid fixture identity. Verifier found fixture indexes outside 0–2 alias to Corpus B and can appear valid; this authorized cycle is exhausted. |
| L04-HOST | L04-FW pass | not_started | not_started | 0/1 | Proceed only on a current FW pass. |
| L04-QUAL | L04-HOST pass | not_started | not_started | 0/1 | Includes required fresh read-only standards review and qualification report. |
| L04-DEVICE | not selected | not_started | not_started | 0 | Hardware criteria pending. |

## Package L04-FW — preview allocation failure trace

- Prior verifier verdict: fail on one trace contract defect. `run_variant_one` preview allocation failure initializes a zeroed event and leaves `fixture` empty; `emit_trace` produces `fixture=`, violating trace-schema enum. `observe_completion` rejects it, so no false completion, but emitted evidence is invalid.
- This cycle's implementer and verifier: fresh implementation and fresh independent read-only verifier completed.
- Current source change: `main/app_main.cpp` only; previous fail-path fixture omission is fixed for valid fixture indexes.
- Verifier verdict: fail. `load_fixture()` and `fixture_name()` map every index outside `{0,1}` to Corpus B; index `3` therefore loads and reports valid `corpus-b`, which can satisfy completion. Reproduction: call `run_variant_one()` with fixture index 3. Required next fix: reject out-of-range indexes before loading or emitting a normal scenario event, and ensure they cannot satisfy the success inventory.
- Checks passed: all 7 focused model tests; independent manual and scripted ESP-IDF builds; Kconfig check; trace schema parse and regular trace/completion inspection; plan SHA-256; whitespace check. No device action.
- Additional cycle authorized in this run: 1; consumed. Stop; no further L04-FW implementation cycle is currently authorized.
- Final verdict: fail; HOST and QUAL remain not started/blocked by L04-FW prerequisite. Qualification report and standards review remain unproduced.

<!-- orchestrate-build:end run_id=20261019-LAB04-SOFTWARE-003 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-SOFTWARE-004 -->
# Orchestration progress

- Run ID: 20261019-LAB04-SOFTWARE-004
- Status: complete
- Map: docs/labs/remaining-labs-implementation-plan.md (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Package fingerprint: same frozen LAB-04 definition in section 3.1 and sections 2, 8.4–8.5
- Repository root: /Users/lynnfrank/src/family-link
- Baseline HEAD: a101f504907fc27a8862f321e1502c789f5f5c23
- Baseline visible state: LAB-04 source/model/test additions remain untracked from prior runs; the controlling plan and this progress file are protected; ignored build outputs and ESP-IDF dependencies are preserved.
- Reauthorization: user explicitly authorized three more L04-FW implementation + fresh verification cycles. Maximum three sequential pairs for FW in this resumed run; stop earlier if FW passes, and proceed to HOST/QUAL only then.
- Current prerequisite: L04-MODEL remains pass; its source fingerprints are unchanged and the latest verifier reran the seven-test harness successfully.
- Resume scope: L04-FW; on pass continue L04-HOST → L04-QUAL with auto_continue inherited. L04-DEVICE remains not selected. No commits, pushes, PRs, publication, device flashing, or product-decision closure.

| Package | Dependencies | Implement | Verify | Authorized cycles | State |
|---|---|---|---|---:|---|
| L04-FW | Current MODEL pass | completed | pass | 1/3 | Fresh verifier passed strict 0/1/2 fixture mapping and rejection of 3/255; prior marker, duplicate-ID, trace/schema, scenario, model test and both builds reverified. |
| L04-HOST | L04-FW current pass | completed | pass | 1/1 | Fresh repair verifier passed 18 combined HOST/MODEL tests, registry/help checks, both registered builds, marker 0/1 handling, S-10 pre-write rejection, and HOST acceptance conditions. |
| L04-QUAL | L04-HOST pass | completed | pass | 1/1 | Fresh repair verifier passed: 24 focused tests, both registered builds, full root suite 306 tests, corrected report hashes/counts, safe callback/queue lifecycle, and all applicable acceptance dispositions. L2/L3 remain pending. |
| L04-DEVICE | not selected | not_started | not_started | 0 | Hardware criteria remain pending. |

## Package L04-FW — fixture index validation

- Definition fingerprint: LAB-04 section 3.1 plus common and FW briefs; controlling plan hash above.
- Prior independent finding: `load_fixture()` and `fixture_name()` map every value other than 0 or 1 to Corpus B; index 3 can emit normal `corpus-b` traces and satisfy completion.
- Current baseline source fingerprint: `main/app_main.cpp` SHA-256 `6bbf544229fd67dfbad86cacfacbf5f273aac05288f36ed8ffc79bfd671e60cd`.
- Implementer and verifier: fresh implementation and independent verification completed; pass.
- Required outcome: reject fixture indexes outside 0–2 before loading or emitting a normal scenario row; ensure invalid values cannot satisfy any completion counters. Preserve valid 0/1/2 mapping and all previously passed marker, duplicate-ID, trace/schema, build and model checks.
- Authorized cycles consumed: 1 of 3; no more FW repair was needed.
- Final verdict: pass. L04-HOST is eligible.

## Package L04-HOST — parser, adapter, registry, builds

- Definition fingerprint: frozen LAB-04 section 3.1, section 2, section 8.4 HOST, and section 8.5 L04-HOST under plan SHA above.
- Dependency evidence: L04-MODEL pass and L04-FW pass from this run; model harness and both FW builds were freshly rechecked by the independent FW verifier.
- Implementer completed parser, adapter, registry and focused tests; the first fresh read-only verifier failed on valid `checkpoints=1`, sensitive PIN/media evidence leakage, and sandboxed manual build. One focused repair modified only parser/adapter/test. A second fresh read-only verifier passed: 18 combined HOST/MODEL tests; registry validation; config/help/operator help; manual and scripted registered builds; distinct SDK configs; sensitive-data pre-write rejection; reset/cleanup/malformed evidence gates; exact decided inputs and exploratory observations.
- Baseline and changed paths: HEAD `a101f504907fc27a8862f321e1502c789f5f5c23`, no tracked baseline edits. HOST changes are `client/labs/lab-04-sketch-format/tools/parse_lab_trace.py`, `scripts/lab_adapter_lab04.py`, `scripts/tests/test_lab04_host.py`, `config/labs.registry.yaml`, `config/labs/labs.registry.schema.v1.json`, and `scripts/lab_adapter_registry.py`; only the two lab build output dirs changed under ignored paths. The plan hash is unchanged. Protected earlier MODEL/FW files remain unchanged.
- Repair cycle: one of one authorized and consumed. Both completion marker checkpoint values, sensitive-data prevention, and registered builds independently pass.
- Required: parser/adapter/registry; decided-input exactness; redaction and sensitive payload rejection; cleanup/malformed evidence cannot false-pass; Make registry/help/build checks for all variants.
- Final verdict: pass (HOST), L04-01/L04-05 mechanism/L04-09 and applicable host conditions verified at L1/L0; no hardware claim.

## L04-QUAL standards review gate

- Review mode: fresh read-only client-code-review subagent; source snapshot fingerprints remained unchanged during review. No product/source edits made during review.
- Applicable active sources: client coding standard; LAB-04 plan; LAB-04 README/manifest/trace schema; sketch experience (SK-002/SK-004/SK-007 and EXP-SKP-001–006); message-delivery contract; metrics.v2/run-manifest.v2; LVGL budget foundation; labs configuration standard. `OPEN-SKP-01`, `OPEN-SKP-03`, `COM-OPEN-002`, checkpoint/performance/device policy remain open.
- Confirmed findings assigned stable IDs: LAB04-F001 scripted parser can pass while missing expected variant repetitions; LAB04-F002 adapter unconditionally asserts `firmware_trace_loss.status=OBSERVED` without parsed device evidence; LAB04-F003 metrics document claims v2 but omits `schema_version`, `parser_version`, and shared `evidence` fields; LAB04-F004 cross-task `volatile bool s_harness_failed` lacks synchronization.
- Plausible risk (not confirmed for implementation): vector/string allocation failure may not terminate cleanly; requires injected/target proof. Track as residual risk/evidence gap, do not make speculative policy changes.
- Findings LAB04-F001–F004 were fixed in the initial QUAL implementation. The initial QUAL verifier found stale source/test counts and two callback defects; the one allowed repair updated the report and callback lifecycle. Stable report IDs LAB04-F005 and LAB04-F006 describe callbacks reaching absent/deleted queues and synchronous console I/O inside input callbacks. The repair verifier passed all six outcomes.
- No-device/runtime evidence gaps: L2/L3, timing/memory, and target concurrency remain unverified. These must remain explicitly pending/UNMEASURED in report/index.
- QUAL verifier: first verdict fail. Findings: report parser SHA stale and summary counts inconsistent; callbacks could submit against missing/deleted `s_jobs`; queue-full branches called `std::printf`. First verifier passed 22 focused tests, registry/help checks, both builds, full `make test` (304 tests), source/fixture/acceptance checks, and scope review.
- Repair cycle: one of one QUAL repair rerun completed. Fresh verifier pass: 24 focused tests; registry/help; both registered builds; full `make test` 306 passed with loopback permission; report hashes, counters, source/fixture/binary identities reconciled; callback lifecycle under the pinned LVGL port mutex verified by source; no callback I/O. Initial sandbox run hit socket EPERM. No repair capacity remains.
- Final selected-package verdicts: L04-MODEL pass; L04-FW pass (one of three extra authorized FW cycles used); L04-HOST pass (one repair of one used); L04-QUAL pass (one repair of one used). No selected package is stale or blocked. L04-DEVICE was not selected.
- Qualification artifact: `docs/labs/evidence/lab-04/REPORT-LAB-04-v1.md`; evidence-index row in `docs/labs/evidence/README.md` marks software-qualified, hardware pending. Report source fingerprints: app_main.cpp `bd3cee0eb19e030242d1c5c2b2cfaf31632b2da7b93ac266334c4752721130db`, parser `edceaaed748f3cb49d871af6fd24fda7a86a5eaeb80d90cd7b0317c05bc2a95d`, adapter `2302d9a44b3cef5466d700c6be34e0ce00b47f4a0882587adad2947da001b579`; plan remains `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.
- Full suite generated untracked LAB-02 test evidence side effects absent from baseline; preserved without cleanup. These are under `docs/labs/evidence/lab-02/runs/` and include synthetic `20261019-eager-run-*`, `20261019-phaseb-run-*`, `20261019-scripted-phaseb-run-*`, and `w9-loss-sources-0e5d8b23`; no LAB-02 source was changed.
- Pending DEVICE criteria: L04-05 real reset-gated target run, L04-06 hardware timing percentiles, L04-07 simultaneous largest-free-block samples, and applicable L2/L3 evidence. Performance stays UNMEASURED; no experiment/performance PASS. OPEN-SKP-01, OPEN-SKP-03, COM-OPEN-002 and checkpoint/cache/format policy remain open. Allocation-failure behavior remains an unproven risk/evidence gap.

<!-- orchestrate-build:end run_id=20261019-LAB04-SOFTWARE-004 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-FW-BACKLIGHT-001 -->
# Orchestration progress — LAB-04 backlight follow-up

- Run ID: 20261019-LAB04-FW-BACKLIGHT-001
- Status: complete for the single selected L04-FW repair; target visual verification remains pending.
- Map: `docs/labs/remaining-labs-implementation-plan.md` (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Package: L04-FW only, under sections 3.1, 8.4 FW, and 8.5 L04-FW. No other packages were reopened.
- Repository root: `/Users/lynnfrank/src/family-link`; baseline HEAD `a101f504907fc27a8862f321e1502c789f5c5c23`.
- Baseline protected state: existing LAB-04 source/evidence and prior completion record; tracked changes in `config/labs.registry.yaml`, `config/labs/labs.registry.schema.v1.json`, `docs/labs/evidence/README.md`, and `scripts/lab_adapter_registry.py`; all LAB-02 synthetic evidence; plan and both user device-run records. Only the existing LAB-04 `main/app_main.cpp` and project `README.md` were authorized for this bounded follow-up.
- User evidence motivating repair: `20261019-manual-run-665361ec` boots, initializes the Box-3 LCD and touch stack, prints `LAB04_READY`, then captures no `LAB04_TRACE`; user reports a black screen and no available controls.
- Implementation finding: LAB-04 omitted `bsp_display_brightness_set(80)` after successful display startup. LAB-03, LAB-05, and LAB-10 use that initialization pattern.
- Implementer: fresh L04-FW invocation; changed only `client/labs/lab-04-sketch-format/main/app_main.cpp` and `client/labs/lab-04-sketch-format/README.md`. Added brightness 80 before LVGL screen setup and explicit manual instructions to locate/tap Points, Strokes, Preview, and Fault cases during the 30-second capture.
- Independent verifier: fresh read-only invocation, verdict **pass**. Recomputed app SHA-256 `db5046fae4b2f44303705d24a6e810c99cc3d028e4cc88d4ddce591f505e9243`; README SHA-256 `5120f4bf05c0fc911cb87a1650d6a71bc0bc2118c3d0b899b028372a6d87ab7a`; map SHA unchanged.
- Actual checks: isolated ESP-IDF 5.4.2 manual build at `/tmp/lab04-fw-verify-manual` passed, with scripted unset and build ID `lab04-fw-v1-manual`; isolated scripted build at `/tmp/lab04-fw-verify-scripted` passed, with `CONFIG_LAB04_SCRIPTED=y` and build ID `lab04-fw-v1-scripted`; `python3 scripts/tests/test_lab04_model.py` passed (10 tests); source inspection confirmed control positions, callback/worker ownership, trace publication, and cleanup; `git diff --check` passed.
- Device evidence: none generated. No flash, target operation, or visual confirmation. The new code builds and follows sibling BSP practice, but the screen being visibly lit and touch working are still pending. The user-run parser remains `workflow_status=FAIL`, `experiment_verdict=UNMEASURED` for its captured evidence.
- Prior selected package passes from run `20261019-LAB04-SOFTWARE-004` remain tied to that run's source snapshot. The qualification report still records the prior `app_main.cpp` fingerprint and has not been refreshed for this follow-up. No experiment/performance PASS or hardware verification is claimed. `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain open.
- No commits, pushes, PRs, merges, publication, hardware actions, or product-decision changes.

| Package | Implement | Verify | Repair attempts | State |
|---|---|---|---:|---|
| L04-FW | completed | pass | 0 | Bounded backlight and manual-instruction repair passed independent source review, both isolated ESP-IDF builds, and 10 model tests. Physical display/touch remains pending. |
| L04-MODEL / HOST / QUAL | not reopened | prior passes | 0 | Prior results belong to run 20261019-LAB04-SOFTWARE-004. QUAL report source fingerprint predates this FW follow-up. |
| L04-DEVICE | not selected | not_run | 0 | Still pending; no flash or target capture performed. |

<!-- orchestrate-build:end run_id=20261019-LAB04-FW-BACKLIGHT-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-FW-TRACE-COMPLETE-001 -->
# Orchestration progress — LAB-04 trace and completion repair

- Run ID: 20261019-LAB04-FW-TRACE-COMPLETE-001
- Status: complete for the selected L04-FW repair; target confirmation pending.
- Map: `docs/labs/remaining-labs-implementation-plan.md` (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Scope: LAB-04 section 3.1, shared requirements section 2 and 8.1–8.3, common FW brief section 8.4, L04-FW delta section 8.5. Only L04-FW reopened; MODEL/HOST/QUAL were not dispatched.
- Device evidence motivating repair: run `20261019-manual-run-f85d4bb7`. Serial has 35 trace rows, `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0`, then `app_main` returns. Parsing the unmodified serial reproduces `ParseError: unknown checkpoint position`; the `F-legacy` `legacy-colored` row had `checkpoint_position=ed`. User reported the status label appeared to stop at F-cancel and subsequent touches were not accepted.
- Root causes: `TraceEvent.variant[12]` was too small for `legacy-colored` copied with `strcpy`, overwriting the adjacent checkpoint-position field. After the full required scenario inventory, firmware intentionally removes controls, stops the worker, and returns, but showed no completion/reset instruction.
- Implementer: changed only `client/labs/lab-04-sketch-format/main/app_main.cpp` and `client/labs/lab-04-sketch-format/README.md`; introduced capacity 20 with compile-time guard and bounded `snprintf`, retained zero checkpoint position for the legacy row, displayed `Complete - reset to rerun` or `Stopped - reset to rerun` after worker cleanup, and documented the completion lifecycle.
- Independent verifier: fresh read-only verifier verdict **pass**. Source hashes: app_main.cpp `0bd7141f48274a8c57b4c0289940b3213d7e5cef8c80fda321ef0ebe21f95966`; README `bb574639b706742115655e4e6b93f520bb659bc97b8a9bc146276af02621a399`; controlling map hash unchanged.
- Actual checks: ESP-IDF v5.4.2 manual and scripted builds passed in their separate registered output directories; sdkconfigs confirmed manual `CONFIG_LAB04_SCRIPTED` unset and scripted `y`; `python3 scripts/tests/test_lab04_model.py` passed 10 tests; whitespace/scope/source review passed. The verifier reran the parser on the old, untouched run and confirmed it still fails with `unknown checkpoint position`.
- Evidence protection: run manifest SHA-256 `5852591f6d0a0a4df0301604d5cb29d5b211068acfe4eee53ab768e04e8ebd2f`; events SHA-256 `9a405edb4dc46bf529fdbac090a3c84dd210ee6813c4216ff8e4d41c72c6cb9a`; serial SHA-256 `046d7fae54ff822507e9a2d93a4b1152959b07e6aba9fcad1f4ae83e1a915c3f`. Evidence was not modified.
- Hardware state: no device flash/operation by agents; physical trace correction and completion display need a new user-run capture. Existing user run remains `workflow_status=FAIL`, `experiment_verdict=UNMEASURED`; no experiment/performance PASS is claimed.
- Prior MODEL/HOST/QUAL passes remain tied to run 004. The qualification report predates these follow-up firmware hashes and has not been refreshed. OPEN-SKP-01, OPEN-SKP-03, and COM-OPEN-002 remain open. No commits, pushes, PRs, merges, publication, or product-decision changes.

| Package | Implement | Verify | State |
|---|---|---|---|
| L04-FW | completed | pass | Fixed the reproduced trace corruption and clarified successful/failed completion. Both builds and focused tests pass. Hardware confirmation pending. |
| L04-MODEL / HOST / QUAL | not reopened | prior passes only | No package work in this follow-up; current QUAL report has older firmware fingerprints. |
| L04-DEVICE | not selected | not_run | New reset-gated capture and any target timing/resource evidence remain pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-FW-TRACE-COMPLETE-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-FW-MANUAL-GUIDE-001 -->
# Orchestration progress — LAB-04 on-screen guide and manual input guard

- Run ID: 20261019-LAB04-FW-MANUAL-GUIDE-001
- Status: complete for L04-FW software repair; the triggering user capture remains failed/unmeasured and hardware confirmation of the latest firmware is pending.
- Map: `docs/labs/remaining-labs-implementation-plan.md` (read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`)
- Scope: LAB-04 section 3.1, shared section 2 and 8.1–8.3, FW brief section 8.4, L04-FW delta section 8.5. No other package reopened.
- Trigger evidence: user run `20261019-manual-run-531b8cf0` has 70 unique trace operations: F-points 6 (expected 3), F-strokes 9 (expected 3), F-preview 3, checkpoints 18 (expected 9), F-malformed 18 (expected 9), F-overflow 6 (expected 3), F-cancel 6 (expected 3), F-legacy 4 (expected 2). It ended `LAB04_HARNESS_DONE status=failed variants_incomplete=1 reason=trace_or_terminal_mismatch`; no `worker_queue_overflow` trace. The extra scenarios match repeated button actions. Operation 65 exceeds the 64-ID terminal registry and sets the failure latch; this explains the stopped label. The capture compiled the previous firmware without one-shot controls.
- User request incorporated: the manual firmware previously presented controls without explaining the experiment. Added a startup guide directly to the display, separate from the dynamic status label, and matching README instructions. Guide describes comparing three encodings without selecting a product format; tapping Points, Strokes, Preview once each; tapping Fault cases only afterward (bad input, overflow, cancellation); waiting for completion and resetting to rerun.
- Input guard: each strategy can enqueue at most once per run; Fault cases is rejected with a missing-strategy status until all three have been selected, and accepted once only. Duplicate taps enqueue nothing. The previous legacy trace overflow fix and complete/stopped status remain.
- First independent verifier verdict: fail only because the initial guide wrapped to 10 lines and clipped on the 320x240 display. Repair limit 1/1 used to shorten/reflow. Final candidate: six explicit lines at x=8,y=124, width 304; Montserrat 14 line-height 16; glyph advances max 279.75px, 96px text height, ending y=220. Final fresh verifier verdict: **pass**, including manual interaction guards, trace/cleanup paths, guide fit, model tests, scripted build, and registered manual build.
- Final source hashes: app_main.cpp `28ea879077464d8de5817541b1726b152020b1e4398bd9e2090339df5de9ba2a`; README `bed0dea7bbb3536cf0aa3a7d3fe6189a9ebd8a385300e50ab5a32ce583c752fb`.
- Actual checks: `make lab.lab04.build` passed with ESP-IDF 5.4.2 after process-enumeration escalation; binary `build/labs/lab-04-sketch-format-manual/lab04_sketch_format.bin` SHA-256 `1046d9a7926906ece49a08ad7da72134f84377519dee149c88c4d94232875b3f`, size `0xb5f70`, 76% partition free. `make lab.lab04.scripted.build` passed; manual sdkconfig has `CONFIG_LAB04_SCRIPTED` unset and build ID `lab04-fw-v1-manual`; scripted sdkconfig has `CONFIG_LAB04_SCRIPTED=y` and build ID `lab04-fw-v1-scripted`. `python3 scripts/tests/test_lab04_model.py` passed 10 tests; `python3 scripts/tests/test_lab04_host.py` passed 14 tests; `git diff --check` passed. Initial verifier sandbox attempts failed in CMake process enumeration; the successful escalated manual build and read-only sdkconfig inspection enabled final verifier pass.
- Protected trigger run unchanged: manifest SHA-256 `c189c638b15ec3aca835be5b0ae3fd32355c1f515889b010f82033c1a7acb6af`; events SHA-256 `23890b8610afbadedd069d2f12d03da1987079a0a7d90903fccd922422551950`; serial SHA-256 `f283f16b59403dea477ea68a667fae5d5b78ec421ef1a5565e11ec9ac5480d42`. Run remains `FAIL` / `UNMEASURED`; it is not reinterpreted.
- No flash or device operation by agents. User must run the latest firmware to see the guide and validate hardware behavior. No experiment/performance PASS. QUAL report still contains an earlier firmware hash and has not been refreshed. OPEN-SKP-01, OPEN-SKP-03, and COM-OPEN-002 remain open. No commit/push/PR/merge/publication or product decision change.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-FW | completed | pass | 1/1 | Bounded one-shot input guard and visible startup guide implemented; both firmware variants and focused suites pass. Device confirmation pending. |
| L04-MODEL / HOST / QUAL | not reopened | prior pass only | 0 | Prior results remain associated with run 004. Current qualification report source fingerprint predates follow-up firmware changes. |
| L04-DEVICE | not selected | not_run | 0 | New reset-gated target capture and L2/L3 evidence remain pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-FW-MANUAL-GUIDE-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-QUAL-REFRESH-001 -->
# Orchestration progress — LAB-04 qualification refresh

- Run ID: `20261019-LAB04-QUAL-REFRESH-001`
- Status: complete for the single reopened package L04-QUAL. The prior MODEL, FW, and HOST passes remain documented above; L04-DEVICE was not selected.
- Map: `docs/labs/remaining-labs-implementation-plan.md`, read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b` (unchanged).
- Scope: refresh qualification for current LAB-04 source/build state, incorporate the user's actual manual and scripted run evidence, and fix confirmed in-scope review defects. No other lab or package was reopened.
- User evidence: manual run `20261019-manual-run-a28075c4` and scripted run `20261019-scripted-run-167d48e9` each report workflow PASS and experiment PASS on the exact decided inventory. Both contain 35 trace rows. Their manifests have no source/build identity; current-parser derivatives therefore retain `evidence=INCOMPLETE`, `firmware_trace_loss=UNKNOWN`, raster accounting `MISSING` on 35/35 rows, zero eligible raster timing samples, and `performance_verdict=UNMEASURED`. Captures predate C-1/C-3 source fixes. Original run directories and serial captures were preserved unchanged.
- Standards review: a fresh read-only client standards review found confirmed preview-accounting (C-1), raster-work accounting (C-3), and parser metrics/legacy compatibility (C-2) defects. C-1 and C-3 were implemented in source/trace reporting with focused coverage; C-2 added nearest-rank p50/p95/p99/worst summaries and truthful capture provenance while retaining compatibility with legacy rows. A fresh independent verifier confirmed all three fixes; its first C-2 review found the legacy rejection, repaired within the single authorized repair, then passed. Allocation-failure terminal behavior remains an unproven risk and was not changed speculatively.
- Implementer and report changes: current qualification report and evidence index refreshed; standards review and current-parser post-hoc metrics for both real captures saved under `docs/labs/evidence/lab-04/reviews/20261019-LAB04-QUAL-REFRESH-001/`. Original evidence was not overwritten. Product decisions OPEN-SKP-01, OPEN-SKP-03, and COM-OPEN-002 remain OPEN.
- Independent final verifier: fresh read-only verifier verdict **pass**, no findings. It confirmed acceptance dispositions S-01–S-22 and L04-01–L04-11, both manifest/evidence references and exact decided inputs, the report's limitations/open decisions, no device operation, and unchanged plan hash.
- Actual checks: focused LAB-04 host/model suite 31 passed; registry validation passed; lab help passed; full `make test` 313 passed; `git diff --check` passed; manual build passed after a sandbox retry requiring process-enumeration access; scripted build passed. Manual binary SHA-256 `1f2712bbf2c35a96eea3cd4bf5ba4b52165f8f8fb7c44b2edb115e7a41058fb4`; scripted binary SHA-256 `2146d44e60130d2b2ff02a0b9af6efe2a22402bfdc44d515b4720bb4b7522815`. No device was flashed or operated.
- Full-suite side effects: additional synthetic LAB-02 run directories were generated and left untouched; no LAB-02 source or evidence was cleaned or edited by this orchestration.
- Package verdict: L04-QUAL pass. The user's two runs have genuine workflow/experiment PASS results, but neither establishes performance PASS or complete qualification of current source. Software verification does not establish hardware verification.
- Pending device criteria: obtain reset-gated target evidence for current firmware, including C-1 preview accounting and C-3 emitted raster eligible/rendered/remaining counters; close L04-05 real reset-gated target evidence, L04-06 hardware timing percentiles, and L04-07 verified simultaneous-peak resource ledger. Bind future evidence to firmware source/build identity and determine trace-loss status. Performance remains UNMEASURED. OPEN-SKP-01, OPEN-SKP-03, and COM-OPEN-002 remain OPEN. No product decision was closed.
- No commit, push, PR, merge, publication, flash, hardware action, or original evidence modification.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-QUAL | completed | pass | 1/1 | Qualification report refreshed; legacy parser compatibility repair and current evidence limitations independently verified. |
| L04-MODEL / FW / HOST | not reopened | prior passes | 0 | Previous package passes remain recorded in earlier progress sections. |
| L04-DEVICE | not selected | not_run | 0 | Target recapture and L2/L3 evidence remain pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-QUAL-REFRESH-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-QUAL-DEVICE-RECUT-001 -->
# Orchestration progress — LAB-04 qualification recut with new captures

- Run ID: `20261019-LAB04-QUAL-DEVICE-RECUT-001`
- Status: complete for the single selected package L04-QUAL. L04-MODEL/FW/HOST prerequisite passes remain recorded in prior sections; L04-DEVICE was not selected.
- Map: `docs/labs/remaining-labs-implementation-plan.md`, read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.
- Frozen package definition: LAB-04 section 3.1, shared requirements section 2, common QUAL brief section 8.4, and L04-QUAL row section 8.5. Only the QUAL report/index/derived-review evidence was updated. All preexisting source, tests, registry changes, plan, older run evidence, earlier review folders, LAB-02 evidence, and the progress history were protected. Original run directories and raw captures remain unchanged.
- New user runs: `20261019-manual-run-243e8112` and `20261019-scripted-run-4587ca24`. Both manifests report workflow `PASS`, experiment `PASS`, parser `parsed`, post-reset capture, and harness completion; current-parser reparses preserve exact decided inputs `F-malformed`, `F-overflow`, `F-cancel` and 35 rows per run. All rows include preview/raster fields; each run has eight 28,800-byte preview observations and raster totals of 8,270 eligible, 206 rendered, 8,064 remaining. No work-cap violations were found.
- Qualification interpretation: runs supply new raw timing and heap observations and observed C-1/C-3 fields. They are still evidence `INCOMPLETE`; firmware trace loss is `UNKNOWN`; manifest source/build/sdkconfig/fixture identity fields are null, so these captures are not attributed to current binaries/source. Timing series are candidate evidence, not performance approval. Heap samples do not prove the required simultaneous decode-plus-live-image peak because `live_buffers=0` throughout. Performance remains `UNMEASURED`; no format approval or L04-DEVICE pass is claimed.
- Implementer changed only `docs/labs/evidence/lab-04/REPORT-LAB-04-v1.md`, `docs/labs/evidence/README.md`, and new derived review artifacts under `docs/labs/evidence/lab-04/reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001/`. The report and index include all `S-01`–`S-22` and `L04-01`–`L04-11` dispositions. `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain OPEN.
- First fresh read-only verifier: fail with one medium finding: README SHA-256 typo in new standards review. All other evidence, tests, builds, ledger, OPENs, and scope checks passed. One authorized repair (1/1) corrected only `standards-review.md`; the recorded digest now matches the current README.
- Final fresh read-only verifier: **pass**, no findings. It independently checked all raw artifact hashes, reparsed captures and compared derived outputs, reconciled preview/raster/timing/heap claims and report/index, and confirmed the README/source/map fingerprints and preserved OPEN decisions.
- Actual final-verifier checks: focused host/model suite `31 passed`; registry validation, LAB-04 help, both registered builds passed; full `make test` `313 passed` in 84.410 seconds after loopback permission was enabled following a sandbox socket-permission failure. Manual build also needed process-enumeration access after a sandbox denial. `git diff --check` passed in the initial verification; repaired review whitespace was checked directly. No device action.
- Full-suite side effects: generated additional LAB-02 evidence directories were left untouched. No cleanup was performed. No commit, push, PR, merge, publication, flashing, hardware operation, or product-decision change.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-QUAL | completed | pass | 1/1 | New real manual/scripted captures incorporated; review fingerprint repair and all software qualification checks independently passed. |
| L04-MODEL / FW / HOST | not reopened | prerequisite passes | 0 | Prior passes remain recorded; no source changes in this recut. |
| L04-DEVICE | not selected | not_run | 0 | Candidate raw timing/heap evidence is present. Source/build identity, firmware trace loss, simultaneous decode-plus-live-image peak, and independent DEVICE qualification remain pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-QUAL-DEVICE-RECUT-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-IDENTITY-PEAK-001 -->
# Orchestration progress — LAB-04 resource evidence and capture identity continuation

- Run ID: `20261019-LAB04-IDENTITY-PEAK-001`
- Status: **blocked** at L04-FW after its one authorized repair was consumed and the fresh verifier found a remaining failure-path ordering defect. HOST and QUAL were not started; L04-DEVICE was not selected.
- Map: `docs/labs/remaining-labs-implementation-plan.md`, read-only; SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.
- Scope: reopen LAB-04 MODEL and FW in dependency order to clarify emitted trace schema and resource observations for future captures. No other lab/package was selected. No flash/device operation.
- MODEL: current FW trace schema gained conditional supplemental event types, staling the earlier MODEL schema contract assertion. Implementer changed only `scripts/tests/test_lab04_model.py`; fresh independent verifier passed all 10 model tests, native production model/failure-publication binaries, corpus hashes, resource-envelope/scenario checks and schema conditional requirements. Draft 2020-12 runtime schema validator was unavailable (`jsonschema` package absent); focused contract tests passed. MODEL verdict: **pass**.
- FW initial implementation changed `client/labs/lab-04-sketch-format/main/app_main.cpp`, `trace-schema.json`, and `README.md`. It added a post-decode/pre-raster point resource sample while preview payload and decoded drawing are live; image/lifetime and decoded-vector capacity fields; Ledger A LVGL monitor data and Ledger B image/internal/PSRAM observations; plus a firmware publication-queue drop event whose claim excludes UART/host loss. It does not claim a measured peak or performance approval.
- First fresh FW verifier: **fail**. Found (1) stale MODEL test contract, resolved by the preceding MODEL package; (2) incomplete explicit Ledger A/B measurements, addressed in the single FW repair; (3) new supplemental rows are ignored by the existing HOST parser, reserved for a separate HOST package. MODEL test and resource ledger concerns passed final FW recheck.
- One authorized FW repair (1/1) added total/free/used/max LVGL monitor fields and decoded-vector capacity bytes, updated event schema/docs, and retained point-sample/no-UART-claim limits. Fresh read-only verifier passed both ESP-IDF v5.4.2 manual/scripted builds in distinct output directories, Kconfig/build IDs, 10 MODEL tests, live-object resource sampling review, event schema/source inspection and `git diff --check`.
- Remaining FW verifier finding (blocking): failure path emits `LAB04_HARNESS_DONE status=failed` before worker stop and before `publication_loss`, while the loss counter is emitted only after worker-stop/cleanup. There is no failure completion marker after the drained publication trace. Exact evidence: ordering in `client/labs/lab-04-sketch-format/main/app_main.cpp` around app-loop failure marker, stop/worker notification, and final publication event. Success path emits after the loss event. This can leave failure evidence incomplete/out of order.
- Repair limit exhausted: no further implementation attempt made. Per the user-authorized limit of one repair per package, stop here until a new L04-FW repair allowance is explicitly authorized. HOST depends on accepted FW; it was not launched. Existing prior HOST/QUAL passes are stale relative to the new FW event types and must be revalidated after FW passes.
- Actual checks: `python3 scripts/tests/test_lab04_model.py` — 10 passed; both separate ESP-IDF v5.4.2 builds passed with manual scripted symbol unset and scripted symbol enabled; Kconfig/build IDs and ELF strings matched. JSON syntax and `git diff --check` passed. Runtime Draft 2020-12 validation unavailable. No full `make test`, host parser verification, target capture, or device action in this FW follow-up.
- No commits, pushes, PRs, merges, publications, cleanup, flashing, or product decisions. All existing run evidence and LAB-02 side effects were preserved.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-MODEL | completed | pass | 0 | Trace contract regression test updated and independently verified. |
| L04-FW | completed | fail | 1/1 | Ledger sample added and builds pass; failure terminal currently precedes worker drain/final publication-loss row. Repair cap exhausted. |
| L04-HOST / QUAL | not started | stale after FW change | 0 | Must revalidate supplemental event parsing and refresh report after a passing FW result. |
| L04-DEVICE | not selected | not_run | 0 | No flash or target operation; all hardware qualification pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-IDENTITY-PEAK-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-HOST-QUAL-SUPPLEMENT-001 -->
# Orchestration progress — LAB-04 HOST telemetry support and QUAL refresh

- Run ID: `20261019-LAB04-HOST-QUAL-SUPPLEMENT-001`
- Scope: only selected LAB-04 MODEL/FW/HOST/QUAL packages. L04-DEVICE was not selected. The implementation map remained read-only at SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.
- MODEL prerequisite: pass in `20261019-LAB04-IDENTITY-PEAK-001`; 10 focused tests passed. Existing prior MODEL/FW/HOST/QUAL work and all unrelated LAB-02 artifacts were preserved.
- FW continuation `20261019-LAB04-FW-TERMINAL-ORDER-001`: implementer changed only `client/labs/lab-04-sketch-format/main/app_main.cpp`; fresh independent verifier passed. Final runtime marker follows callback removal, worker stop/drain, resource cleanup, firmware publication-queue loss summary. Manual/scripted ESP-IDF 5.4.2 builds passed; verifier source SHA-256 `a3ecbd8c49911949770b0e453c48c9fde697161dae3b51bfec0ceb3086f4583b`; binary SHA-256 values `4d6ade7ae323bbd7733f4ffe3e536e8cd95d0e1f99dbc91bb7197ab2c7fc74be` (manual) and `5efb2cac4478323f1b57701e6041c8f39dcdbfb6158747936252664371e15abc` (scripted). No hardware action.
- HOST `20261019-LAB04-HOST-SUPPLEMENT-001`: parser now validates and preserves point `resource_sample` independently from timing and reports firmware publication-queue rejections separately from end-to-end trace loss. Adapter binds fresh builds to source/fixture/sdkconfig/binary hashes and a fingerprint, and checks marker build ID. Legacy captures remain parseable but missing supplemental evidence stays incomplete. Independent verifier passed: HOST/MODEL 35 combined tests, registry validation, config/help checks, both registered builds, and `git diff --check`.
- HOST standards finding and repair 1/1: verifier found LAB-04 help advertised unsupported `BACKEND=local`. `scripts/lab_registry.py` now identifies managed-http as required for LAB-04; `scripts/tests/test_lab04_host.py` covers it. Fresh verifier passed 36 combined HOST/MODEL tests, LAB-04 help, LAB-02 help preserving its local-comparison instruction, and `git diff --check`. No other lab behavior changed.
- QUAL `20261019-LAB04-QUAL-SUPPLEMENT-001`: report/index and new derived review artifacts were refreshed. Original user runs `20261019-manual-run-243e8112` and `20261019-scripted-run-4587ca24` reparse as 35-row decided experiment PASS captures, but both predate current firmware and have null source/build/sdkconfig/fixture identity. They lack `resource_sample` and `publication_loss`; evidence remains `INCOMPLETE`, device resource samples are MISSING, firmware queue loss and end-to-end trace loss remain UNKNOWN, performance remains UNMEASURED, and no format/performance approval or DEVICE pass is claimed. OPEN-SKP-01, OPEN-SKP-03, and COM-OPEN-002 remain OPEN.
- QUAL independent verification passed report/evidence interpretation and recalculated derived metrics byte-for-byte. Focused HOST/MODEL suite passed (36), registry/config/help passed, both registered builds passed after reviewed process-enumeration retries, root `make test` passed (318), and `git diff --check` passed. No changes were made during verification.
- QUAL repair allowance 1/1 was used during implementation to correct a report framing defect and whitespace. The fresh verifier identified an unresolved audit defect: report/standards review state raw capture hashes were checked, but the report does not retain hash values for the selected raw artifacts. Current hashes for the files present under each original run directory are:
  - Manual manifest `ec06988f75640c49dbfa84de59177aa44fb7e824e6298d212113fe1144f902ea`; events `f1f30e3340825270f087cc4941b702bde1ec2020e70b2ccbbe2779f47b2e1ae2`; metrics `8e37eb803660f465c971ba26ec003e22a276003d71b1f4f1a06c08df766ee91b`.
  - Scripted manifest `201141a388e99d7466a1cfcb4c03303145b53807b3e3c052992534486284d772`; events `3c8f12261b2034df095db42b5eaff57814b173866703763b8db74c7348076d72`; metrics `443cd66c3cd1d2378f99fa656de51ce97ccb4118b99c295c90e6d633a8d57f9e`.
  The verifier could not establish a durable report baseline for raw serial files because the run folders contain only manifest/events/metrics. The report should correct the audit trail and include all available raw hashes if another QUAL repair is authorized. Package acceptance is blocked at the user-set one-repair limit; no second repair was started.
- Pending device criteria: current-source reset-gated recapture and source/build identity; C-1 preview and C-3 raster rows; L04-05 reset-gated target evidence; end-to-end trace-loss determination; L04-06 hardware timing percentiles; L04-07 verified simultaneous-peak resource ledger; L04-DEVICE qualification if later selected. Software completion does not verify hardware or qualify performance.
- No commit, push, PR, merge, publication, flash, hardware operation, original evidence modification, or product decision change. Root test generated LAB-02 directories which were left untouched.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-MODEL | prerequisite pass | pass | 0 | Conditional supplemental-schema contract independently checked. |
| L04-FW | completed | pass | 1/1 in previous run; 0/1 here | Runtime terminal ordering repaired and independently verified; target confirmation pending. |
| L04-HOST | completed | pass | 1/1 | Supplemental rows, future build identity, legacy compatibility, and LAB-04-specific help independently verified. |
| L04-QUAL | completed | pass with unresolved finding | 1/1 | Report refreshed, but raw artifact hash values are not recorded although review claims they were checked; repair limit exhausted. |
| L04-DEVICE | not selected | not_run | 0 | All current-source target and hardware criteria pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-HOST-QUAL-SUPPLEMENT-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-QUAL-CURRENT-CAPTURES-001 -->
# Orchestration progress — LAB-04 current-source target qualification

- Run ID: `20261019-LAB04-QUAL-CURRENT-CAPTURES-001`; follows user-provided current-source manual and scripted run results. The controlling implementation map is read-only and remains SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.
- Scope remained the selected LAB-04 MODEL/FW/HOST/QUAL packages. L04-DEVICE was not selected. MODEL/FW/HOST prerequisite passes are recorded in prior sections.
- Current user captures: `20261019-manual-run-9cc65b45` and `20261019-scripted-run-1acfdd59`. Both independent current-parser derivatives match the raw serial logs, each has 35 rows and exact decided inputs F-malformed/F-overflow/F-cancel, and both manifests report workflow PASS / experiment PASS with post-reset capture and cleanup.
- Build identity: both manifests match current source tree SHA-256 `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4`, fixture SHA-256 `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`, build IDs, sdkconfig and firmware binary hashes, build fingerprints, and READY markers. The report and audit ledger record hashes and byte lengths for all 8 current raw artifacts and all 8 accessible historical raw artifacts; original files remain unchanged.
- Current observed evidence: C-1 preview counters occur on 35/35 rows and eight observations per run account for 28,800 bytes each. C-3 counters occur on 35/35 rows with 8,270 eligible / 206 rendered / 8,064 remaining per run. Two post-decode/pre-raster `resource_sample` events per run show both image and decoded drawing resident; these are phase-specific point samples, not a simultaneous lifecycle peak. Firmware publication queue reports 0 rejected submissions; end-to-end firmware trace loss remains UNKNOWN and host journal loss is NONE. Evidence remains INCOMPLETE, performance remains UNMEASURED, L04-07 remains unproven, and no format/performance approval is made.
- QUAL implementer changed only `REPORT-LAB-04-v1.md`, the LAB-04 row in evidence `README.md`, and new derived artifacts under `reviews/20261019-LAB04-QUAL-CURRENT-CAPTURES-001/`. Prior review folders and raw evidence were preserved. Standards review corrected the earlier hash-audit gap by adding durable hashes and corrected a stale qualification status; confirmed changes are recorded in the new standards review.
- Checks by implementer: 36 focused HOST/MODEL tests passed; registry validation, `make config.labs`, `make lab.help`, `make lab.lab04.help`, and `git diff --check` passed. Root `make test` passed 318 tests in 103.642 s after the first sandbox attempt failed on 15 unrelated LAB-02/LAB-03 localhost bind permissions; loopback-enabled retry succeeded. Generated LAB-02 run directories were preserved.
- Fresh independent QUAL verifier: **PASS with verification limitation**. It independently reparsed both serials and compared JSON structures to derivatives, verified all 26 ledger path/hash/byte entries, build identity/markers, C-1/C-3, acceptance coverage S-01–S-22 and L04-01–L04-11, OPEN decisions, report semantics, and `git diff --check`; model tests 10, config/help checks passed. It could not start HOST tests or registry validation because `jsonschema` was unavailable in its execution environment. Those checks passed in implementation, and the full root suite passed. It made no edits. Git HEAD lacks untracked LAB-04 report/review artifacts and the broader workspace has prior LAB-04 and unrelated LAB-02 changes, so the verifier could not attribute every untracked artifact transition from HEAD alone; task log and evidence index align with reported scope. No content/evidence finding remained.
- Acceptance: L04-05 observed PASS at L2 for the captured reset-gated runs; L04-06 current-source timing series is recorded but remains unapproved pending budgets/independent DEVICE qualification; L04-07 not achieved. OPEN-SKP-01, OPEN-SKP-03, COM-OPEN-002 remain OPEN. No full hardware qualification, performance PASS, or product format selection is claimed.
- Pending device criteria: end-to-end trace-loss determination; approved performance budgets and independent L04-06 qualification; simultaneous lifecycle peak for L04-07; any selected L04-DEVICE work. No agent flashed hardware or operated the device.
- No commit, push, PR, merge, publication, original evidence modification, or unrelated cleanup.

| Package | Implement | Verify | State |
|---|---|---|---|
| L04-MODEL | prerequisite pass | pass | Trace schema contract and 10 focused tests pass. |
| L04-FW | prerequisite pass | pass | Manual/scripted software builds and terminal ordering pass; current-source target runs now observed. |
| L04-HOST | prerequisite pass | pass | Supplemental parsing, identity binding, legacy behavior, and help guidance pass. |
| L04-QUAL | completed | pass with limitation | Current-source captures and durable hash audit incorporated; independent verifier lacked jsonschema for two rerun commands but core evidence review passed. |
| L04-DEVICE | not selected | not_run | L04-07 simultaneous peak and performance approval remain pending. |

<!-- orchestrate-build:end run_id=20261019-LAB04-QUAL-CURRENT-CAPTURES-001 -->

<!-- orchestrate-build:begin run_id=20261019-LAB04-TRACE-PEAK-001 -->
# Orchestration progress — LAB-04 peak and trace-loss observability

- Run ID: `20261019-LAB04-TRACE-PEAK-001`; **Status: complete for selected software packages**. User authorized continuing software development until an operator/device action is needed. Resolved agenda is sequential: **MODEL** (freeze peak/footer event contract and test the production decode observation hook) → **FW** (sample the decode allocation peak with the preview image live; emit a final sequenced-row footer after drains) → **HOST** (reconcile sequence gaps, footer, publication queue loss, and write failures without false no-loss) → **QUAL** (refresh report/index and preserve all OPEN decisions). L04-DEVICE remains unselected.
- Frozen map is read-only, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`; HEAD at dispatch `a101f504907fc27a8862f321e1502c789f5f5c23`. Existing WIP and generated LAB-02 evidence are protected. Current source fingerprints: model header `ed26f1cd8dbc611d7ce2edc91480d9e241580dbe56e62aa28f2023aefc003f32`, model implementation `97f797621fe154b1498606fbed70e57f6b131abe023a1ed0b58c72c0789aefd3`, app `a3ecbd8c49911949770b0e453c48c9fde697161dae3b51bfec0ceb3086f4583b`, trace schema `cdc53d32153ea0ccf5c8f4f6bd98d3aef33afa24e527429cf1fb45c009841f7e`, parser `4fa8790a7bd895ed5afa8634c7749b5db393439986ad8ca67cfcbca0aa198ba4`, adapter `8c490a85ff34ce3fbc073f7d4ed523b6f216ce56049ddfa33db712ae1e272891`, model test `e3889eb2618f11ffb4f5ed5ab3fd877b63615e17eb0e2f844dd3fabc806acf1e`, host test `5cdb9b984692ec8a0c7e753e54a8f7b64099cfe7bee05c5b661877a7094f16ba`.
- Current evidence baseline: latest user runs `20261019-manual-run-9cc65b45` and `20261019-scripted-run-1acfdd59` both workflow/experiment PASS, source/build-bound; parser sees two post-decode/pre-raster samples but still marks peak as point-only; firmware publication queue 0, end-to-end trace loss UNKNOWN; performance UNMEASURED. L04-07 is not proven. Product format and performance budgets remain OPEN.
- Fresh client standards review `lab04_instrumentation_review` identified (a) confirmed L04-07 measurement gap: current point sample is not explicitly a peak observation; (b) confirmed trace observability gap: no final expected-row sentinel or sequence reconciliation, and `printf` failures ignored; (c) a temporary missing-manifest finding. Orchestrator disproved (c): `client/labs/lab-04-sketch-format/manifest.lab.json` exists and is asserted by `scripts/tests/test_lab04_model.py`. Review noted no product budget is approved; no budget or format choice is in scope. Model allocation source reserves max segment capacity before decode and keeps preview bytes live; instrumentation must preserve bounded behavior, distinguish firmware queue rejections from serial/host gaps, and report UNKNOWN if the final sentinel is missing.
- Dependency plan: MODEL trace contract/native callback test → FW observer and final footer/row-write accounting → HOST parser/adapter reconciliation and tests → QUAL report/index refresh. Fresh read-only verifier after each implementation; packages run sequentially. Software can establish the method and current-source capture gaps; actual L3 peak/loss values require a new user target run after all software checks. Do not flash or operate hardware.
- L04-MODEL: implementation completed; one bounded repair (1/1) renamed peak-only segment fields to `expected_output_segments` and `ledger_b_expected_output_segments`, explicitly reflecting reserved capacity before insertion. Existing `resource_sample.decoded_segments` fields remain unchanged. Fresh read-only verifier verdict: **pass**. Actual checks: `python3 scripts/tests/test_lab04_model.py` (10 passed), trace-schema JSON parse, fixture hash recomputation (short 128 bytes, dense 40,968 bytes), `git diff --check` plus no-index whitespace checks. Full JSON Schema Draft 2020-12 validation unavailable (`jsonschema` missing). The verifier confirmed observer source order, legacy compatibility, unchanged plan hash, and no wider whole-system peak claim.
- L04-FW implementer `lab04_trace_peak_fw_impl` completed, changed only `client/labs/lab-04-sketch-format/main/app_main.cpp` (SHA-256 `3e4827e43e4500a5d5a4be648c1654a1e91cf134364c14591dbded896a629ee2`) and `README.md` (`bd461a14a676b3a2caee43d4e8ab0807828dd50d3e700764b39f81aa3b35c585`). It captures the production reservation callback only after decode and semantic match; retains the preview bytes; emits Ledger A and B separately; checked bounded writes count API failures and fail the harness; after callback shutdown, worker/publication drain, and cleanup, emits queue-loss and trace-summary rows before final marker.
- Actual implementer checks: focused MODEL tests 10 passed; direct ESP-IDF v5.4.2 manual build passed in `/tmp/20261019-lab04-fw-peak-manual` (scripted unset, ID `lab04-fw-v1-manual`, binary SHA-256 `aa07fc90…2bd8b6`); scripted build passed in `/tmp/20261019-lab04-fw-peak-scripted` (scripted enabled, ID `lab04-fw-v1-scripted`, binary SHA-256 `a82644d5…90e650`); resolved sdkconfig hashes `c801c2f8…f2aa3e` and `0c37fd77…a40a4`; schema JSON and whitespace checks pass. First sandboxed attempt blocked at macOS process enumeration; retry passed. No hardware action.
- Fresh independent L04-FW verifier `lab04_trace_peak_fw_verify`: **PASS**. It independently built both ESP-IDF v5.4.2 variants into fresh `/private/tmp` outputs; effective sdkconfig hashes and manual/scripted symbols/build IDs matched. It reran 10 MODEL tests, schema JSON parse and whitespace checks and reviewed output accounting, cleanup ordering, callback gating, separate ledgers and documentation. Independent binary hashes differed due build metadata/path; both linked. Full JSON Schema validation unavailable to this verifier (`jsonschema` missing); implementer did pass Draft 2020-12 check. No hardware or edits.
- L04-HOST implementer `lab04_trace_peak_host_impl` completed and changed only `client/labs/lab-04-sketch-format/tools/parse_lab_trace.py` and `scripts/tests/test_lab04_host.py`. It reconciles contiguous sequences, expected row count and footer; separately reports publication queue drops and serial API errors; absent/malformed summary leaves loss UNKNOWN/evidence INCOMPLETE and cannot yield experiment PASS; legacy v1 remains parseable; peak metrics use expected reservation/capacity semantics. Actual implementer checks: HOST+MODEL 38 passed; registry/schema/help checks passed; manual and scripted make builds passed; root `make test` passed 320. First default root test attempt had 15 localhost bind errors in unrelated LAB-02/LAB-03; narrow permission-reviewed retry passed. Existing generated synthetic LAB-02 evidence was preserved.
- Fresh independent HOST verifier `lab04_trace_peak_host_verify`: **PASS**. It independently reran the 38 HOST+MODEL tests, registry/schema/help checks, both registered builds, and root test discovery. Root default execution reproduced 15 unrelated localhost permission errors; the narrow permission-reviewed retry passed 320 tests. Parser review confirmed missing/malformed summary is UNKNOWN/INCOMPLETE, queue/API/gap accounting is separate, and performance/L04-07 are not overstated.
- Client standards review by `lab04_current_client_standards_review` confirmed finding `LAB04-F001` (orphan/duplicate resource_peak could be surfaced as observed), and `LAB04-F002` (unhandled model allocator failure has no terminal operation path). Manual on-screen guide was not defective; OPEN format/palette/seek choices and L04-07/performance remain pending. Findings are from a fresh read-only review of current working-tree source.
- `LAB04-F001` finding fix is independently verified **fixed** by `lab04_peak_terminal_verify`. HOST parser binds every peak to exactly one successful `F-preview` terminal with matching operation ID, fixture and variant; orphan/duplicate/failed/wrong-variant peaks raise `ParseError`. Verifier passed 28 HOST tests and whitespace checks; no L04-07/performance claim was added.
- `LAB04-F002` allocation-failure finding is independently verified **fixed at L1 software level** by `lab04_alloc_failure_verify`. It passed 11 model tests with actual preview-reserve/encode failure injection; 28 HOST tests; and fresh manual/scripted ESP-IDF v5.4 builds with new configs (`CONFIG_COMPILER_CXX_EXCEPTIONS=y`, `-fexceptions`). Worker OOM failure reports one failure, latches harness failure, and does not emit a successful marker. No L2 target runtime was performed, so target confirmation remains explicitly pending under coding-standard §11; this is not a device PASS.
- Revalidation after the QUAL change: a fresh L04-MODEL verifier passed 11 model tests, corpus hash/semantic checks, Draft 2020-12 schema validation, exception-scope checks, and whitespace. A fresh L04-FW verifier independently built manual/scripted variants with current exception-enabled defaults, confirmed expected build IDs/config symbols/`-fexceptions`, reran model tests, and passed source/trace/cleanup review. FW: **pass**. L04-HOST source was unchanged by F002; its fresh verifier already passed after F001, and F002 verifier reran its 28-test suite.
- QUAL report/index refresh completed: `REPORT-LAB-04-v1.md` SHA-256 `d03b35e1158ccbee0f30f8bd4b1443d7a545fa6ad55c266859bd7a3abf37c834`; evidence index SHA-256 `765b481f57f799c785fbf75451aca211ffc7fdc194be6ff88c9f8adf9f2b5e6f`; durable standards-review artifact `reviews/20261019-LAB04-TRACE-PEAK-001/standards-review.md` SHA-256 `a903412a312da55dffdd22409114b9e6d4bf9c3c4ddd4a241366f4b72b66411e`. It retains original capture outcomes/hashes but says they bind to prior source SHA `1ffa7898...c1b4`; they are not current-source runs. F001 status: L1 fixed and independently verified. F002 status: L1 software implementation/test fixed; L2 target behavior still unverified, not waived. No new target evidence or experiment PASS. L04-05 current-source, L04-06/07, footer/trace-loss target evidence, performance budget/approval, and L04-DEVICE remain pending. OPEN-SKP-01/03 and COM-OPEN-002 stay open.
- Final QUAL implementation checks actual: 39 HOST+MODEL tests passed; Draft 2020-12 schema, registry, config, help, registered manual/scripted builds passed; root `unittest discover` default sandbox run had 15 unrelated LAB-02/LAB-03 localhost permission errors, while narrow localhost-permission retry passed 321/321 in 100.964 s; docs `git diff --check` and trailing-whitespace scan passed. Fresh independent final QUAL verifier `lab04_final_qual_verify`: **PASS**. It verified all report/index/review/map hashes, current source tree `93fa2323…c549` and fixture tree `05a0f684…3160`, no manifest bound to the current source, all 16 raw-artifact ledger entries, manual instructions, OPEN decisions, both registered builds, 39 focused tests, schema/registry/config/help checks, and root suite 321 passed after the localhost-only retry. It confirmed current L04-05/06/07, F002 L2, and performance remain pending/unmeasured.
- All selected software packages now have current verifier passes. Next, operator action is needed: current-source `make lab.lab04 KIT=<kit>` with the on-screen button sequence and `make lab.lab04.scripted KIT=<kit>`, then send the run IDs/results. These runs are not performed by any agent. L04-DEVICE remains unselected.
| Package | Implement | Verify | Repairs | Final state |
|---|---|---|---:|---|
| L04-MODEL | complete | pass (after QUAL fix revalidation) | 1/1 | Pass; 11 focused tests, schema/fixture/semantics verified. |
| L04-FW | complete | pass (after QUAL fix revalidation) | 0/1 | Pass; clean manual/scripted ESP-IDF builds with exception-enabled configs; trace and OOM failure paths reviewed. |
| L04-HOST | complete | pass | 1/1 | Pass; footer/sequence reconciliation, separate loss sources, peak-operation binding. |
| L04-QUAL | complete | pass | 1/1 | Software pass at L0/L1; standards review and fixes recorded; current target evidence pending. |
| L04-DEVICE | not selected | not_run | 0 | Pending; no hardware/device operation was performed. |

- No commits, pushes, PRs, merges, publications, flashes, or product decisions. Existing raw capture files and unrelated LAB-02 evidence were preserved.

<!-- orchestrate-build:end run_id=20261019-LAB04-TRACE-PEAK-001 -->

<!-- orchestrate-build:begin run_id=20261020-LAB04-MAIN-STACK-REPAIR-001 -->
# Orchestration progress — LAB-04 main-task stack repair

- Run ID: `20261020-LAB04-MAIN-STACK-REPAIR-001`
- Status: complete for the single reopened package L04-FW; board runtime confirmation is pending from the operator. The user-selected scope remains LAB-04 software only; L04-DEVICE was not selected.
- Map: `docs/labs/remaining-labs-implementation-plan.md`, read-only; current SHA-256 `22384571a65773284e391fe668e28a1bc9fc1acc5f27f5050b4abf923dc12e3b`.
- Triggering evidence: manual run `20261020-manual-run-87109e20` built from source fingerprint `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549`, fixture `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`, and binary `3715384a17bc207a8057267cd2a2d9bbb2ecde3c01f5023ce74d0ac917a55009`. Its preserved serial shows `LAB04_READY`, a successful F-points row, then a main-task stack overflow and reboot. Captured effective main stack was 3584 bytes. The run manifest and serial were not modified.
- Implementer changed only `client/labs/lab-04-sketch-format/sdkconfig.defaults` and `sdkconfig.defaults.scripted`, raising `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to 8192 in both profiles. No queue ownership, trace contract, or experiment behavior changed.
- Independent verifier initially could not rebuild manual because macOS denied ESP-IDF component-manager process enumeration. The orchestrator retried the registered manual build with local process-enumeration permission; it passed. A fresh independent verifier then passed: registered manual and scripted builds both resolve stack size 8192 and expected build IDs; focused HOST/MODEL suite passed 39 tests; firmware integration, callback/worker ownership, failure/cleanup paths, and preserved failed-run identity were reviewed. Verifier verdict: **pass** at software level. No flash or board operation was performed.
- New manual and scripted binaries were built; software build success does not prove the 8192-byte stack prevents target runtime overflow. User must run both current-source targets to provide runtime evidence. Performance stays `UNMEASURED`; no experiment PASS or hardware verification is claimed. The current qualification report should be refreshed against new run identities after target evidence is available.
- Changed firmware defaults SHA-256: `633065b9010a74295f9bc374148080c7274dbbc6b442eb9c834f11d66b968a23` and `5d0a45cb722d6947e2e60d81f5616d87eca482fa127cc42cbfd4e292e39aa9dc`.
- Actual checks: `make lab.lab04.build` passed after the permission retry; `make lab.lab04.scripted.build` passed; `bash scripts/make/python.sh run -m unittest scripts.tests.test_lab04_model scripts.tests.test_lab04_host` passed (39 tests). Current build binary SHA-256: manual `187704014b5e3f02ab4f29587f387e51b69f214c6c8ca418b214bdeed0a7c409`, scripted `234c6c69c7cf5e485a3152d9eef0f222f700920e9e36b9a0f9132ea9593f82e9`.
- No commits, pushes, PRs, merges, publications, hardware operations, or product-decision changes. Unrelated working-tree and evidence changes were preserved.

| Package | Implement | Verify | Repairs | State |
|---|---|---|---:|---|
| L04-FW | completed | pass | 1/1 | Main-task stack raised and both variants/tests verified at L0/L1; target runtime needs user retest. |
| L04-MODEL / HOST / QUAL | not reopened | prior passes | 0 | Preserve existing results; refresh QUAL evidence/report after current-source target runs. |
| L04-DEVICE | not selected | not_run | 0 | Target evidence pending; no hardware claim. |

<!-- orchestrate-build:end run_id=20261020-LAB04-MAIN-STACK-REPAIR-001 -->
