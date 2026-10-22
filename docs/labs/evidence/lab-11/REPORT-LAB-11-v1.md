# LAB-11/v1 software qualification

LAB-11/v1 = firmware lab11-fw-scripted-v1 + server lab11-server/v1 + fixture lab11-send/v1

The manual build id is `lab11-fw-manual-v1`.

| Field | Value |
| --- | --- |
| Target | esp32s3, ESP-BOX-3, ESP-IDF v5.4.2. Build directories `build/labs/lab-11-send-receipt-manual` and `build/labs/lab-11-send-receipt-scripted`. |
| Measured | Host unittest, the production C model against the fixture server, and both firmware builds. No kit capture. |
| Trigger | Software qualification of frozen-capture send, receipt lookup, and the decided failure probes. |
| Decision | OPEN. Product delivery meaning, outbox retention, recipient atomicity, and the upload wire stay open. |
| L0 | Both variants build. Manual sdkconfig has `# CONFIG_LAB11_SCRIPTED is not set`. Scripted sdkconfig has `CONFIG_LAB11_SCRIPTED=y`. |
| L1 | Native model and host parser, server, and adapter tests pass. Scripted success is parsed fixture responses reconciled to durable part and message counts. |
| L2 | No post-reset serial and no `run-manifest.json`. L11-06 stays blocked. |
| L3 | Performance verdict `UNMEASURED`. Calculated peaks are not device measurements. |

Active acceptance condition copied from `fixtures/acceptance.json`: `accept-on-archive-commit`.

COM-OPEN-001: OPEN

COM-OPEN-002: OPEN

COM-OPEN-003: OPEN

COM-OPEN-004: OPEN

COM-OPEN-005: OPEN

OPEN-SKP-03: OPEN

The payload is an opaque synthetic blob. LAB-04 encoded bytes were not copied. The frozen file records accepted point count 128, `duration_cap_ms` 180000, logical 120x120, and capture queue capacity 16. Those bounds were read from the LAB-10 capture policy. This report does not cite a LAB-10 experiment verdict.

Success phase text is `condition met`, and only after the active condition is met. Parsed metrics have no `sent_approved` key. Neither acceptance condition is a product outcome. `D-one` does not rewrite `acceptance.json`.

## Visible behavior

The scene root is created once. Progress and phase text are Level 0 property updates. Next, Chunk, Run, and Cancel return without waiting for upload, receipt lookup, or worker cleanup. The screen shows fixture `lab11-send/v1`, the frozen capture identity, the active condition, the expected result, chunk probe 1024 or 4096, and part queue capacity 8 as a provisional lab probe.

Boot marker: `LAB11_READY build=<id> mode=<manual|scripted> fixture=lab11-send/v1 harness=<0|1>`. Scripted completion: `LAB11_HARNESS_DONE status=complete`. Display init failure prints `LAB11_INIT_FAILED status=display_start_failed` and no ready marker.

## Handoff

The reusable send interface is `lab11_op_begin`, `lab11_plan_parts`, `lab11_worker_exchange`, `lab11_writer_apply`, `lab11_set_progress`, and `lab11_request_cancel`. Transport is a function pointer. Firmware supplies `esp_http_client` with one-chunk copy. The native test supplies a socket exchange to the same runner.

`lab11_capture` owns the immutable payload and the recipient list. A send operation borrows that object, copies the content hash, length, and recipient ids, and never frees the payload. It releases the borrow on success, failure, cancellation, and a stale completion, and frees only its staging and response buffers. `lab11_capture_free` refuses while a borrow is held. Capture bytes stay until the owner frees them. Local timeout or cancellation sets `remote_rollback=0` and is not a remote rollback. A late response after a terminal increments `late_responses` and does not create a second terminal.

Peak contributors, calculated:

| Chunk | Retained input | Staging | Transport copy | Retry extra | Response | Peak |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 5120 | 1024 | 0 when the body is borrowed; 1024 when the client copies one chunk | 0 | 512 | 6656 borrowed / 7680 copied |
| 4096 | 5120 | 4096 | 0 borrowed / 4096 copied | 0 | 512 | 9728 borrowed / 13824 copied |

`full_payload_duplicate_bytes` is 0. Staging is one window, refilled from the borrowed payload. The host scripted run observed the borrowed peaks (6656 and 9728) with provenance `calculated`. The account test observes the copied 4096 peak. Device heap, largest free block, and part timing p50/p95/p99/worst stay `unmeasured/blocked`.

The future live capture, stop, upload, and receipt path is not implemented. End-to-end qualification needs that handoff from a real recording into this send interface. The frozen file is the stand-in, and the ownership boundary is what remains unresolved.

## Acceptance ledger

| Acceptance ID | Owner package | Required level | Implemented path | Actual check/artifact | Achieved level | Disposition/blocker |
| --- | --- | --- | --- | --- | --- | --- |
| S-01 | L11-FW | L0 | `client/labs/lab-11-send-receipt/` | `make lab.lab11.build` exit 0. Not `make build`. | L0 | pass |
| S-02 | L11-HOST | L1 | `manifest.lab.json` | `test_s02_manifest_and_s03_parser` | L1 | pass |
| S-03 | L11-HOST | L1 | `tools/parse_lab_trace.py`, registry `trace_parser` | `test_s02_manifest_and_s03_parser`. Document `family-link.lab-metrics/v2`. | L1 | pass |
| S-04 | L11-HOST | L1 | `config/labs.registry.yaml` `lab11` | `scripts/lab_registry.py validate` exit 0. `make lab.help` lists `lab.lab11` and `lab.lab11.scripted`. | L1 | pass |
| S-05 | L11-HOST | L1 | registry instructions, capture 90 s | `make lab.lab11.help` exit 0 | L1 | pass |
| S-06 | L11-FW | L0 | `sdkconfig.defaults`, `sdkconfig.defaults.scripted` | `make lab.lab11.build` and `make lab.lab11.scripted.build` exit 0. Distinct directories. Scripted symbol only in the scripted sdkconfig. | L0 | pass |
| S-07 | L11-FW | L1 | `lab11_format_boot`; adapter `classify_capture` | `test_s07_pre_reset_missing_harness_and_s14_cleanup`. Pre-reset success is rejected. Missing harness yields workflow not PASS and experiment UNMEASURED. | L1 | pass |
| S-07 | L11-FW | L2 | same firmware path | No post-reset serial from a kit. | none | blocked. No device capture. |
| S-08 | L11-MODEL | L1 | part queue capacity 8 | native `overflow` case in `test_sha256_boot_ownership_progress_overflow_account`. README names the capacity. Ninth admission is one `failure/overflow`, count stays 8, `dropped=0`. | L1 | pass |
| S-09 | L11-MODEL | L1 | one terminal per operation id | `test_lab11_terminal` | L1 | pass |
| S-10 | L11-HOST | L1 | `redact_line`; parser secret rejection | `test_s10_redact_and_s11_health_identity`. No evidence run directory was written. | L1 | pass |
| S-11 | L11-HOST | L1 | `server/labs/lab-11-send-receipt/` | Health echoes `run_id`, fixture version, fixture SHA-256, and server version. A byte change without a revision bump raises `IdentityError`. | L1 | pass |
| S-11 | L11-HOST | L2 | run manifest server and fixture hashes | Adapter records those hashes when `execute` runs. No device run manifest was written. | none | blocked. No kit capture. |
| S-12 | L11-HOST | L2 | this lab has a server on port 8770 | Registry entry omits `workflow`. S-12 applies to labs without a server. | n/a | not applicable |
| S-13 | L11-HOST | L1 | parser provenance | `test_lab11_experiment_inputs_and_open_sent`. Ledgers use `unmeasured/blocked`, never `hardware`. | L1 | pass |
| S-13 | L11-FW | L3 | heap, largest free block, part timing | No hardware capture. | none | blocked |
| S-14 | L11-HOST | L2 | `classify_capture` | Parser success with failed cleanup forces workflow FAIL. | L2 | pass for the host mechanism. No device run manifest. |
| S-15 | L11-QUAL | L3 | resource ledgers | p50, p95, p99, worst, and largest free block are null. `performance_verdict` is `UNMEASURED`. | recorded, not promoted | pass for the non-promotion rule. No performance PASS. |
| S-16 | L11-QUAL | L1 | `client/labs/lab-11-send-receipt/README.md` | `test_owned_server_stops_and_readme_budget`. ResourceBudget labeled provisional lab probe. | L1 | pass |
| S-17 | L11-MODEL | L1 | writer id 11 applies terminals | native `ownership` and `progress` cases. A wrong writer is rejected. | L1 | pass |
| S-18 | L11-QUAL | L0 | change set | No path under `client/device/` or `artifacts/device/bootstrap/`. | L0 | pass |
| S-19 | L11-QUAL | L0 | change set | No new path under the v1 POC archive. Pre-existing dirty archive files were left untouched. | L0 | pass |
| S-20 | L11-QUAL | L2 | this report and the evidence index | `docs/labs/evidence/README.md` row: software-qualified, hardware pending. | L2 | pass for the software report. Hardware pending. |
| S-21 | L11-HOST | L1 | `scripts/lab_adapter_lab11.py` | `test_s21_scripted_variant_and_local_backend`. Unset variant with mode scripted selects scripted. `backend=local` returns non-zero, binds nothing, and workflow is not PASS. | L1 | pass |
| S-22 | L11-HOST | L1 | parser verdict | `test_s22_exploratory_rows_do_not_decide_the_verdict` | L1 | pass |
| L11-01 | L11-MODEL | L1 | `fixtures/frozen-capture.json` | `test_lab11_frozen_hash`. File hash matches the manifest. Point count, duration, and 120x120 stay inside the capture-policy bounds. Queue capacity 16. | L1 | pass |
| L11-02 | L11-MODEL | L1 | lost complete, then lookup | `test_lab11_one_accept`. Accepted count stays 1. `complete_posts` is 1. `lookup_same` is 1. | L1 | pass |
| L11-03 | L11-MODEL | L1 | dropped ack and identity conflict | `test_lab11_part_retry`. Retry keeps the operation and payload. Durable parts equal unique sequences. Conflict does not overwrite. | L1 | pass |
| L11-04 | L11-FW | L1 | static scene, Level 0 labels | `test_lab11_progress_level`. `scene_generation=1` and `progress_level=0`. | L1 | pass |
| L11-05 | L11-MODEL | L1 | one terminal; no open checking phase | `test_lab11_terminal` | L1 | pass |
| L11-06 | L11-HOST | L2 | `make lab.lab11.scripted` on a kit | Host classifier requires `LAB11_HARNESS_DONE status=complete` and cleanup. No `run-manifest.json` or post-reset `serial.txt`. | none | blocked. No device run. |
| L11-07 | L11-QUAL | L3 | both chunk probes | Calculated peaks recorded. Host observation is `calculated`. Device p50/p95/p99/worst and largest free block are unmeasured. Performance verdict `UNMEASURED`. | none | blocked. UNMEASURED |
| L11-08 | L11-QUAL | L1 | `fixtures/acceptance.json` | Condition `accept-on-archive-commit` copied above. Report line `COM-OPEN-001: OPEN`. Parser output has no `sent_approved`. | L1 | pass |
| L11-09 | L11-MODEL | L1 | storage-full and cleanup | `test_lab11_storage_full`. One failure, reason `storage`, `terminal_count` 1, `live_buffers` 0, sketch and PCM lengths 0, work `discarded`. | L1 | pass |
| L11-10 | L11-HOST | L1 | parser experiment inputs | `test_lab11_experiment_inputs_and_open_sent` and `test_lab11_cancel_phases`. Inputs are the five decided ids. Exploratory rows stay in `observation`. `D-one` leaves `acceptance.json` unchanged. | L1 | pass |

## Checks

`make test` exit 0: 440 tests, 5 skipped. `scripts/lab_registry.py validate`, `make config.labs`, `make lab.help`, and `make lab.lab11.help` exited 0. No files were written under `docs/labs/evidence/lab-11/runs/`.

An independent standards review was not run. Wi-Fi in this lab is a namespaced up/down probe, not an association. The HTTP client timeout of 1000 ms is a lab probe. Named host deadlines are host limits.
