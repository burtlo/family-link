# LAB-08/v1 report

**Lab version.** LAB-08/v1 = firmware `client/labs/lab-08-pin-cooldown` + server `server/labs/lab-08-pin-cooldown` (`lab08-server/v1`) + fixture `lab08-pin/v1` (`lab08-fixture/v1`) + trace `lab08-trace/v1`.

**Hardware.** Target is ESP32-S3 / ESP-BOX-3, ESP-IDF v5.4.2, 320x240. No kit was flashed. No post-reset serial capture exists. Device responsiveness was not measured.

**Scenarios.** Decided inputs are `K-ok`, `K-wrong`, `K-five`, `K-switch-user`, `K-timeouts`, `K-lost`, `K-mute-local`, `K-boot`, and `K-idle`. Exploratory observations are `K-concurrent`, `K-mismatch`, and `K-mute-server`. They stay out of `experiment_inputs`.

**Target.** Software qualification at L0/L1. L08-07 and L08-08 stay blocked. Performance stays `UNMEASURED`.

## Reusable entry points

The only stage writer is `lab07::SessionWriter` in the LAB-07 session component. LAB-08 does not add a second owner.

- `begin_pin_request` captures the operation id, session id, user, and key generation, and returns that id.
- `copy_pin_request` copies the frozen request for a worker. Callers must not log the PIN.
- `enqueue_pin` and `deliver_pin_completion` accept a result that already carries the submission id. They do not copy the current in-flight id onto operation id 0.
- `cancel_pin` records one local terminal. It does not roll back a remote side effect.
- `classify_pin_http` and `deliver_classified_pin` live outside the owner. The classifier uses the id the caller captured. HTTP status and body decide the outcome.

The same session methods run in the firmware and in the native tests.

## Ownership and threading

The UI task is the only caller of `SessionWriter`. A manual worker may perform HTTP and enqueue a `PinResult`. It does not call the session writer. The UI timer drains with a zero wait, then updates existing labels. A digit or a cooldown second does not rebuild the guide or the stage. Button callbacks do not perform HTTP, join the worker, or block in cleanup. Fixture transport and the named controls stay outside the reusable owner.

## Cancellation

Mute wipes the PIN buffer, turns the lab display off (`display_power` 0, `digit_len` 0), and records one `cancelled` terminal. A later success for that id does not unmute, authenticate, or reveal digits. A user change and a newer `begin_pin_request` each cancel the previous id once. A late result for the old id does not apply and does not clear the newer digits. A rejected or overflow result releases the PIN buffer it held, keeps one terminal, and does not start a second login. Queue capacity is 2. The third identified result is one `failure` with reason `overflow`.

## Provisional resource bounds

These are a provisional lab probe, not a pass threshold.

| Field | Bound |
|---|---|
| `max_live_nodes` | 24 |
| `max_transient_bytes` | 4096 |
| `max_persistent_bytes` | 512 |
| `max_concurrent_operations` | 2 |
| `max_stack_or_call_depth` | 12 |
| `maximum_input_size` | 4 PIN digits |
| PIN result queue | 2 |
| Roster result queue | 4, unchanged |
| HTTP body cap | 1024 bytes |
| `CONFIG_LV_MEM_SIZE` | 65536, compile default only |

## Asynchronous path exercised

Submission stores identity before any response. The virtual clock and `deliver_pin_completion` complete that same id, including timeouts and transport failures. `test_lab08_transport_schedule`, `test_lab08_one_terminal`, `test_lab08_mute_release`, `test_identity_queue_and_level0` (`identity`, `newer`, `overflow`, `lost`), and `test_lab08_idempotent_and_live_body` exercise that path. The live test writes the fixture's actual status and body into `http-apply`. A missing server, a malformed body, and a wrong outcome do not authenticate. The 1/2/4-second delays, the 2-second wrong-pin copy, the 60-second cooldown, and the 60-second partial-entry return advance on the virtual clock. Tests do not sleep them.

`K-mute-server` records the server side-effect count and stays exploratory. Local mute does not undo that count.

## Measured

Host and native tests passed for the decided model, the parser, and the fixture on port 8768 (or an ephemeral port when the test only needed a free socket). `experiment_verdict` on the model catalog is `PASS`. `performance_verdict` is `UNMEASURED`. Evidence provenance for that catalog is `host`. Input-callback, internal-heap, PSRAM, and LVGL free-block series have sample count 0 and provenance `unmeasured/blocked`.

Firmware builds, after the format fixes below:

- `make lab.lab08.build` exit 0. Artifact `build/labs/lab-08-pin-cooldown-manual/lab08_pin_cooldown.bin` (and `.elf`). `CONFIG_LAB08_SCRIPTED` is not set. Build id `lab08-fw-v1-manual`.
- `make lab.lab08.scripted.build` exit 0 on the retry. Artifact `build/labs/lab-08-pin-cooldown-scripted/lab08_pin_cooldown.bin` (and `.elf`). `CONFIG_LAB08_SCRIPTED=y`. Build id `lab08-fw-v1-scripted`.

The first scripted build stopped in an ESP-IDF compiler crash (`esp_lcd_panel_rgb.c` internal compiler error). The retry with `CCACHE_DISABLE=1` completed. `make build` was not used.

`make test` exit 0: 445 tests, 2 skipped. The skip markers were not in the LAB-08 tests.

## Hardware measurements

None. L08-07 has no `run-manifest.json` and no post-reset `serial.txt`. L08-08 has no input-callback samples. This report does not say the device answered quickly or that the cooldown copy is visually approved.

## OPEN

`COM-OPEN-006`, `COM-OPEN-007`, `COM-OPEN-008`, and `COM-OPEN-009` stay OPEN. This lab does not issue a session token, choose a concurrent-key policy, start a retention window, or approve cooldown layout. The fixture lock that keeps one side effect per key is a lab probe. `K-concurrent` and `K-mismatch` record that probe. They do not decide `experiment_verdict`.

## Triggers

No product-spec change. The screen reuses the LAB-07 stage tokens. PIN phase is a view, not a new stage.

## Decision

No product decision is approved here.

## Verification

| ID | Level | Disposition | Evidence |
|---|---|---|---|
| S-01 | L0 | pass | `make lab.lab08.build` and `make lab.lab08.scripted.build` exit 0. Not `make build`. |
| S-02 | L1 | pass | `test_s02_manifest_fields` |
| S-03 | L1 | pass | `test_s03_and_lab08_experiment_inputs` writes metrics `family-link.lab-metrics/v2` |
| S-04 | L1 | pass | `scripts/lab_registry.py validate` and `lab_host.py help lab08` |
| S-05 | L1 | pass | Help text includes the registry instructions and 90-second capture |
| S-06 | L0 | pass | Distinct build directories. Manual sdkconfig has `CONFIG_LAB08_SCRIPTED` unset. Scripted sets it. |
| S-07 | L1 | pass | Adapter rejects a pre-reset harness line and a missing harness marker. Experiment stays `UNMEASURED`. |
| S-07 | L2 | blocked | No device capture. |
| S-08 | L1 | pass | README queue capacity 2. Native overflow case records one `failure` and does not start a second login. |
| S-09 | L1 | pass | `test_lab08_one_terminal` |
| S-10 | L1 | pass | Redaction test, parser PIN rejection, server log check, catalog fingerprint is not a PIN hash |
| S-11 | L1 | pass | Health identity and fixture byte-change without a revision bump |
| S-11 | L2 | blocked | No device run manifest |
| S-12 | L2 | not applicable | This lab has a server on port 8768. The registry entry has no workflow key. |
| S-13 | L1 | pass | Catalog provenance is `host`, never `hardware` |
| S-14 | L1 | pass | Parser success with failed cleanup yields workflow `FAIL` |
| S-14 | L2 | blocked | No device `run-manifest.json` |
| S-15 | L3 | blocked | Series exist with null percentiles. `performance_verdict` is `UNMEASURED`. No budget was approved. |
| S-16 | L1 | pass | README `ResourceBudget` fields, labeled provisional lab probe |
| S-17 | L1 | pass | One `SessionWriter`. `roster-digits` keeps a partial PIN when a roster result arrives. |
| S-18 | L0 | pass | No change under `client/device/` or the device bootstrap artifacts |
| S-19 | L0 | pass | The v1 archive was not modified |
| S-20 | L2 | pass | This report and the evidence index row. Software qualification only. |
| S-21 | L1 | pass | Unset variant with scripted mode selects `scripted`. `backend=local` returns non-zero, binds no port, and is not workflow `PASS`. |
| S-22 | L1 | pass | A failing decided row stays `FAIL` while an exploratory success remains. Exploratory failure does not fail a passing decided set. Empty inputs are `UNMEASURED`. |
| L08-01 | L1 | pass | `test_lab08_transport_schedule` |
| L08-02 | L1 | pass | `test_lab08_cooldown` |
| L08-03 | L1 | pass | `test_lab08_wrong_pin_copy_time` |
| L08-04 | L1 | pass | `test_lab08_idempotent_and_live_body`: duplicate exact request, observe side-effect count 1 |
| L08-05 | L1 | pass | `test_lab08_mask` |
| L08-06 | L1 | pass | `test_lab08_boot` |
| L08-07 | L2 | blocked | No flashed kit, no post-reset serial, no harness marker from a device |
| L08-08 | L3 | blocked | No input-callback samples. `performance_verdict` `UNMEASURED`. |
| L08-09 | L1 | pass | `test_lab08_one_terminal` |
| L08-10 | L1 | pass | `test_lab08_mute_release`: muted submission is `cancelled`, `terminal_count` 1, `display_power` 0, `digit_len` 0. A late success does not undo mute. |
| L08-11 | L1 | pass | `test_s03_and_lab08_experiment_inputs`. Workflow `PASS` with parser `UNMEASURED` does not satisfy this row. |

L0 means the host build. L1 means host or native tests. L2 and L3 device rows above are blocked.
