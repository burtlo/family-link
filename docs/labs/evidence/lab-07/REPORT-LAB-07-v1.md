# LAB-07/v1 software qualification

LAB-07/v1 = firmware lab07-fw-v1-manual / lab07-fw-v1-scripted + server lab07-server/v1 + fixture lab07-fixture/v1

| Field | Value |
| --- | --- |
| Target | esp32s3, ESP-IDF v5.4.2, ESP-BOX-3. Distinct build directories `build/labs/lab-07-readiness-roster-manual` and `build/labs/lab-07-readiness-roster-scripted`. |
| Measured | Host and native checks, both firmware variants, and the retained kit captures on `box-a`: scripted `20261020-scripted-run-5c448b64` and manual `20261020-manual-run-8658e29a`. Failed and incomplete captures are not retained. |
| Trigger | Software qualification of the readiness, synthetic roster, mute, and health lab. Device package was not selected. |
| Decision | Roster cadence, one retry, mute stale, and the eight-user focus rule are implemented as decided behavior on the virtual clock. Screen copy stays exploratory. |
| L0 | Both variants build. Manual sdkconfig has `# CONFIG_LAB07_SCRIPTED is not set`. Scripted sdkconfig has `CONFIG_LAB07_SCRIPTED=y`. Both have `CONFIG_LV_MEM_SIZE=65536`. |
| L1 | Native model and host parser, server, and adapter tests pass. Timings are exact virtual ticks. |
| L2 | Scripted run `20261020-scripted-run-5c448b64` and manual run `20261020-manual-run-8658e29a` on kit `box-a`: `workflow_status` PASS, `experiment_verdict` PASS, `performance_verdict` UNMEASURED. Both are post-reset captures. Manual `R-timeout-ui` is `timed_out` with `touch_during_wait=1`. |
| L3 | Performance verdict `UNMEASURED`. Virtual-clock ticks are not frame time. |

COM-OPEN-001: OPEN

COM-OPEN-004: OPEN

COM-OPEN-002, COM-OPEN-003, and COM-OPEN-005 remain OPEN.

`R-wire.class` is exploratory. `R-stale-clock.class` is exploratory. Neither id is in `experiment_inputs`.

`stage_owner=lab07-local`. One session writer in `components/lab07_session` applies stage transitions. Health results do not write the user stage. No LAB-06 tree was created.

`CONFIG_LV_MEM_SIZE=65536` is the compile default, not a threshold. The README ResourceBudget is a provisional lab probe. This report does not declare a performance PASS. Manual run `20261020-manual-run-8658e29a` is an experiment PASS. Its manifest leaves firmware binary and source hashes empty, and firmware trace loss stays UNKNOWN.

The 10-minute idle and 5-minute roster cadence are L1 virtual-clock only. Host tests and the scripted harness do not sleep 10 minutes. The scripted firmware harness uses a real 1-second HTTP request timeout. The manual Idle clock and Stale beats controls advance that same virtual clock and print one terminal each. They are lab controls, not a real 10-minute wait and not product copy approval. The manual hold still uses a real 1-second device timeout.

## Manual screen layout

The manual screen is 320x240 and does not scroll as a whole. Montserrat 14 has line height 16.

| Band | Position | What the operator sees |
| --- | --- | --- |
| Status | y=0, height 48, width 316 | Three lines: stage and user count; focus id; stale, in-flight count, and touch. These lines sit above the stage on connecting, Wi-Fi unavailable, roster, and empty hangout. |
| Stage | y=48, height 74 | Connecting, Wi-Fi unavailable, roster, and empty hangout. Roster keeps left-justified `Sign in as ...` and the horizontal cards. |
| Controls | y=122, height 118 | This band has `Tap each once` at the top and one step button, 312 by 86, under that label. The button names the current step, such as `1/16  Wi-Fi down`, and advances after a tap. A second tap within 700 ms does not count. During Hold 1 second, finger-down on that button starts the wait and counts while the finger remains down. The status line shows `touch=1`. |

Muted hides the status, stage, and controls. The screen is black except a large `Unmute (lab stand-in)` button.

Manual run `20261020-manual-run-8658e29a` on kit `box-a` is workflow PASS and experiment PASS. All ten decided samples are present once, and `R-timeout-ui` has `touch_during_wait=1`. Scripted run `20261020-scripted-run-5c448b64` is also experiment PASS. Performance stays UNMEASURED. Earlier failed and incomplete captures are not retained.

Manual controls print each new session terminal once as a `LAB07_TRACE` line when the control is handled. The hold prints its terminal when the real 1-second device timeout completes. Idle clock advances the lab virtual clock and expects stage roster, roster period 300000 ms, heartbeat period 60000 ms, and stale 0. Stale beats advances the lab virtual clock through three missed heartbeats and expects stage roster unchanged, roster period 60000 ms, heartbeat period 60000 ms, and stale 1. A manual capture that skips a control stays experiment INCOMPLETE.

## Acceptance ledger

| Acceptance ID | Owner package | Required level | Implemented path | Actual check/artifact | Achieved level | Disposition/blocker |
| --- | --- | --- | --- | --- | --- | --- |
| S-01 | L07-FW | L0 | `client/labs/lab-07-readiness-roster/` | `make lab.lab07.build` exit 0. Not `make build`. | L0 | pass |
| S-02 | L07-HOST | L1 | `client/labs/lab-07-readiness-roster/manifest.lab.json` | `test_s02_manifest_fields` | L1 | pass |
| S-03 | L07-HOST | L1 | `tools/parse_lab_trace.py`, registry `trace_parser` | `test_s03_parser_document_and_run_id` | L1 | pass |
| S-04 | L07-HOST | L1 | `config/labs.registry.yaml` `lab07` | `scripts/lab_registry.py validate` exit 0. `make lab.help` lists `lab.lab07` and `lab.lab07.scripted`. | L1 | pass |
| S-05 | L07-HOST | L1 | registry `run.instructions`, capture 90 s | `make lab.lab07.help` exit 0 | L1 | pass |
| S-06 | L07-FW | L0 | `sdkconfig.defaults`, `sdkconfig.defaults.scripted` | `make lab.lab07.build` and `make lab.lab07.scripted.build` exit 0. Distinct directories. Scripted symbol only in the scripted sdkconfig. `CONFIG_LV_MEM_SIZE=65536` in both. | L0 | pass |
| S-07 | L07-FW | L1 | `main/app_main.cpp` fail-closed statuses; adapter `classify_capture` | `test_s07_pre_reset_and_missing_marker`. Pre-reset `LAB07_HARNESS_DONE` does not pass. Missing marker yields workflow FAIL and experiment UNMEASURED. | L1 | pass |
| S-07 | L07-DEVICE | L2 | same firmware path | No post-reset serial from a kit. | none | blocked. Device flash was not authorized. |
| S-08 | L07-MODEL | L1 | roster result queue capacity 4 | `test_lab07_queue_focus_and_single_terminal`. README names the capacity as a provisional lab probe. | L1 | pass |
| S-09 | L07-MODEL | L1 | `SessionWriter` terminal records | native `terminal` case inside `test_lab07_queue_focus_and_single_terminal` | L1 | pass |
| S-10 | L07-HOST | L1 | `scripts/server_log.py` `redact_line`; parser PIN rejection | `test_s10_redact_line_and_pin_rejection`. No evidence run directory was written. | L1 | pass |
| S-11 | L07-HOST | L1 | `server/labs/lab-07-readiness-roster/` | `test_s11_health_identity_and_revision_bump`. Health echoes `run_id`, fixture version, fixture SHA-256, and server version. A byte change without a revision bump raises `IdentityError`. | L1 | pass |
| S-11 | L07-DEVICE | L2 | run manifest `server.source_sha256` and `fixture.sha256` | Adapter records those hashes when `execute` runs. No device run manifest was written. | none | blocked. No kit capture. |
| S-12 | L07-HOST | L2 | this lab has a server on port 8767 | Registry entry omits `workflow`. S-12 applies to labs without a server. | n/a | not applicable |
| S-13 | L07-HOST | L1 | parser provenance | `test_s13_host_provenance_is_not_hardware`. Host parse uses `host` or `unmeasured/blocked`. | L1 | pass |
| S-13 | L07-DEVICE | L3 | heap, touch, and scanout series | No hardware capture. Performance verdict stays `UNMEASURED`. | none | blocked |
| S-14 | L07-HOST | L2 | `classify_capture` | `test_s14_parser_success_with_failed_cleanup` forces workflow FAIL. | L2 | pass |
| S-15 | L07-HOST | L3 | parser nearest-rank fields | `test_percentiles_and_empty_series`. Empty series is null, `sample_count` 0, with `missing_reason`. `performance_verdict` is `UNMEASURED`. | recorded, not promoted | pass for the non-promotion rule. No performance PASS. |
| S-15 | L07-DEVICE | L3 | LVGL and pixel ledgers on device | Firmware can emit `resource_sample`. No device samples. | none | blocked. UNMEASURED |
| S-16 | L07-QUAL | L1 | `client/labs/lab-07-readiness-roster/README.md` | `test_s16_and_stage_owner_readme`. ResourceBudget labeled provisional lab probe. | L1 | pass |
| S-17 | L07-MODEL | L1 | one `apply_stage` writer; pollers enqueue only | `test_lab07_stale_flag` and `test_lab07_stage_enum` source scan | L1 | pass |
| S-18 | L07-QUAL | L0 | change set | `git status` shows no path under `client/device/` or `artifacts/device/bootstrap/`. | L0 | pass |
| S-19 | L07-QUAL | L0 | change set | `git status` shows no v1 POC archive path. | L0 | pass |
| S-20 | L07-QUAL | L2 | this report and the evidence index | `docs/labs/evidence/README.md` row: software-qualified, hardware pending. | L2 | pass for the software report. Hardware pending. |
| S-21 | L07-HOST | L1 | `scripts/lab_adapter_lab07.py` | `test_s21_scripted_variant_and_local_backend`. Unset variant with mode scripted selects scripted. `backend=local` returns non-zero, binds nothing, and workflow is not PASS. | L1 | pass |
| S-22 | L07-HOST | L1 | parser verdict | `test_s22_exploratory_rows_do_not_decide_the_verdict` | L1 | pass |
| L07-01 | L07-MODEL | L1 | virtual clock in `session.cpp` | `test_lab07_cadence`. After 600000 ms quiet, roster period is 300000 ms and heartbeat stays 60000 ms. | L1 | pass. Not promoted to L3. |
| L07-02 | L07-MODEL | L1 | request timeout 1000 ms | `test_lab07_timeout`. No terminal at 999 ms. `timed_out` at 1000 ms. | L1 | pass |
| L07-02 | L07-DEVICE | L3 | scripted `esp_http_client` timeout 1000 ms | `20261020-scripted-run-5c448b64` `elapsed_ms` one sample, nearest-rank p50/p95/p99/max 990 ms, provenance `hardware`. | L3 sample recorded | recorded. Do not tighten the 1000 ms product rule from one sample. |
| L07-03 | L07-MODEL | L1 | one immediate retry | `test_lab07_one_retry`. Attempt count 2, stage stays roster. | L1 | pass |
| L07-04 | L07-MODEL | L1 | fixture flag `valid=0` | `test_lab07_invalid_not_ready`. `selectable_cards` 0. Stage stays connecting. JSON field names are not compared to a product schema. | L1 | pass |
| L07-05 | L07-MODEL | L1 | mute during an in-flight roster response | `test_lab07_mute_stale`. Late success stays `stale` and stage stays `muted`. | L1 | pass |
| L07-06 | L07-DEVICE | L2 | `make lab.lab07.scripted KIT=box-a` | `docs/labs/evidence/lab-07/runs/20261020-scripted-run-5c448b64/run-manifest.json`. Health identity matched. `workflow_status` PASS only with the post-reset harness marker and successful cleanup. | L2 | pass for this run. `experiment_verdict` PASS is separate and does not approve wire fields or performance. |
| L07-07 | L07-DEVICE | L3 | touch-to-trace gap | No device samples. `performance_verdict` `UNMEASURED`. | none | blocked |
| L07-08 | L07-DEVICE | L3 | eight-user widget count and LVGL largest free block | `20261020-scripted-run-5c448b64` records one LVGL largest-free sample, 49816 bytes, provenance hardware. Widget count is not in that sample. `performance_verdict` stays UNMEASURED. | L3 sample recorded | recorded. One sample is not a performance pass. |
| L07-09 | L07-QUAL | L1 | fixture schema `lab07-health-roster/v1` | This report. COM-OPEN-001: OPEN. COM-OPEN-004: OPEN. `R-wire.class` is exploratory. | L1 | pass |
| L07-10 | L07-MODEL | L1 | `components/lab07_session/include/stages.hpp` | `test_lab07_stage_enum` | L1 | pass |
| L07-11 | L07-MODEL | L1 | mute release snapshot | `test_lab07_mute_release` | L1 | pass |
| L07-12 | L07-HOST | L1 | `experiment_inputs` in `tools/parse_lab_trace.py` | `test_lab07_experiment_inputs`. Workflow PASS is not treated as experiment PASS. | L1 | pass |
