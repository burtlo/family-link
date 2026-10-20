<!-- orchestrate-build:begin run_id=20261019-LAB05PEAK-SOFTWARE-001 -->
# Orchestration progress

- Run ID: 20261019-LAB05PEAK-SOFTWARE-001
- Status: complete
- Map: docs/labs/remaining-labs-implementation-plan.md (read-only; SHA-256 `b1de0a1fbff300d276e380e1a20b0ebc8290a6495664d20ee1e47cfcbabb32d6`)
- Map/package fingerprint: lab05peak-def `29a0f38f324062bd8ad6d91e0330cefbb38660fff642645557e73e768344b855` (sections 2, 3.2, 8.4, L05P rows in 8.5, uniqueness note)
- Specification: self-contained with section 3.2, sections 1–2 and 8, coding standard, labs configuration, repository layout, and TRANSITION-COVER-DOWN (draft pattern, not a pass limit)
- Repository root: C:\Users\lynnv\src\family-link-lab05
- Baseline revision: 9b20c064f7f0715b316f9eb470c42f07c0421f15 (branch main)
- Baseline protected paths: no staged, unstaged, or untracked files visible. Gitignored and otherwise protected: `.venv/`; `config/deployment/local.yaml`; preexisting `build/labs/lab-02-bounded-inbox`, `build/labs/lab-02-bounded-inbox-eager`, `build/labs/lab-02-bounded-inbox-scripted`; `docs/labs/evidence/lab-05/`; this map; `docs/product/`; `client/device/`; other labs’ firmware, servers, and evidence; `poc-v1/`.
- Started: 2026-10-19T22:40-05:00
- Updated: 2026-10-20T08:20-05:00
- Execution scope: software. Selected L05P-MODEL → L05P-FW → L05P-HOST → L05P-QUAL. L05P-DEVICE not selected.
- Resolution: bare LAB-05 resolved to the LAB-05 peak addition. Product `lab_id` stays `LAB-05`. New make name is `lab05peak`. Existing `lab05` registration stays runnable and is not the work.
- Kit: only `audrey` (`audrey-kit`) may be named. No flash, capture, or device bind. `make flash`, `make flash.all`, `make device.bind`, and `make lab.lab05peak` with flash enabled are forbidden.
- auto_continue: true. Repair limit: 1 per package. No commits, pushes, pull requests, or merges.
- Hard prerequisite: lifecycle manifest, variants, markers, and `lab05` registration match section 3.2 (inspected at HEAD). `lab05` omits `adapter`, `evidence_slug` `lab-05`, `project_dir` `client/labs/lab-05-transition-proof`, variants `manual` and `scripted` only, build dirs `build/labs/lab-05-transition-proof-manual` and `-scripted`. Manifest `lab_revision` is `v1-proof` and has no `peak` object. `app_main.c` prints `LAB05_READY`. No waiver.

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| L05P-MODEL | Additive peak model, fixtures, experiment contract | lifecycle baseline matches 3.2 | completed | pass | 0 | Fresh verifier passed native production-C tests, L05-02, L05-08, L05-12, S-02, S-08, S-09, S-16, S-17. S-10 parser deferred to HOST. |
| L05P-FW | Peak device integration and instrumentation | L05P-MODEL pass | completed | pass | 0 | Fresh verifier passed six ESP-IDF v5.4.2 idf.py builds, sdkconfig symbols, peak-only markers, lifecycle #else branch, and 17 model tests. L05-09/L05-10 remain unmeasured on device. |
| L05P-HOST | Peak parser, adapter, registry, builds | L05P-FW pass | completed | pass | 0 | Fresh verifier passed registry gates, parser/adapter tests, and seven make builds on ESP-IDF v5.4.2. L05-05–L05-07 remain L1 mechanisms. |
| L05P-QUAL | Standards review, regression, software report | L05P-HOST pass | completed | pass | 1 | Repair verifier passed `make test` (359 tests, OK). Index row is software-qualified, hardware pending. |
| L05P-DEVICE | Target execution and evidence | not selected | not_started | not_started | 0 | Omitted from this software run. L2/L3 rows stay pending. |

## Package L05P-MODEL: Additive peak model, fixtures, experiment contract

- Definition fingerprint: `a55f69c51c0d28b590b61db51199080e530760075a52d885668a384bd5eef063`
- Dependency evidence / waivers: lifecycle baseline recorded above. No waiver.
- Implementer role and invocation: fresh general-purpose implementer `db097659-33bd-488e-9b9b-0f395f4a850a` (not resumed)
- Implementer report: status completed. Added peak domain model, fixtures, trace schema, and host-compiled native tests. Lifecycle manifest stays v1-proof. peak.lab_revision v1-peak, peak.evidence_slug lab-05-peak. steady_live_images 1. Command `.venv\Scripts\python.exe -m unittest scripts.tests.test_lab05_peak_model -v` exit 0, 17 tests. commit_created false.
- Baseline and changed paths: HEAD `9b20c064f7f0715b316f9eb470c42f07c0421f15`. Tracked edit: `client/labs/lab-05-transition-proof/manifest.lab.json` (additive `peak` object only). New files: README, `components/lab05_peak/**`, `fixtures/**`, `peak-trace-schema.json`, `tests/test_lab05_peak_native.c`, `scripts/tests/test_lab05_peak_model.py`, plus this orchestrator progress file.
- Verifier role and fresh invocation: fresh read-only general-purpose verifier `83b83f1e-9f6c-4f36-8cc4-90a0df3791cf`. Resume was not used. Task has no `fork_turns` parameter; a new invocation without `resume` is the fresh context.
- Verifier verdict: pass
- Findings and reproducible evidence: no findings. Verifier recomputed map SHA-256 `b1de0a1fbff300d276e380e1a20b0ebc8290a6495664d20ee1e47cfcbabb32d6`, reran the 17-test unittest (0.444s, OK), independently compiled `lab05_peak_model.c` with gcc, and checked handles, cancel (`live_image_count=1`), overflow (count 8, dropped 0), one terminal, single writer, fixture sizes 28800 / hashes `9b144c64f65dfdb0a427bf1c059cc29350b5c04efa6660dc568e1c35474b551d` and `383541a4ebe15b202f940b9668ca52bf153fd0eea809ce4017a9bcf807fed781`, decided vs exploratory inputs, CMake exclusion, and unchanged `app_main.c` / lifecycle sdkconfig. S-10 parser exemption accepted; secret scan clean.
- Repair attempts: 0
- Commit/PR/publish actions: none
- Final state: pass

## Package L05P-FW: Peak device integration and instrumentation

- Definition fingerprint: `5564ad07d84052b08a48e557ba556ff76c57ab0f2a1464468b74ca416fff71f5`
- Dependency evidence / waivers: L05P-MODEL current pass. No waiver.
- Implementer role and invocation: fresh general-purpose implementer `589ee3d6-cca2-4587-927f-a5541169186f` (not resumed)
- Implementer report: status completed. Six idf.py builds for exact tag v5.4.2. Peak elfs contain `LAB05_PEAK_READY` and `LAB05_PEAK_HARNESS_DONE`; lifecycle elfs contain `LAB05_READY` and neither peak string. sdkconfig symbols match the six variants. `CONFIG_LV_MEM_SIZE` stayed 65536. Model tests 17 OK. commit_created false. No flash.
- Baseline and changed paths: still HEAD `9b20c064f7f0715b316f9eb470c42f07c0421f15`. Additional tracked edits: `CMakeLists.txt`, `main/CMakeLists.txt`, `main/app_main.c`. New: `sdkconfig.defaults.peak`, `sdkconfig.defaults.peak.replace`, `lab05_peak_harness.c`, `lab05_peak_harness.h`. Model sources and `scripts/tests/test_lab05_peak_model.py` refined. Lifecycle `sdkconfig.defaults` and `sdkconfig.defaults.scripted` unchanged.
- Verifier role and fresh invocation: fresh read-only verifier `4b7862c3-1d1d-4288-8066-1c5616e2b122`. Resume was not used.
- Verifier verdict: pass
- Findings and reproducible evidence: no findings. Verifier activated ESP-IDF through `scripts/firmware.py` (`git describe` exact `v5.4.2`, checkout `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`) and reran all six `idf.py` builds (exit 0). Elf byte counts: lifecycle peak markers 0 and `LAB05_READY` 2; peak images peak markers 2 and `LAB05_READY` 0. `#else` in `app_main.c` is the prior lifecycle body. Peak C sources compile only when `CONFIG_LAB05_PEAK` is set. Harness uses queue capacity 8, 20 distinct overlap and replace operation ids, fail-closed ready line, and `metrics_provenance=instrumented`. Native suite 17 tests in 0.459s. esp_lcd `-O1` workaround is project-local and does not change lab source optimization. No commit.
- Repair attempts: 0
- Commit/PR/publish actions: none
- Final state: pass

## Package L05P-HOST: Parser, adapter, registry, reproducible make builds

- Definition fingerprint: `92e420ad43aee6f8aaca9a3fbebe733b0e6dc034eaea1a47b8ec5b1ca6056be7`
- Dependency evidence / waivers: L05P-FW current pass. No waiver.
- Implementer role and invocation: fresh general-purpose implementer `94f16558-d216-4190-9c18-fb5305bf8948` (not resumed)
- Implementer report: status completed. Registered `lab05peak` without removing `lab04`. Parser `tools/parse_lab05_peak.py`, adapter `scripts/lab_adapter_lab05peak.py`, uniqueness gate in `scripts/lab_registry.py`. Host tests passed. Seven make builds exit 0 on ESP-IDF v5.4.2. No flash. commit_created false.
- Changed paths: `config/labs.registry.yaml`, `config/labs/labs.registry.schema.v1.json`, `scripts/lab_adapter_registry.py`, `scripts/lab_registry.py`, `scripts/lab_adapter_lab05peak.py`, `client/labs/lab-05-transition-proof/tools/parse_lab05_peak.py`, `scripts/tests/test_lab05_peak_host.py`. Lifecycle `lab05` entry unchanged in role (generic, no trace parser, variants manual and scripted).
- Verifier role and fresh invocation: fresh read-only verifier `593eb852-1adf-4e20-8006-b30fffdb2235`. Resume was not used.
- Verifier verdict: pass
- Findings and reproducible evidence: no findings. Host suite 13 tests OK. Model suite 17 tests OK. Live enum keeps `lab04` and adds `lab05peak`. `make lab.lab05peak.scripted.build VARIANT=replace` stayed on variant `scripted`. IDF exact tag `v5.4.2`. Makefile diff empty. No evidence run directory. L05-05–L05-07 not claimed as L2.
- Repair attempts: 0
- Commit/PR/publish actions: none
- Final state: pass

## Package L05P-QUAL: Standards review, regression, software report

- Definition fingerprint: `63733fd6eec5e342bf70065e485eeb5f49cab6ce10c3593bff1c1c2322958ae1`
- Dependency evidence / waivers: L05P-HOST current pass. No waiver.
- Firmware source fingerprint before standards review: owned tree SHA-256 `a07888859254ccc89369123b969df5ac359b188d2a3245f1e5df223f3613a380` (26 files, excluding `managed_components` and `__pycache__`). HEAD `9b20c064f7f0715b316f9eb470c42f07c0421f15`. Branch `main`. Mode: review. Included: uncommitted WIP in the lab tree.
- Standards reviewer: fresh read-only reviewer `a640c800-8993-4f81-b8ba-9ce488e69f39`. Resume was not used. Review only; no fixes attempted. Native model tests 17 OK.
- Confirmed in-scope defects, one QUAL assignment, no dependency ordering required: LAB05PEAK-F001 (R1, P1) worker rows make the parser reject `P-cancel` and inflate overlap repetition counts; LAB05PEAK-F002 (R2, P1) `P-alloc-fail` stamped `control=replace` demotes a persistent log; LAB05PEAK-F003 (R3, P2) pixels freed when display lock fails; LAB05PEAK-F004 (R4, P2) page worker mutates widgets and drops a command on lock timeout; LAB05PEAK-F005 (R5, P2) cover allocation failure leaves the animation in progress; LAB05PEAK-F006 (R6, P2) unsampled heap emitted as instrumented zeros; LAB05PEAK-F007 (R7, P3) full operation ledger can complete a cover without a terminal; LAB05PEAK-F008 (R8, plan §1.7) a replace-only arm must not become experiment `FAIL` merely because decided scenarios are absent — use `UNMEASURED` or `INCOMPLETE`.
- Not fix work: R9 hang-bound versus timeout (plausible; document), R10 frame-cap truncation (plausible), R11 audio flag before rejected publish (plausible), R12 persistent-versus-replace and cover-down visual remain OPEN.
- Implementer role and invocation: fresh general-purpose implementer `b7b0ff64-b27e-4128-8761-7006a244a8fd` (not resumed)
- Implementer report: status failed. Focused peak tests 38 OK. Four peak make builds exit 0 on ESP-IDF v5.4.2. `make test` exit 1, 348 tests, 3 failures, 7 errors. Report `docs/labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md` does not claim software qualification. commit_created false.
- Unexpected paths after that assignment, not present after HOST: `docs/labs/evidence/lab-02/runs/20261020-eager-run-1a2a2ecc/`, `docs/labs/evidence/lab-02/runs/20261020-phaseb-run-cd678dd3/`, `docs/labs/evidence/lab-02/runs/20261020-scripted-phaseb-run-34dc285f/` (manifest and events only). Left in place. Not reverted.
- Verifier: not started on the first attempt. A failed implementation report plus unexpected other-lab evidence stopped the package.
- Repair attempts: 1 authorized by the user on 2026-10-20. The authorized defects were the root `make test` failures and the three leaked `docs/labs/evidence/lab-02/runs/20261020-*` directories.
- Repair implementer: fresh general-purpose implementer `d13a0793-d4ee-414a-a24d-c517e61cda0f` (not resumed). Status completed. `make test` reproduced 4 failures and 7 errors, then exited 0 with 359 tests. The three leaked run directories were removed. Peak firmware was not rebuilt. commit_created false.
- Repair verifier: fresh read-only verifier `cfdb0183-40e0-4317-8b40-36155feb7ebe`. Resume was not used. Verdict pass. Independent `make test` exit 0, `Ran 359 tests in 119.278s`, OK. Peak unittest 38 OK. Registry validate exit 0. The three leaked directories stayed absent and the verifier's run wrote no new evidence directory. Forbidden paths and `docs/labs/evidence/lab-05` were unchanged. HEAD stayed `9b20c064f7f0715b316f9eb470c42f07c0421f15`.
- Commit/PR/publish actions: none
- Final state: pass

<!-- orchestrate-build:end run_id=20261019-LAB05PEAK-SOFTWARE-001 -->
