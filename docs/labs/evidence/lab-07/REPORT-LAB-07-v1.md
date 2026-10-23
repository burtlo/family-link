# LAB-07/v1 software qualification

LAB-07/v1 = firmware lab07-fw-v1-manual / lab07-fw-v1-scripted + server lab07-server/v1 + fixture lab07-fixture/v1

Correction run `login-session-20261020`, package `L07-CORRECTION`. This report qualifies the software path. It does not record device responsiveness.

| Field | Value |
| --- | --- |
| Target | esp32s3, ESP-IDF v5.4.2, ESP-BOX-3. Distinct build directories `build/labs/lab-07-readiness-roster-manual` and `build/labs/lab-07-readiness-roster-scripted`. |
| Measured | Host and native checks, and both firmware variant builds. No kit was flashed for this correction. |
| Trigger | Correction of request identity on drain, and of readiness decisions that must come from representative fixture responses. |
| Decision | Roster cadence, one retry, mute stale, and the eight-user focus rule stay the decided virtual-clock behavior. Screen copy stays exploratory. |
| L0 | Both variants build. Manual sdkconfig has `# CONFIG_LAB07_SCRIPTED is not set` and `CONFIG_LAB07_BUILD_ID="lab07-fw-v1-manual"`. Scripted sdkconfig has `CONFIG_LAB07_SCRIPTED=y` and `CONFIG_LAB07_BUILD_ID="lab07-fw-v1-scripted"`. Both have `CONFIG_LV_MEM_SIZE=65536`. |
| L1 | Native model and host parser, server, and adapter tests pass, including the live fixture classification. Timings of 60 seconds, 10 minutes, and 5 minutes are virtual-clock ticks. |
| L2 | Device confirmation of the corrected path is pending. Historical runs `20261020-scripted-run-5c448b64` and `20261020-manual-run-8658e29a` were not rewritten and do not confirm this path. |
| L3 | Performance verdict `UNMEASURED`. Virtual-clock ticks are not frame time. No new device series was captured. |

## Reusable owner surface

`components/lab07_session/include/session.hpp` is the owner surface a later lab can call:

- `SessionWriter::begin_roster_request` starts one roster request and returns the id captured at submission. It returns 0 when no new request starts.
- `SessionWriter::deliver_roster_completion` delivers a completion that already carries that submission id. Enqueue and drain do not fill a missing id from the current in-flight request or from `next_operation_id_`.
- `SessionWriter::mute` cancels in-flight roster work and blanks the stage.
- `stage` and `stage_name` read the visible stage.
- `terminal_for` and `terminal_count_for` read the single terminal for an operation.

Fixture URLs, HTTP, the scripted catalog, and on-screen lab controls stay outside that header. `roster_http.hpp` holds `classify_roster_http` and `deliver_classified_roster`. `catalog.hpp` holds `run_scripted_catalog`, `apply_manual_control`, and `complete_manual_hold`. There is one session writer. No file was added under `client/shared` or `client/device`. LAB-08 and LAB-09 were not implemented.

## Ownership and threading

`stage_owner=lab07-local`. `SessionWriter` is the only stage writer. `RosterPoller` enqueues roster results. `HealthPoller` only acknowledges health. Health does not write the user stage.

The scripted HTTP exchange runs on the main task inside `scripted_harness`, before the LVGL loop. `perform_http` and `deliver_live_roster` are compiled only when `CONFIG_LAB07_SCRIPTED` is set. Button callbacks `on_step`, `on_step_pressed`, and `on_unmute`, and the connecting timer `animate_connecting`, do not perform HTTP and do not join a worker. `render` publishes from session state. A stage change redraws the stage subtree. An unchanged stage updates status text, and on roster it rebuilds cards inside the stage panel only. The guide is not rebuilt for a data change.

Status stays at y=0 with height 48. The stage starts at y=48. The cue `Tap each once` stays inside the scrollable guide.

## Cancellation

Identity is stored on the result when it is queued. Enqueue does not copy the in-flight id or the next id onto operation id 0. Drain does not mint an id. A result with operation id 0 at drain is skipped and does not complete the active request. An identified result whose id is not the active request gets one `stale` terminal and is not applied.

The native `identity` case queues a roster result with its submission id, mutes, unmutes, and drains. The old result does not complete the new request, does not replace the active operation, and keeps a single stale terminal. The new request has no terminal until its own outcome. An unidentified result enqueued after unmute is not given the new request id and does not complete that request.

A full queue does not drop an identified terminal: that result records one `failure` for its own id and is not applied. An unidentified overflow is counted in `rejected_results_` and is not given a new id. `record_terminal` refuses a second terminal for the same id and refuses operation id 0.

Mute is a local cancellation. It does not mean the server rolled back a side effect.

## Resource bounds

README ResourceBudget remains a provisional lab probe: 32 live nodes, 16384 transient bytes, 4096 persistent bytes, 2 concurrent operations, stack or call depth 12, and maximum input size 2048. The roster result queue capacity is 4. The HTTP body cap is 2048 bytes. The client timeout is 1000 ms. `CONFIG_LV_MEM_SIZE=65536` is the compile default, not a threshold. This report does not declare a performance PASS.

## Asynchronous path evidence

User intent and scripted probes enter through `SessionWriter::boot` / `begin_roster`, which capture `roster_operation_id_` before any read. `deliver_live_roster` keeps that id, calls `perform_http`, then `classify_roster_http` and `deliver_classified_roster`. Delivery goes through `deliver_roster_completion`, `enqueue_roster`, and `drain` into `apply_roster_body` or a non-success terminal. `render` then publishes the session stage.

`run_scripted_catalog` still advances the 60-second, 10-minute, and 5-minute schedules on the virtual clock. The harness prints those rows and then `event=virtual_clock catalog=complete`. Those rows are model terminals. They are not live HTTP readiness. After that, the harness reads four live outcomes into the same writer: unknown script `missing-script` (unavailable), `invalid-200`, `malformed`, and `http://127.0.0.1:1/roster` (connection failure). Each must be `failure`, one terminal, stage `connecting`, `selectable_cards` 0, and `held_response=0`. A connection failure is not `event=device_timeout` and is not scenario `R-timeout-ui`. `LAB07_HARNESS_DONE status=complete` is printed only when every representative probe stays non-ready. Otherwise the harness prints `representative_not_ready` or `url_rejected`.

Host evidence starts `server/labs/lab-07-readiness-roster/server.py` and records `LAB07_HOST_HTTP port=8767 bind=configured-8767`. The live bytes for the unavailable, invalid, and malformed fixtures, plus a connection to a closed port, are passed to the native `http-apply` mode, which calls `deliver_classified_roster`. The test fails if that result is success, a timeout, or selectable readiness. A `device_http` line with `elapsed_ms=1000` does not become an elapsed sample and does not change provenance to hardware.

Native `identity` covers a matching apply-once, an unidentified result that does not succeed while a request is in flight, an identified stale id, an unidentified result that cannot complete a newer request, a result queued with its submission id before mute that stays stale after unmute, mute then unmute then enqueue of operation id 0 then drain, a hold timeout on the id captured at `begin_hold_probe`, the four classified failures, and one failure terminal for an identified queue overflow.

## Hardware measurements still pending

These are not software results and were not measured on this correction:

- Device serial confirmation of `deliver_live_roster` and the four representative HTTP outcomes. Pending. No kit was flashed.
- L07-06 post-reset scripted workflow on a kit for this firmware. Blocked.
- L07-07 touch-to-trace gap. Blocked. `performance_verdict` stays `UNMEASURED`.
- L07-08 eight-user widget count and LVGL largest free block on this build. Blocked. `performance_verdict` stays `UNMEASURED`.
- Firmware trace loss. UNKNOWN.
- Historical captures `20261020-scripted-run-5c448b64` and `20261020-manual-run-8658e29a` remain on disk. They were produced before this identity and fixture-outcome correction. They are not observations of this path.

## Product decisions still open

COM-OPEN-001, COM-OPEN-002, COM-OPEN-003, COM-OPEN-004, and COM-OPEN-005 stay OPEN. The fixture `valid` flag is not a product schema. `R-wire.class` and `R-stale-clock.class` stay exploratory and are not experiment inputs. No PIN login and no `wrong_pin` field were added. Screen copy for connecting, Wi-Fi unavailable, and empty hangout stays exploratory.

## Operator text

The lab README operator section states:

Manual mode shows one step at a time. The cue says "Tap each once". Tap the large button once and wait until its name advances. Mute turns the display off, cancels roster work that is still in flight, and leaves the Unmute stand-in. Unmute starts a new readiness check. A result that was already queued keeps the request it belonged to. After mute, then unmute, that old result does not finish the new check.

Scripted mode is L1 virtual-clock only. It keeps the 60-second roster refresh, the 10-minute idle threshold, and the 5-minute idle period on a virtual clock. It does not wait those out on the wall clock. The scripted harness reads representative fixture responses and feeds each completion, with the request id captured at submission, into the same session writer. An unavailable fixture, a malformed or invalid response, and a connection failure are not readiness. A connection failure that takes about a second is not a completed held-response test.

Two correction rules:

1. The request id is captured when the work is submitted and stored on the result when that result is queued. Drain does not assign the current or next request id. A result that has no id is not given the in-flight id or the next id, including after unmute, and is not applied.
2. Unavailable, invalid, malformed, and connection-failure responses do not select cards and do not complete the held-response scenario.

Status text ends at y=48. The stage starts at y=48. The cue sentence stays inside the scrollable guide. Mute does not mean the server rolled back a side effect. Health updates do not change the user stage.

## Manual screen layout

The manual screen is 320x240 and does not scroll as a whole. Montserrat 14 has line height 16.

| Band | Position | What the operator sees |
| --- | --- | --- |
| Status | y=0, height 48, width 316 | Three lines: stage and user count; focus id; stale, in-flight count, and touch. These lines sit above the stage on connecting, Wi-Fi unavailable, roster, and empty hangout. |
| Stage | y=48, height 74 | Connecting, Wi-Fi unavailable, roster, and empty hangout. Roster keeps left-justified `Sign in as ...` and the horizontal cards. |
| Controls | y=122, height 118 | This band has `Tap each once` inside a scrollable guide and one step button, 312 by 86, under that guide. The button names the current step, such as `1/16  Wi-Fi down`, and advances after a tap. A second tap within 700 ms does not count. During Hold 1 second, finger-down on that button starts the wait and counts while the finger remains down. The status line shows `touch=1`. |

Muted hides the status, stage, and controls. The screen is black except a large `Unmute (lab stand-in)` button.

Manual controls print each new session terminal once as a `LAB07_TRACE` line when the control is handled. The hold prints its terminal when the on-device 1-second hold timer completes. Idle clock advances the lab virtual clock and expects stage roster, roster period 300000 ms, heartbeat period 60000 ms, and stale 0. Stale beats advances the lab virtual clock through three missed heartbeats and expects stage roster unchanged, roster period 60000 ms, heartbeat period 60000 ms, and stale 1. A manual capture that skips a control stays experiment INCOMPLETE.

## Acceptance ledger

| Acceptance ID | Owner package | Required level | Implemented path | Actual check/artifact | Achieved level | Disposition/blocker |
| --- | --- | --- | --- | --- | --- | --- |
| S-01 | L07-FW | L0 | `client/labs/lab-07-readiness-roster/` | `make lab.lab07.build` exit 0. Not `make build`. Artifact `build/labs/lab-07-readiness-roster-manual/lab07_readiness_roster.bin`. | L0 | pass |
| S-02 | L07-HOST | L1 | `client/labs/lab-07-readiness-roster/manifest.lab.json` | `test_s02_manifest_fields` inside `make test` exit 0. | L1 | pass |
| S-03 | L07-HOST | L1 | `tools/parse_lab_trace.py`, registry `trace_parser` | `test_s03_parser_document_and_run_id` | L1 | pass |
| S-04 | L07-HOST | L1 | `config/labs.registry.yaml` `lab07` | Covered by `make test` exit 0. Registry was not rewritten. | L1 | pass |
| S-05 | L07-HOST | L1 | registry `run.instructions` | Covered by `make test` exit 0. | L1 | pass |
| S-06 | L07-FW | L0 | `sdkconfig.defaults`, `sdkconfig.defaults.scripted` | `make lab.lab07.build` and `make lab.lab07.scripted.build` exit 0. Manual sdkconfig: `# CONFIG_LAB07_SCRIPTED is not set`, `CONFIG_LAB07_BUILD_ID="lab07-fw-v1-manual"`, `CONFIG_LV_MEM_SIZE=65536`. Scripted sdkconfig: `CONFIG_LAB07_SCRIPTED=y`, `CONFIG_LAB07_BUILD_ID="lab07-fw-v1-scripted"`, `CONFIG_LV_MEM_SIZE=65536`. | L0 | pass |
| S-07 | L07-HOST | L1 | `main/app_main.cpp` fail-closed statuses; adapter `classify_capture` | `test_s07_pre_reset_and_missing_marker`. Pre-reset `LAB07_HARNESS_DONE` does not pass. Missing marker yields workflow FAIL and experiment UNMEASURED. `status=complete` is the post-catalog marker only when representative probes stay non-ready. | L1 | pass |
| S-07 | L07-DEVICE | L2 | same firmware path | No post-reset serial from a kit for this firmware. | none | blocked. Device confirmation pending. |
| S-08 | L07-MODEL | L1 | roster result queue capacity 4 | `test_lab07_queue_focus_and_single_terminal` and native `identity` overflow. README names the capacity as a provisional lab probe. | L1 | pass |
| S-09 | L07-MODEL | L1 | `SessionWriter` terminal records | native `identity` and `test_lab07_queue_focus_and_single_terminal`. One terminal per accepted operation. | L1 | pass |
| S-10 | L07-HOST | L1 | `scripts/server_log.py` `redact_line`; parser PIN rejection | `test_s10_redact_line_and_pin_rejection`. No evidence run directory was written for this correction. | L1 | pass |
| S-11 | L07-HOST | L1 | `server/labs/lab-07-readiness-roster/` | `test_s11_health_identity_and_revision_bump`. Health echoes `run_id`, fixture version, fixture SHA-256, and server version. | L1 | pass |
| S-11 | L07-DEVICE | L2 | run manifest `server.source_sha256` and `fixture.sha256` | No device run manifest was written for this correction. | none | blocked. No kit capture. |
| S-12 | L07-HOST | L2 | this lab has a server on port 8767 | Registry entry omits `workflow`. S-12 applies to labs without a server. | n/a | not applicable |
| S-13 | L07-HOST | L1 | parser provenance | `test_s13_host_provenance_is_not_hardware` and `test_live_http_outcomes_use_session_classifier`. A `device_http` elapsed value is not hardware provenance. | L1 | pass |
| S-13 | L07-DEVICE | L3 | heap, touch, and scanout series | No hardware capture for this correction. Performance verdict stays `UNMEASURED`. | none | blocked |
| S-14 | L07-HOST | L2 | `classify_capture` | `test_s14_parser_success_with_failed_cleanup` forces workflow FAIL. | L2 | pass |
| S-15 | L07-HOST | L3 | parser nearest-rank fields | `test_percentiles_and_empty_series`. Empty series is null, `sample_count` 0, with `missing_reason`. `performance_verdict` is `UNMEASURED`. | recorded, not promoted | pass for the non-promotion rule. No performance PASS. |
| S-15 | L07-DEVICE | L3 | LVGL and pixel ledgers on device | Firmware can emit `resource_sample` from the scripted harness. No device samples for this build. | none | blocked. UNMEASURED |
| S-16 | L07-QUAL | L1 | `client/labs/lab-07-readiness-roster/README.md` | `test_s16_and_stage_owner_readme`. ResourceBudget labeled provisional lab probe. Operator section states the two correction rules. | L1 | pass |
| S-17 | L07-MODEL | L1 | one `apply_stage` writer; pollers enqueue only | `test_lab07_stale_flag` and `test_lab07_stage_enum` source scan. Health does not write the user stage. | L1 | pass |
| S-18 | L07-QUAL | L0 | change set | No path under `client/device/` or `artifacts/device/bootstrap/` was edited. | L0 | pass |
| S-19 | L07-QUAL | L0 | change set | No v1 POC archive path was edited. | L0 | pass |
| S-20 | L07-QUAL | L2 | this report and the evidence index | `docs/labs/evidence/README.md` LAB-07 row: software-qualified at L0/L1, device confirmation pending. | L2 | pass for the software report. Hardware pending. |
| S-21 | L07-HOST | L1 | `scripts/lab_adapter_lab07.py` | `test_s21_scripted_variant_and_local_backend`. Unset variant with mode scripted selects scripted. `backend=local` returns non-zero, binds nothing, and workflow is not PASS. | L1 | pass |
| S-22 | L07-HOST | L1 | parser verdict | `test_s22_exploratory_rows_do_not_decide_the_verdict` | L1 | pass |
| L07-01 | L07-MODEL | L1 | virtual clock in `session.cpp` | `test_lab07_cadence`. After 600000 ms quiet, roster period is 300000 ms and heartbeat stays 60000 ms. Catalog does not sleep those intervals. | L1 | pass. Not promoted to L3. |
| L07-02 | L07-MODEL | L1 | request timeout 1000 ms | `test_lab07_timeout`. No terminal at 999 ms. `timed_out` at 1000 ms. Native `identity` uses the id captured at `begin_hold_probe`. | L1 | pass |
| L07-02 | L07-DEVICE | L3 | scripted client timeout 1000 ms | No device elapsed series for this firmware. Host classification rejects a padded connection failure as readiness and does not store it as `device_timeout`. | none | blocked. Device confirmation pending. Do not treat host or virtual-clock time as device responsiveness. |
| L07-03 | L07-MODEL | L1 | one immediate retry | `test_lab07_one_retry`. Attempt count 2, stage stays roster. | L1 | pass |
| L07-04 | L07-MODEL | L1 | fixture flag `valid=0` | `test_lab07_invalid_not_ready` and live `invalid-200` through `http-apply`. `selectable_cards` 0. Stage stays connecting. JSON field names are not compared to a product schema. | L1 | pass |
| L07-05 | L07-MODEL | L1 | mute, unmute, and a newer request | `test_lab07_mute_stale` plus native `identity`. A result queued with its submission id before mute stays stale after unmute and does not complete the new request. An unidentified result enqueued after unmute does not receive that new id and does not complete it. | L1 | pass |
| L07-06 | L07-DEVICE | L2 | `make lab.lab07.scripted KIT=box-a` | Not run. No new run manifest. Historical `20261020-scripted-run-5c448b64` does not confirm `deliver_live_roster`. | none | blocked. Device confirmation pending. |
| L07-07 | L07-DEVICE | L3 | touch-to-trace gap | No device samples. `performance_verdict` `UNMEASURED`. | none | blocked |
| L07-08 | L07-DEVICE | L3 | eight-user widget count and LVGL largest free block | No device sample for this build. `performance_verdict` stays UNMEASURED. | none | blocked. UNMEASURED |
| L07-09 | L07-QUAL | L1 | fixture schema `lab07-health-roster/v1` | This report. COM-OPEN-001 through COM-OPEN-005 stay OPEN. `R-wire.class` is exploratory. | L1 | pass |
| L07-10 | L07-MODEL | L1 | `components/lab07_session/include/stages.hpp` | `test_lab07_stage_enum` | L1 | pass |
| L07-11 | L07-MODEL | L1 | mute release snapshot | `test_lab07_mute_release` | L1 | pass |
| L07-12 | L07-HOST | L1 | `experiment_inputs` in `tools/parse_lab_trace.py` | `test_lab07_experiment_inputs`. Workflow PASS is not treated as experiment PASS. Virtual-clock catalog lines are not HTTP readiness. | L1 | pass |

## Checks

| Command | Result |
| --- | --- |
| `.venv\Scripts\python.exe -m unittest scripts.tests.test_lab07_model scripts.tests.test_lab07_host -v` | exit 0. 27 tests. `LAB07_HOST_HTTP port=8767 bind=configured-8767`. |
| `make lab.lab07.build` | exit 0. `build/labs/lab-07-readiness-roster-manual`. |
| `make lab.lab07.scripted.build` | exit 0. `build/labs/lab-07-readiness-roster-scripted`. |
| `make test` | exit 0. 423 tests, 2 skipped, 120.995 s. Includes the LAB-07 host line `LAB07_HOST_HTTP port=8767 bind=configured-8767`. |

No commit was created. No kit was flashed. Independent review was not performed.
