# LAB-05/v1-peak

**LAB-05/v1-peak = firmware `lab05-peak-v1` (replace arm `lab05-peak-replace-v1`) + server none + fixture `lab05-peak-fixtures/v1`**

Software qualification run `20261019-LAB05PEAK-SOFTWARE-001`, package `L05P-QUAL`, repair count 1, is the L0/L1 record below. Device captures on audrey-kit on 2026-10-20 are in [Device captures](#device-captures). Performance stays **UNMEASURED**. Persistent root, replace control, cover-down visual, and the performance budget stay **OPEN**. Existing `docs/labs/evidence/lab-05/` runs stay lifecycle evidence and are not scored as the peak experiment.

**Return status: L0/L1 software-qualified; L2 captures recorded; L3 series recorded from device traces; performance UNMEASURED.**

ESP-IDF checkout `C:\Users\lynnv\esp\esp-idf` is tag `v5.4.2` (`git describe --tags --exact-match`). Target `esp32s3`. The software package itself did not flash or bind a device. Later captures used kit `audrey-kit`. Host fixtures for this package name only that kit.

## Source and build identity

Owned lab-tree SHA-256 after these edits: `d0fa5eacc81a248a02ab7365f54e8b0faa533d2dc9cccfcd376fe5e6f279a0de`.

Framing: 26 authored files under `client/labs/lab-05-transition-proof`, each as relative path, NUL, file bytes, NUL. Excludes `managed_components`, `__pycache__`, `build`, and a generated `sdkconfig`. The pre-review fingerprint `a07888859254ccc89369123b969df5ac359b188d2a3245f1e5df223f3613a380` used a different framing; the same framing on the tree before these edits was `a984a69cd3df792d5f51ae2a300cc0801533c661e9465424338a13f8db66e11e`.

| Variant | Build ID | `CONFIG_LAB05_PEAK` | `CONFIG_LAB05_SCRIPTED` | `CONFIG_LAB05_PEAK_REPLACE` | sdkconfig SHA-256 | Binary SHA-256 |
|---|---|---|---|---|---|---|
| peak manual | `lab05-peak-v1` | y | not y | not y | `03957b3b73d33fe6f0f5eaee441e26c2d4cb8aa04e8b5688d50b8128a1da6e14` | `7a96c1a8314d76ba727434748ed7e1bb7a7381cca87029ca83e41d4377db94b6` |
| peak scripted | `lab05-peak-v1` | y | y | not y | `5097f14d785c92d02fb558651b573f93ef833d177447f340138afb0dae3b8ed7` | `4529ef37765a8c2a0266a9bc18d655b885fadfc3a1c41499cf6a4ccbb56b7624` |
| peak replace | `lab05-peak-replace-v1` | y | not y | y | `674e23268966bfb15bc5b58461a9c05c209cf18176040715ea3b53271f560feb` | `88fd6a32154c12f5bbf7e79605fca41944f58a1c431ef0132d70069bc02b12c4` |
| peak scripted-replace | `lab05-peak-replace-v1` | y | y | y | `64a2fe069398b514cd2d332f1f2efc7140f92dca647afe1dadcaab95aa7a60ea` | `87d1cbcc0465d9ccbda14fb87d8308c0faaa915710f889abcce1a3a4c376c160` |

Lifecycle variants were not rebuilt. Their compiled inputs did not change: peak C files stay out of the lifecycle link when `CONFIG_LAB05_PEAK` is off. `build/labs/lab-02-*` was not cleaned.

## What was measured

Host and native checks exercised allocation failure, stale callback, cancellation, queue overflow, one terminal per operation, repetition completeness, a full 80-operation ledger, canvas-view retention, and parser scoring. The software package did not flash a board. Performance is **UNMEASURED**. There is no approved performance budget.

## Device captures

Kit `audrey-kit` was selected as the only connected registered kit. Target `esp32s3`. ESP-IDF v5.4.2. These runs were captured after the software qualification record. Peak `metrics.json` files were reparsed with `lab05-peak-parser/v2` so sampled series use `provenance` `hardware`. Samples with count 0 stay `unmeasured/blocked`. The run manifests leave firmware identity fields null; those hashes were not recorded at capture time. Serial logs stay under `logs/device/labs/` and are not committed. No screenshot or screen recording is in the evidence. Cover-down visual stays **OPEN**.

Kept run records:

- `docs/labs/evidence/lab-05/runs/20261020-scripted-run-c5962756/`
- `docs/labs/evidence/lab-05-peak/runs/20261020-scripted-run-7d0776d4/`
- `docs/labs/evidence/lab-05-peak/runs/20261020-scripted-replace-run-33993433/`

| Run | Command result | What it records |
|---|---|---|
| `20261020-scripted-run-c5962756` | Lifecycle `lab05`, workflow `PASS`, experiment `UNMEASURED` | Evidence slug `lab-05`, variant `scripted`. Serial has `LAB05_READY` and no `LAB05_PEAK_READY`. L05-05. |
| `20261020-scripted-run-7d0776d4` | Peak persistent scripted, workflow `PASS`, experiment `PASS` | Evidence slug `lab-05-peak`, variant `scripted`, control `persistent`. `LAB05_PEAK_HARNESS_DONE status=complete control=persistent`. Decided terminals: `P-alloc-fail` `failure`/`alloc`, `P-stale-callback` `stale`, `P-cancel` `cancelled`. Overlap observation: 20 successes, `live_image_count` 2. Evidence status `COMPLETE`. |
| `20261020-scripted-replace-run-33993433` | Peak scripted-replace, workflow `PASS`, experiment `UNMEASURED` | Variant `scripted-replace`, control `replace`. `LAB05_PEAK_HARNESS_DONE status=complete control=replace`. `P-replace` observation: 20 successes, `live_image_count` 1. Decided scenarios are absent, so the experiment stays `UNMEASURED`. `anim_frame_us` has no samples because this arm swaps the image and does not play the 400 ms cover. Evidence status `INCOMPLETE` for that missing series. |

Persistent series from `7d0776d4`, provenance `hardware` except where noted:

| Series | Samples | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|
| `temp_image_bytes` | 65 | 57600 | 57600 | 57600 | 57600 |
| `anim_frame_us` | 164 | 62874 | 82880 | 82949 | 98787 |
| `lvgl_high_water_bytes` | 23 | 12140 | 12140 | 12140 | 12140 |
| `lvgl_largest_free_bytes` | 23 | 51544 | 51544 | 51676 | 51676 |
| `internal_largest_free_bytes` | 23 | 65536 | 94208 | 94208 | 94208 |
| `psram_largest_free_bytes` | 23 | 0 | 0 | 0 | 0 |

Replace series from `33993433`:

| Series | Samples | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|
| `temp_image_bytes` | 60 | 28800 | 28800 | 28800 | 28800 |
| `anim_frame_us` | 0 | unmeasured | unmeasured | unmeasured | unmeasured |
| `lvgl_high_water_bytes` | 20 | 11436 | 11436 | 11436 | 11436 |
| `lvgl_largest_free_bytes` | 20 | 51924 | 51988 | 52012 | 52012 |
| `internal_largest_free_bytes` | 20 | 94208 | 94208 | 94208 | 94208 |
| `psram_largest_free_bytes` | 20 | 0 | 0 | 0 | 0 |

`anim_frame_us` on the replace arm is `unmeasured/blocked`. PSRAM largest free is a measured 0 on both arms. Performance stays **UNMEASURED**.

## OPEN decisions

- Persistent root: **OPEN**. Not approved as the product transition.
- Replace control: **OPEN**. Exploratory control only.
- Cover-down visual: **OPEN**. The 400 ms motion is the animation under test, not a frame budget and not a visual approval.
- Performance budget: **OPEN**. No owner-approved budget. Series stay **UNMEASURED**.

## Standards review dispositions

Reviewer `a640c800-8993-4f81-b8ba-9ce488e69f39`. Owned-source fingerprint at review: `a07888859254ccc89369123b969df5ac359b188d2a3245f1e5df223f3613a380`.

| ID | Disposition |
|---|---|
| LAB05PEAK-F001 | Fixed at L1. Each trace row carries `kind`. The parser scores a decided verdict from the repetition row, keeps worker rows as separate operation results, and counts repetitions from repetition rows only. A 20-repetition overlap log with audio and page workers parses with `repetition_count` 20. A three-row `P-cancel` log, including the kind-less shape, parses as `cancelled` with `terminal_count` 1. |
| LAB05PEAK-F002 | Fixed at L1. `P-alloc-fail` records `control=persistent`. A persistent log is not demoted by that row. A log that stamps it `replace` beside persistent rows becomes `UNMEASURED`. Delete-then-fail behavior remains. |
| LAB05PEAK-F003 | Fixed at L1. `lab05_peak_retain_view` / `lab05_peak_release_view` free a buffer only after the canvas view is released. Lock failure fails the scenario and leaves the buffer allocated until teardown deletes the canvas. The device lock is not unit-tested. This is not L3. |
| LAB05PEAK-F004 | Fixed in firmware, checked by a host source seam. `page_worker` posts a result and does not call `lv_label_set_text`. The scenario task applies label text while holding the display lock. Lock failure requeues or prints `LAB05_PEAK_PUBLISH` `status=rejected`. |
| LAB05PEAK-F005 | Fixed at L1. Allocation failure after cover-down starts records one `failure` / `alloc` terminal, clears `animation_active`, and restores `running_operations`. Covered for overlap and cancel. |
| LAB05PEAK-F006 | Fixed in the harness, with an L1 seam. Internal and PSRAM stay `unmeasured`, and provenance is not `instrumented`, unless `capture_memory` ran. `lab05_peak_heap_metrics_are_instrumented(0)` is not instrumented. |
| LAB05PEAK-F007 | Fixed at L1. The completion path reserves a ledger slot before finishing the cover. A full 80-operation ledger does not complete silently: the cover stays active, live images stay owned by that cover, and no terminal is stored. One free slot completes and the incoming image can be released. |
| LAB05PEAK-F008 | Fixed at L1. A scripted-replace log with no decided rows is `UNMEASURED`, not `FAIL`. An observed wrong decided terminal on that arm is still `FAIL`. Persistent logs that contain the three decided terminals still `PASS` only under L05-11. |
| R9 | Not changed. The 2000 ms animation wait is a hang bound, not the 400 ms cover length and not a pass limit. Residual risk. |
| R10 | Not changed. `anim_frame_us` stores at most 32 samples. Residual risk. No pass-limit change. |
| R11 | Not changed. `audio_running` is set before a rejected publish. Residual risk. |
| R12 | Not changed. Persistent root, replace control, and cover-down visual stay **OPEN**. No product approval. |

## Acceptance ledger

| Acceptance ID | Owner package | Required level | Implemented path | Actual check / artifact | Achieved level | Disposition / blocker |
|---|---|---|---|---|---|---|
| S-01 | L05P-FW | L0 | `client/labs/lab-05-transition-proof` | Four `make lab.lab05peak*.build` commands below exited 0 | L0 | Achieved for the four peak variants rebuilt here. Lifecycle variants were not rebuilt; their compiled inputs did not change. |
| S-02 | L05P-MODEL | L1 | `manifest.lab.json` | `test_lab05_manifest_s02_and_l05_02` inside the focused unittest | L1 | Achieved. `server_path` is `none`. |
| S-03 | L05P-HOST | L1 | `tools/parse_lab05_peak.py` | `test_metrics_document_matches_run_id_and_percentiles` | L1 | Achieved. Document `family-link.lab-metrics/v2`. The L1 test uses a fixture serial. Device metrics were written later by capture. |
| S-04 | L05P-HOST | L1 | `config/labs.registry.yaml` | `scripts/lab_registry.py validate` exit 0; `make lab.help` lists `lab05peak` | L1 | Achieved. |
| S-05 | L05P-HOST | L1 | registry `run.instructions` | `make lab.lab05peak.help` printed 90 s default and the persistent completion marker | L1 | Achieved. |
| S-06 | L05P-FW | L0 | distinct peak `build_dir`s | sdkconfig reads in the identity table: scripted sets `CONFIG_LAB05_SCRIPTED=y`; manual does not | L0 | Achieved for the four rebuilt peak variants. |
| S-07 | L05P-HOST | L1 | adapter post-reset marker check | `test_post_reset_marker_cleanup_interrupt_and_provenance` rejects a pre-reset success line and yields experiment `UNMEASURED` | L1 | Achieved at L1 only. |
| S-07 | L05P-HOST | L2 | on-target post-reset ready and cleanup | Peak runs `7d0776d4` and `33993433`: `capture.post_reset` true, cleanup passed, success marker after reset | L2 | Achieved for those captures. |
| S-08 | L05P-MODEL | L1 | audio-stop and page-apply queues, capacity 8 | `test_lab05_peak_queue_overflow` | L1 | Achieved. Firmware page-result lock failure now requeues or prints a rejection. |
| S-09 | L05P-MODEL | L1 | one terminal per operation id | `test_lab05_peak_one_terminal`, cancel, alloc-fail, cover-alloc | L1 | Achieved. |
| S-10 | L05P-HOST | L1 | `redact_line` and parser secret rejection | `test_s10_redact_line_redacts_url_password_and_bearer`; parser rejects PIN and media payload | L1 | Achieved for host fixtures. No new device evidence files were added. |
| S-11 | L05P-HOST | L1 and L2 | server fixture | `manifest` `server_path` `none` | not applicable | No server. |
| S-12 | L05P-HOST | L2 | no-server manifest | Peak device manifests record `server.path` `none` and `service`/`scenario` `not_configured`, with no bind host or port | L2 | Achieved on `7d0776d4` and `33993433`. |
| S-13 | L05P-HOST | L1 | provenance on heap and timing fields | Fixture parses stay `host` or `unmeasured/blocked`. A device capture context stamps sampled series `hardware`. | L1 | Achieved. F006 stops memset zeros from being labeled instrumented. |
| S-13 | L05P-FW | L3 | device heap, PSRAM, LVGL, frame samples | Reparsed device series with samples use `provenance` `hardware` | L3 | Achieved as recorded samples. Replace `anim_frame_us` stays `unmeasured/blocked`. Performance **UNMEASURED**. |
| S-14 | L05P-HOST | L2 | workflow versus experiment | Device manifests contain both `workflow_status` and `experiment_verdict`. Host fixture still forces workflow `FAIL` on cleanup failure | L2 | Achieved on the captured manifests. |
| S-15 | L05P-FW | L3 | nearest-rank series and largest free blocks | Device metrics record p50, p95, p99, and max for sampled series, plus largest free blocks | L3 | Recorded. Performance **UNMEASURED**. No approved budget. |
| S-16 | L05P-MODEL | L1 | README `ResourceBudget` | `test_lab05_peak_resource_budget` | L1 | Achieved. Numbers are a provisional lab probe, not a product budget. |
| S-17 | L05P-MODEL | L1 | session writer | `test_lab05_peak_single_writer` | L1 | Achieved. |
| S-18 | L05P-QUAL | L0 | product bootstrap | Change set does not include `client/device` or bootstrap artifacts | L0 | Pass by scoped path review. Those trees were not edited. |
| S-19 | L05P-QUAL | L0 | v1 archive | Change set has no archive path | L0 | Pass by scoped path review. The archive was not edited. |
| S-20 | L05P-QUAL | L2 | this report and the evidence index | This file cites the device runs. The evidence index row matches | L2 | Achieved as a record. Performance stays **UNMEASURED**. OPEN decisions stay open. |
| S-21 | L05P-HOST | L1 | scripted variant and backend | `test_scripted_variant_selection_and_local_backend_binds_no_port` | L1 | Achieved. `backend=local` raises and binds no port. |
| S-22 | L05P-HOST | L1 | exploratory versus decided | `test_lab05_peak_experiment_inputs` and the replace-arm test | L1 | Achieved. Replace-only absence is `UNMEASURED`. A wrong decided terminal is `FAIL`. |
| L05-01 | L05P-HOST | L1 | registry gates | `test_lab05peak_registry_gates` | L1 | Achieved. |
| L05-02 | L05P-MODEL | L1 | `manifest.lab.json` `peak` object | `test_lab05_manifest_s02_and_l05_02` | L1 | Achieved. Lifecycle `lab_revision` stays `v1-proof`. |
| L05-03 | L05P-FW | L0 | sdkconfig symbols | Identity table from the four rebuilt variants | L0 | Achieved for peak manual, scripted, and scripted-replace. Lifecycle `make lab.lab05.build` was not rerun. |
| L05-04 | L05P-FW | L1 | peak markers only when `CONFIG_LAB05_PEAK=y` | `test_lifecycle_markers_unchanged` and CMake source selection | L1 | Achieved. |
| L05-05 | L05P-HOST | L2 | lifecycle scripted capture | `docs/labs/evidence/lab-05/runs/20261020-scripted-run-c5962756/` | L2 | Achieved. Workflow `PASS`, experiment `UNMEASURED`, no `LAB05_PEAK_READY`. |
| L05-06 | L05P-HOST | L2 | peak scripted persistent capture | `docs/labs/evidence/lab-05-peak/runs/20261020-scripted-run-7d0776d4/` | L2 | Achieved. Workflow `PASS` with `status=complete control=persistent`. |
| L05-07 | L05P-HOST | L2 | scripted-replace capture | `docs/labs/evidence/lab-05-peak/runs/20261020-scripted-replace-run-33993433/` | L2 | Achieved. Variant `scripted-replace`, control `replace`, experiment `UNMEASURED`. |
| L05-08 | L05P-MODEL | L1 | stale handle | `test_lab05_peak_handles` | L1 | Achieved. |
| L05-09 | L05P-FW | L3 | device LVGL, internal, PSRAM, and frame series | Persistent run records all six series. Replace records heap and `temp_image_bytes`; frames are absent | L3 | Recorded. Performance **UNMEASURED**. |
| L05-10 | L05P-FW | L3 | simultaneous `temp_image_bytes` on device | Persistent overlap max 57600 with live count 2. Replace max 28800 with live count 1 | L3 | Recorded. No pass ceiling. |
| L05-11 | L05P-HOST | L1 | decided inputs `P-alloc-fail`, `P-stale-callback`, `P-cancel` | `test_lab05_peak_experiment_inputs` plus worker-row and persistent-control tests | L1 | Achieved. Persistent logs with those three terminals `PASS`. Replace-only absence is `UNMEASURED`. |
| L05-12 | L05P-MODEL | L1 | cancel returns to `steady_live_images` 1 | `test_lab05_peak_cancel` | L1 | Achieved. Provisional lab probe, not a product budget. |

## Commands actually run

```text
.venv\Scripts\python.exe -m unittest scripts.tests.test_lab05_peak_model scripts.tests.test_lab05_peak_host
```

Exit 0. 38 tests, OK. Repair 1 re-ran the same command: exit 0, 38 tests, OK.

```text
.venv\Scripts\python.exe scripts\lab_registry.py validate
```

Exit 0. `valid: config/labs.registry.yaml`. Repair 1 re-ran the same command: exit 0.

```text
make test
```

Exit 0. `Ran 359 tests in 115.377s`. `OK`. Repair 1 cleared the earlier host failures: local deployment parsing no longer looks up a kit id, LAB-02 directory-escape checks use a junction when symlink creation is denied, the LAB-02 native session pause finishes inside its timeout, LAB-02 server close tests follow the POSIX shutdown path, LAB-04 model guards compile under `-Werror` and the host binary loads the compiler's C++ runtime, LAB-03 fixture regeneration keeps LF newlines, and Windows `make` resolves Git Bash. Invented host runs stay in a temporary directory. `build/labs/lab-02-*` was not cleaned. Peak firmware inputs were not changed, so the four peak variants were not rebuilt.

```text
make lab.lab05peak.build
make lab.lab05peak.scripted.build
make lab.lab05peak.build VARIANT=replace
make lab.lab05peak.build VARIANT=scripted-replace
```

Each exited 0. See the identity table.

```text
make lab.help
make lab.lab05peak.help
git -C C:\Users\lynnv\esp\esp-idf describe --tags --exact-match
```

Help commands exited 0. The ESP-IDF command printed `v5.4.2`.

Not run during the software package: `make flash`, `make flash.all`, `make device.bind`, `make build`, `make lab.lab05`, `make lab.lab05.scripted`, `make lab.lab05peak`, `make lab.lab05peak.scripted`. No commit, push, merge, or pull request. The device captures above were run later on audrey-kit.
