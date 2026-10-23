# LAB-09/v1 report

**Lab version.** LAB-09/v1 = firmware `client/labs/lab-09-session-isolation` + server `server/labs/lab-09-session-isolation` (`lab09-server/v1`) + fixture `lab09-session/v1` (`lab09-fixture/v1`) + trace `lab09-trace/v1`.

**Hardware.** Target is ESP32-S3 / ESP-BOX-3, ESP-IDF v5.4.2, 320x240. Kit `box-a` scripted run `20261021-scripted-run-10611ef2` is workflow `PASS` and experiment `PASS`. Mute-cancel time, free-block samples, and firmware trace loss were not in that capture. Device responsiveness was not measured.

**Scenarios.** Decided inputs are `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client`. Exploratory observations are `I-confused`, `I-revoke-branches`, and `I-expire-http`. They stay out of `experiment_inputs`.

**Target.** Software qualification at L0/L1. L09-04 is observed on scripted run `20261021-scripted-run-10611ef2`. L09-05 stays blocked. Performance stays `UNMEASURED`.

## Reusable entry points

The only stage writer is `lab07::SessionWriter`. `apply_stage` stays on that owner. LAB-09 does not add a second writer or a second class that applies stages.

- `sign_in` and `sign_out` set the signed-in user and the session id from the writer.
- `begin_scoped_operation` captures the operation id, session generation, user, session id, and kind at submission and returns that ticket. The caller does not supply a different user.
- `enqueue_scoped` and `deliver_scoped_completion` accept a result that already carries that id and generation. They do not copy the current user, the current session, or the next id onto the result.
- `cancel_scoped` records one local terminal for the in-flight id.
- `note_scoped_payload` and `note_scoped_observation` attach the visible fields and the exploratory record.
- `classify_scoped_http` and `sign_in_response_ok` live on `lab09::Probe`, outside the owner. HTTP status and body decide the outcome. A completion cannot reconstruct identity from whoever is signed in now.

The same session methods run in the firmware and in the native tests.

## Ownership and threading

The UI task is the only caller of `SessionWriter`. In manual mode a worker performs HTTP into one response buffer and does not call the writer. A 50 ms LVGL timer drains that buffer with a zero wait, then updates the existing session and applied labels. A new summary or the image does not rebuild the guide or the stage. Button callbacks do not perform HTTP, join the worker, or block in cleanup. Mute and sign-out run on the UI task and return. Fixture transport and the named controls stay outside the reusable owner.

Scripted mode does not start the worker. It performs HTTP on the UI task, then calls the same `Probe::complete` with the ticket captured at `begin`.

## Cancellation

Mute of a scoped operation records one `stale` terminal, sets `applied` 0 and `image_bytes` 0, and leaves the lab display off (`display_power` 0 while the stage is muted). The late body does not restore the stage. Sign-out records one `cancelled` terminal and the stage stays `signed_out`. A user change records one `stale` terminal for A's ticket. A's generation on that terminal differs from B's current session generation, and A's bytes are not copied into B's cache. A newer `begin_scoped_operation` cancels the previous id once and keeps the new id. A rejected result releases the image buffer and does not replace the active operation. Queue capacity is 8. An identified result that does not fit records one `failure` with reason `overflow` and does not drop a queued terminal. Operation id 0 stays unidentified. Local cancellation does not undo a remote side effect. `I-revoke-branches` records fixture modes `revoke-on-mute` and `leave-server-session` and stays exploratory.

## Provisional resource bounds

These are a provisional lab probe, not a pass threshold.

| Field | Bound |
|---|---|
| `max_live_nodes` | 24 |
| `max_transient_bytes` | 28800 |
| `max_persistent_bytes` | 512 |
| `max_concurrent_operations` | 1 |
| `max_stack_or_call_depth` | 12 |
| `maximum_input_size` | 28800 |
| Scoped result queue | 8 |
| Image buffer | 28800 bytes, cleared on cleanup |
| Inbox page | 16 summaries |
| `CONFIG_LV_MEM_SIZE` | 65536, compile default only |

## Asynchronous path exercised

Submission stores the operation id and session generation before any response, including failures and timeouts. `deliver_scoped_completion` is the reducer. `test_lab09_mute_release`, `test_lab09_no_cross_apply`, `test_lab09_one_terminal`, and the native `overflow`, `newer`, `missing`, and `expire` cases exercise that path. `test_lab09_actor_observation_and_experiment_inputs` classifies representative live responses from the fixture server. The catalog's `experiment_verdict` is `PASS`. A missing server, a malformed body, and a transport failure do not authenticate and do not apply a signed-in result. The live `I-confused` observation recorded `b_side_effects` 0, matching `/observe` for user B after a profile body named user B while the credential was user A. That integer does not approve the envelope. `I-expire-http` recorded HTTP status 401 from the expire response. Workflow `PASS` with parser `UNMEASURED` does not satisfy L09-09.

## Measured

Host and native tests passed. `experiment_verdict` on the live catalog is `PASS`. `performance_verdict` is `UNMEASURED`. Evidence provenance for that catalog is `host`. The mute-cancel series and the internal-heap, PSRAM, and LVGL free-block series have sample count 0 and provenance `unmeasured/blocked`. Firmware trace loss is `UNKNOWN`.

Firmware builds:

- `make lab.lab09.build` exit 0. Artifact `build/labs/lab-09-session-isolation-manual/lab09_session_isolation.bin` (and `.elf`). `CONFIG_LAB09_SCRIPTED` is not set. Build id `lab09-fw-v1-manual`.
- `make lab.lab09.scripted.build` exit 0. Artifact `build/labs/lab-09-session-isolation-scripted/lab09_session_isolation.bin` (and `.elf`). `CONFIG_LAB09_SCRIPTED=y`. Build id `lab09-fw-v1-scripted`.

The shared owner changed, so these rebuilds also exited 0:

- `make lab.lab07.build` and `make lab.lab07.scripted.build`
- `make lab.lab08.build` and `make lab.lab08.scripted.build`

`make build` was not used. Kit `box-a` was flashed for scripted run `20261021-scripted-run-10611ef2`.

`make test` exit 0: 460 tests, 2 skipped. The skip markers were not in the LAB-09 tests. `scripts.tests.test_lab09_model` and `scripts.tests.test_lab09_host` passed inside that run, including `test_lab09_mute_release` and `test_lab09_actor_observation_and_experiment_inputs`.

## Hardware measurements

Scripted run [20261021-scripted-run-10611ef2](runs/20261021-scripted-run-10611ef2/run-manifest.json) on kit `box-a` has post-reset `LAB09_HARNESS_DONE status=complete`, workflow `PASS`, and experiment `PASS` for `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client`. That capture has no mute-to-cancel samples. Firmware trace loss on that capture is `UNKNOWN`. This report does not say the device answered quickly or that the guide layout is visually approved.

## OPEN

`COM-OPEN-001: OPEN`. `COM-OPEN-003: OPEN`. Both refer to `docs/product/communication-contracts/user-scoped-envelope.md`. This lab does not approve a token format, a revocation protocol, an error-code mapping, inbox paging, or profile field meaning.

## Triggers

No product-spec change. Signed-in and signed-out are stage tokens on the existing writer. The screen is a signed-in probe, not a product inbox.

## Decision

No product decision is approved here.

## Verification

| ID | Level | Disposition | Evidence |
|---|---|---|---|
| S-01 | L0 | pass | `make lab.lab09.build` and `make lab.lab09.scripted.build` exit 0. Not `make build`. |
| S-02 | L1 | pass | `test_s02_manifest_fields` |
| S-03 | L1 | pass | `test_lab09_actor_observation_and_experiment_inputs` writes metrics `family-link.lab-metrics/v2` |
| S-04 | L1 | pass | `scripts/lab_registry.py validate` and `lab_host.py help lab09` |
| S-05 | L1 | pass | Help text includes the registry instructions and 90-second capture |
| S-06 | L0 | pass | Distinct build directories. Manual sdkconfig has `CONFIG_LAB09_SCRIPTED` unset. Scripted sets it. |
| S-07 | L1 | pass | Adapter rejects a pre-reset harness line and a missing harness marker. Experiment stays `UNMEASURED`. |
| S-07 | L2 | observed | `20261021-scripted-run-10611ef2` workflow `PASS` with the post-reset harness marker. |
| S-08 | L1 | pass | README queue capacity 8. Native overflow case records one `failure` `overflow` and keeps the queued terminals. |
| S-09 | L1 | pass | `test_lab09_one_terminal` |
| S-10 | L1 | pass | Redaction test and `test_s10_and_l09_06_redaction` |
| S-11 | L1 | pass | Health identity and fixture byte-change without a revision bump |
| S-11 | L2 | observed | `20261021-scripted-run-10611ef2` manifest and server log. |
| S-12 | L2 | not applicable | This lab has a server on port 8769. The registry entry has no workflow key. |
| S-13 | L1 | pass | Catalog provenance is `host`, never `hardware` |
| S-14 | L1 | pass | Parser success with failed cleanup yields workflow `FAIL` |
| S-14 | L2 | observed | `20261021-scripted-run-10611ef2` workflow `PASS`. |
| S-15 | L3 | blocked | Series exist with null percentiles. `performance_verdict` is `UNMEASURED`. No budget was approved. |
| S-16 | L1 | pass | README `ResourceBudget` fields, labeled provisional lab probe |
| S-17 | L1 | pass | One `SessionWriter`. `apply_stage` stays there. LAB-07 and LAB-08 model tests still pass. |
| S-18 | L0 | pass | No change under `client/device/` or `client/shared/` |
| S-19 | L0 | pass | The v1 archive was not modified |
| S-20 | L2 | pass | This report, the evidence index row, and scripted run `20261021-scripted-run-10611ef2`. Performance stays `UNMEASURED`. |
| S-21 | L1 | pass | Unset variant with scripted mode selects `scripted`. `backend=local` returns non-zero, binds no port, and is not workflow `PASS`. |
| S-22 | L1 | pass | A failing decided row stays `FAIL` while an exploratory observation remains. Empty inputs are `UNMEASURED`. |
| L09-01 | L1 | pass | `test_lab09_no_cross_apply` |
| L09-02 | L1 | pass | `test_lab09_actor_observation_and_experiment_inputs`. `I-confused` is exploratory. `b_side_effects` is 0. The id is absent from `experiment_inputs`. |
| L09-03 | L1 | pass | `test_lab09_one_terminal` |
| L09-04 | L2 | observed | `20261021-scripted-run-10611ef2`: post-reset `LAB09_HARNESS_DONE status=complete`, workflow `PASS`. |
| L09-05 | L3 | blocked | No mute-to-cancel samples. `performance_verdict` `UNMEASURED`. |
| L09-06 | L1 | pass | `test_s10_and_l09_06_redaction` on committed fixtures and `docs/labs/evidence/lab-09/samples/server.log.redacted` |
| L09-07 | L1 | pass | This report. `COM-OPEN-001: OPEN`. |
| L09-08 | L1 | pass | `test_lab09_mute_release`. Four kinds. Each terminal is `stale` or `cancelled`, `terminal_count` 1, `applied` 0, `image_bytes` 0. |
| L09-09 | L1 | pass | `test_lab09_actor_observation_and_experiment_inputs`. `experiment_inputs` is the five decided ids. Live catalog `experiment_verdict` is `PASS`. Exploratory ids stay out and still have `observation`. A workflow `PASS` with parser `UNMEASURED` is a different verdict. |
