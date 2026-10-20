# LAB-06/v1 — Stage shell and panel lifecycle

## Qualification status

**Software qualification status: PASS at L0/L1 only.** The stable-candidate
standards review's confirmed S-10 finding `LAB06-F001` was repaired and
independently verified at L1. The later manual-operator usability correction
passes 58 focused host/model tests, both registered builds, and the full suite
(`Ran 357 tests in 94.757s; OK`). The initial full-suite attempt was blocked
by sandbox-only localhost-bind errors; the authorized retry passed. Nine user
captures from 2026-10-20 are preserved. The latest scripted capture on source
SHA `8a121031…` passed workflow and experiment validation; the latest manual
capture parsed, recorded a successful cancel terminal, and is INCOMPLETE as
expected for exploratory mode. Both captures use the current visible-guidance
and reset-banner source. The scripted PASS is an observed user-run L2 capture,
not formal independent DEVICE qualification. No L3 performance qualification
is claimed. No screenshot or synchronized recording is available.

The latest scripted capture's experiment verdict is **PASS**. The latest
manual capture is **INCOMPLETE**, which is expected for exploratory mode and is
not a firmware or parser failure. Formal independent DEVICE qualification is
still pending. The performance verdict is **UNMEASURED**. Host parser fixtures
that produce a synthetic `PASS` exercise parser logic only; they are not a run,
serial capture, or device result.

## Lab version and identity

| Item | Identity |
|---|---|
| Lab / firmware | `LAB-06/v2`, firmware IDs `lab06-fw-manual-v3` and `lab06-fw-scripted-v3` |
| Project | `client/labs/lab-06-stage-shell/` |
| Manifest / fixture / trace | `family-link.lab-manifest/v1`; `lab06-stage-shell/v1`; `lab06-trace/v1` |
| Server | `none`; no server process or port was used |
| Target class | ESP32-S3 / ESP-BOX-3 BSP; user-run manifests identify kit nickname `lynn-kit`; board serial/identity is not recorded |
| Toolchain in verified build evidence | ESP-IDF v5.4.2, commit `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`; Xtensa GCC 14.2.0 |
| Build directories | `build/labs/lab-06-stage-shell-manual`; `build/labs/lab-06-stage-shell-scripted` |
| Fixture tree SHA-256 | `742c63abaca14bcd4d7bf1d948a49e2537a3f2e3c0c32005501d1c40c56b0ade` |
| Last device-capture source SHA-256 | `8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9` |
| Current software-verified build source SHA-256 | `8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9` |
| Dependency lock SHA-256 | `295fdc6026ed7555597a49e7ca1b4a74e8028920f67aeae4b6bac49281f686ba` |

The first two user captures used source fingerprint
`38145c1e1e090f50f2440d4a3818c5c615355ecd3ff14f0d239d4ffeeb0b1df1`; the next
scripted capture used `1bb3e521185f024daf77cfe1de9e3efbdc20852b650f100190965f6c2e02f479`;
the newest scripted and manual captures used
`cfdeeec6bd157328758de579b07324bc996aad27ecfb396fc851ea993093406c`. The
latest scripted and manual captures now use source
`8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9`, the
current visible-guidance source. Parser, adapter, tests, report, and
documentation are not compiled firmware inputs.

| Variant | Exact command and result | Effective config and artifact identity |
|---|---|---|
| Manual | `make lab.lab06.build` — passed for current software-verified candidate | `CONFIG_LV_MEM_SIZE=65536`; `CONFIG_LAB06_SCRIPTED` unset. Make fingerprint `sha256:18a7096fc2136296412758050ae9725b74b73f82b8af8812b68bbf44ad0dcf75`; sdkconfig SHA-256 `e3a7cf2d027b901cab369b2f2ec95cd4e708038d7e1bfb75379aca6f10f910ee`; BIN SHA-256 `ca5cbadd997220216088a57db216fc765bd124b4b15d48ce7f5710d68efcb9b8` |
| Scripted | `make lab.lab06.scripted.build` — passed for current software-verified candidate | `CONFIG_LV_MEM_SIZE=65536`; `CONFIG_LAB06_SCRIPTED=y`. Make fingerprint `sha256:ab032d020290b071ab97e7da5c49a17ce73e32268583e878c2045b7e335d2f9f`; sdkconfig SHA-256 `9aca9f678e7b5199b17974120a1570e7cab4c04ee31cb124a59b05450da6b271`; BIN SHA-256 `c1e44480217f6a3c82af779f53e996ebd01be9b740653d63a399f798d9015fd0` |

The current build identities also appear in the newest post-reset user captures.
All nine user-run manifests, event journals, metrics, and serial captures are
retained below. The newest scripted PASS and manual INCOMPLETE runs use current
source SHA `8a121031…`. The scripted metrics JSON records performance as
UNMEASURED; the manual metrics JSON records exploratory samples and a
successful cancellation terminal, but no complete decided-scenario inventory.
Neither artifact qualifies L3.

## User-run captures and disposition

The user ran `make lab.lab06` and `make lab.lab06.scripted` on 2026-10-20.
All raw artifacts are immutable and are not upgraded by later software
repairs. The latest scripted and manual captures use source SHA
`8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9`, including
the two-button guidance, shorter manual capture window, and pre-reset banner.

| Run | Captured outcome | Reason and disposition |
|---|---|---|
| Manual `20261020-manual-run-95e789c0` | `workflow_status=FAIL`; build, activity, and cleanup passed; `experiment_verdict=UNMEASURED` | Parser rejected `lvgl_high_water_pct=12320` as an invalid resource sample. The old firmware wrote `lv_mem_monitor_t.max_used` bytes into the percentage field. No metrics JSON was produced. [Manifest](runs/20261020-manual-run-95e789c0/run-manifest.json), [events](runs/20261020-manual-run-95e789c0/events.jsonl), [serial capture](../../../../logs/device/labs/lab-06/20261020-manual-run-95e789c0/serial.txt). |
| Scripted `20261020-scripted-run-3af0c13d` | `workflow_status=FAIL`; activity ended with `RuntimeError`; `experiment_verdict=UNMEASURED` | Serial ended after H-cancel reported success from hangout to record and contained no `LAB06_HARNESS_DONE` marker. The old harness could miss its cancellation midpoint and wait until the host's 240-second harness deadline. No metrics JSON was produced. [Manifest](runs/20261020-scripted-run-3af0c13d/run-manifest.json), [events](runs/20261020-scripted-run-3af0c13d/events.jsonl), [serial capture](../../../../logs/device/labs/lab-06/20261020-scripted-run-3af0c13d/serial.txt). |
| Scripted `20261020-scripted-run-b74a2511` | `workflow_status=FAIL`; build/boot and cleanup passed; `experiment_verdict=UNMEASURED` | This post-reset run used source SHA `1bb3e521185f024daf77cfe1de9e3efbdc20852b650f100190965f6c2e02f479`, with valid `lvgl_high_water_pct` samples and a fail-closed `LAB06_HARNESS_DONE status=failure reason=scenario`. H-cancel operation 254 nevertheless terminalized `success` from hangout to record (`reason=stage_changed`); `anim_us=511901`. This demonstrates that the prior ten-callback midpoint trigger ran too late on target. No metrics JSON was produced. [Manifest](runs/20261020-scripted-run-b74a2511/run-manifest.json), [events](runs/20261020-scripted-run-b74a2511/events.jsonl), [serial capture](../../../../logs/device/labs/lab-06/20261020-scripted-run-b74a2511/serial.txt). |
| Scripted `20261020-scripted-run-bc37a550` | `workflow_status=PASS`; `experiment_verdict=PASS`; post-reset L2 device serial capture; cleanup passed | Used source SHA `3be37ff631bcfe76e6b9edf9a006d1907ee017a21c7a7eea0ab491e29d11d6cc`, build fingerprint `sha256:795ca2a3ad6851712c5a6672193af9dc49befbf6c46a5c5197c1bc8cd324187c`, BIN SHA `c0334fe4069f5d8f75ee317c25de5918b68816680b3e03f9451a0d42a8c45751`. Decided scenarios passed; H-cancel op 254 terminalized `cancelled`, hangout→hangout, timer count 1, 19 live widgets, `anim_us=240321`; summary was 247 pass / 0 fail. Trace summary reports overflow 1 and stale handle use 0; firmware trace loss remains UNKNOWN; 757 rows were parsed. Metrics JSON exists but performance remains UNMEASURED. This is a successful observed scripted run, not independent DEVICE qualification and not a run of the later manual-UX source. [Manifest](runs/20261020-scripted-run-bc37a550/run-manifest.json), [events](runs/20261020-scripted-run-bc37a550/events.jsonl), [metrics](runs/20261020-scripted-run-bc37a550/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-scripted-run-bc37a550/serial.txt). |
| Scripted `20261020-scripted-run-02aecdcc` | `workflow_status=PASS`; `experiment_verdict=PASS`; post-reset capture passed | Used the arm-first manual UX source SHA `cfdeeec6bd157328758de579b07324bc996aad27ecfb396fc851ea993093406c`. This confirms the scripted path on that source; performance remains UNMEASURED and formal DEVICE qualification remains pending. [Manifest](runs/20261020-scripted-run-02aecdcc/run-manifest.json), [events](runs/20261020-scripted-run-02aecdcc/events.jsonl), [metrics](runs/20261020-scripted-run-02aecdcc/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-scripted-run-02aecdcc/serial.txt). |
| Manual `20261020-manual-run-da41e850` | `workflow_status=PASS`; `experiment_verdict=INCOMPLETE`; parser passed; `summary_present=false` | Used the same source SHA/build family as the paired scripted capture. This was an exploratory manual session with 527 trace rows: repeated H-data (80), H-destroyed (62), H-data-during-exit (24), and no complete H-cycle/H-cancel scenario inventory. INCOMPLETE is expected because manual mode does not run the decided scripted inventory; it is not a firmware or parser failure or user error. The operator reported repeated tapping because the Cancel control animated over another control during the transition. Current software-verified instructions remove that timing requirement: tap Cancel while stationary to arm, tap Exit+Data, and watch the automatic reverse. A later run on the updated source is recorded below. Metrics JSON is retained; performance remains UNMEASURED. [Manifest](runs/20261020-manual-run-da41e850/run-manifest.json), [events](runs/20261020-manual-run-da41e850/events.jsonl), [metrics](runs/20261020-manual-run-da41e850/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-manual-run-da41e850/serial.txt). |
| Manual `20261020-manual-run-583664c7` | `workflow_status=PASS`; `experiment_verdict=INCOMPLETE`; post-reset capture and parser passed | Used source SHA `cfdeeec6bd157328758de579b07324bc996aad27ecfb396fc851ea993093406c`. The capture contains 77 trace rows; operation 11 terminalized `cancelled` from roster to roster with 19 live widgets, one shell timer, and `anim_us=268385`. It also contains successful bounded H-data updates and a stale update during exit; no complete decided-scenario inventory or summary is expected from manual mode, so INCOMPLETE is expected. The operator reported that the guidance was difficult to pick out among labels and four controls and that the 180-second window was too long. The current candidate increases the two primary controls' visual prominence, marks Badge/Stale optional, places the steps in a stronger two-line guide, and reduces the default window to 30 seconds. The visual refinement was subsequently run in `20261020-manual-run-9dd0a309`; see its disposition below. Metrics are retained; performance remains UNMEASURED. [Manifest](runs/20261020-manual-run-583664c7/run-manifest.json), [events](runs/20261020-manual-run-583664c7/events.jsonl), [metrics](runs/20261020-manual-run-583664c7/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-manual-run-583664c7/serial.txt). |
| Scripted `20261020-scripted-run-77e3a3ce` | `workflow_status=PASS`; `experiment_verdict=PASS`; 757 parsed trace rows; post-reset current-source capture | Used source SHA `8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9`, matching the current scripted build fingerprint. Decided scenarios passed (247 pass / 0 fail); H-cancel op254 terminalized `cancelled`, hangout→hangout, with one timer and 19 live widgets. Performance remains UNMEASURED, firmware trace loss UNKNOWN, and formal independent DEVICE qualification pending. [Manifest](runs/20261020-scripted-run-77e3a3ce/run-manifest.json), [events](runs/20261020-scripted-run-77e3a3ce/events.jsonl), [metrics](runs/20261020-scripted-run-77e3a3ce/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-scripted-run-77e3a3ce/serial.txt). |
| Manual `20261020-manual-run-9dd0a309` | `workflow_status=PASS`; `experiment_verdict=INCOMPLETE`; 17 parsed trace rows; post-reset current-source capture | Used source SHA `8a121031cd6e3631cf78d28eb96714ae25bbde655e84244a66929f4ec43da9c9`, matching the current manual build fingerprint. The exploratory session records 12 successful bounded H-data operations, a stale update during exit, and transition op3 terminalized `cancelled` from roster to roster with one timer and 19 live widgets (`anim_us=259988`). The decided manual inventory is incomplete by design; this is not a workflow or parser failure. Performance remains UNMEASURED and trace loss UNKNOWN. [Manifest](runs/20261020-manual-run-9dd0a309/run-manifest.json), [events](runs/20261020-manual-run-9dd0a309/events.jsonl), [metrics](runs/20261020-manual-run-9dd0a309/metrics.json), [serial capture](../../../../logs/device/labs/lab-06/20261020-manual-run-9dd0a309/serial.txt). |

The firmware serializes LVGL's `used_pct` field, ignores touchscreen clicks
while the scripted state machine owns the run, and emits an explicit
failed-scenario marker if cancellation completes without being cancelled. The elapsed-time cancellation repair at 200 ms was exercised successfully in
the current scripted and manual captures: H-cancel op254 and manual transition
op3 both terminalized `cancelled` and preserved one shell timer. After the
operator reported that Cancel moved over another control during the animation,
the manual probe changed to arm cancellation while stationary, then start
`Exit+Data` and watch the automatic reverse. The current visible-guidance
candidate has 58 focused host/model tests passing, both manual and scripted
builds passing, and the full root suite passing 357 tests (`Ran 357 tests in
94.757s; OK`). Both modes now have post-reset captures on this source. No user
action during a 400 ms transition is required.

## Scope, target, and operator view

The probe uses a persistent 320×240 shell with an upper 320×80 slot and a lower
320×160 slot. Its six synthetic stages are roster, PIN, empty hangout, inbox,
record, and receipt. Stage transitions replace slot panels while preserving
shell geometry. Data changes remain Level 0 or Level 1; a session-stage change
uses Level 2. The scripted firmware is designed to exercise 20 cycles per stage,
bounded queue overflow, cancellation, stale-generation rejection, and a
400 ms cover-down motion probe.

The firmware's operator view is a rendered panel guide: `<` and `>` move
through stages; `Badge` applies synthetic badge/progress/roster data; `Exit+Data`
submits a bounded update during exit; `Cancel` reverses an active transition;
`Stale` delivers a saved generation after panel destruction. H-cycle compares
19 live LVGL panel objects and one LAB-06-owned LVGL timer after each transition.
The user reported that the earlier layout placed the moving Cancel control over
another control and required repeated attempts during the short transition.
The revised sequence arms cancellation before motion, so it does not require
timed tapping. There is no screenshot or visual review to verify exact display
appearance. There is no server, real login,
authentication, household data, media, capture, or send path.

For the manual probe, tap `Cancel` once while the stage is stationary, confirm
`Cancel armed`, tap `Exit+Data`, then watch the cover reverse automatically.
No button needs to be hit during the 400 ms motion. `Badge` and `Stale` are
separate data and stale-generation probes; `<` and `>` navigate synthetic
stages. The normal commands are `make lab.lab06` and
`make lab.lab06.scripted`.

## Trigger, measurement, and decision

| Topic | Trigger / target | Measurement | Decision |
|---|---|---|---|
| Product question | Whether the 320×240 shell can change stages without stale handles, duplicate timers, or whole-screen rebuilds on data changes | Nine user-run captures are preserved; latest scripted run `20261020-scripted-run-77e3a3ce` passed its decided experiment on current source `8a121031…`; the latest manual capture also uses this source | Successful observed scripted experiment; formal independent DEVICE qualification remains pending |
| Target | ESP32-S3 / ESP-BOX-3 build configuration; run manifests name kit nickname `lynn-kit` | Post-reset serial captures exist; exact board identity is absent | Scripted L2 capture observed PASS; formal DEVICE package/independent qualification remains pending; latest manual and scripted captures use the current visible-guidance source |
| Timing | `anim_us` and `build_us`, p50/p95/p99/worst in microseconds | Scripted and manual metrics JSON contain timing series; scripted metrics mark performance verdict UNMEASURED | L06-06 performance **UNMEASURED**; no threshold or product conclusion is inferred |
| Memory | LVGL pool high-water/largest free block; internal heap and PSRAM high-water/largest-free observations | New captures contain parsed resource samples; firmware trace loss is UNKNOWN; no accepted performance decision | No budget comparison or performance decision |
| Visual/product screens | Draft split-pane, panel, and transition layouts; placeholder screens identified in `docs/product/device-ui/screens/MISSING.md` | No screenshot or visual review | Decisions remain **OPEN**; no visual approval |
| Review trigger | Stable-candidate standards review; independent finding verification and final QUAL verifier | Review confirmed `LAB06-F001`; invocation 24 independently verified its repair. Invocation 28 repaired the demonstrated cancellation timing defect; invocation 29 independently verified it. Final verifier invocation 31 passed the full root suite after a sandbox-only localhost-bind failure and authorized retry. | Software package evidence is PASS at L0/L1 only; DEVICE evidence remains pending |

Placeholder panels are exploratory and excluded from `experiment_inputs`.
Connecting and Wi-Fi copy, inbox, settings, recipient picker, record chrome,
and send feedback remain **OPEN** as specified by `MISSING.md`; this report
does not approve those screens or theme choices.

## Verification and evidence levels

| Check | Evidence | Level and limit |
|---|---|---|
| Production model | `.venv/bin/python -m unittest scripts.tests.test_lab06_model` — 14 tests; actual production C compiled with C11 warnings-as-errors | L1 host/native only |
| Parser and adapter | `.venv/bin/python -m unittest scripts.tests.test_lab06_host` | L1 host fixture logic only; user-run old-firmware captures failed parsing/completion |
| Combined focused regression | `.venv/bin/python -m unittest scripts.tests.test_lab_host scripts.tests.test_lab06_host scripts.tests.test_lab06_model` — 58 tests passed for the current visible-guidance candidate; independent earlier verification remains recorded above | L1; includes resource-percent, manual-INCOMPLETE, evidence redaction, scripted input/cancellation, elapsed-time cancellation scheduling, arm-first interaction, and all-lab reset-banner coverage |
| Parser CLI | Synthetic complete trace passed through `tools/parse_lab_trace.py` and produced LAB-06 metrics JSON | L1 parser entry-point check; synthetic success is not device success |
| Registry and operator entry points | `make config.labs`; `make lab.help`; `make lab.lab06.help` — passed | L1 host integration |
| Manifest, fixture, trace JSON and whitespace | `python -m json.tool` on LAB-06 JSON documents; `git diff --check` — passed | L1 integrity checks |
| Manual and scripted builds | Registered `make` commands and identities in the table above | L0 only |
| Full root suite | Earlier verifier run: `Ran 355 tests in 101.823s; OK`. Current candidate: sandbox attempt had 15 localhost-bind errors; authorized retry passed `Ran 357 tests in 94.757s; OK`. | Current visible-guidance software candidate PASS at L0/L1. The failed sandbox attempt is retained; no target or L3 performance claim is made. |
| Device behavior and measurements | Nine user-run post-reset captures, including scripted experiment PASS and manual exploratory INCOMPLETE on current source `8a121031…` | Successful observed scripted L2 capture; formal independent DEVICE qualification pending. Performance remains UNMEASURED; firmware trace loss UNKNOWN. |

Focused test coverage includes single-writer rejection, geometry, idempotent
entry, production-C stage cycling, data during exit, stale callbacks after
destroy, cancellation, Level-2-on-data negative control, queue capacity and
overflow accounting, accepted-operation terminal cardinality, bounded pending
capacity, frozen manifest/fixture/trace identity, exact decided inputs, reset-
gated markers, missing/mismatched/duplicate markers, cleanup failure, bounded
and redacted serial, JSON-quoted secret-key rejection before serial persistence,
password-value redaction, acceptance of a non-secret JSON trace line, parser
rejection of raw PIN/media payload, scripted variant selection, unsupported
backend rejection before target I/O, missing decided outcomes, and exploratory-
row verdict isolation. These are software checks; they do not establish device
transition, touch, timer, heap, LVGL, or
visual results.

## Standards review finding and remediation

The stable-candidate client coding standards review confirmed `LAB06-F001`
(P2), violating S-10 and client coding standard §10. A serial line containing a
JSON-quoted key such as `{"pin":"1234"}`, `{"password":"secret"}`, or
`{"media_payload":"AAAA"}` passed the prior host sanitizer unchanged. The
capture path wrote raw bytes to `serial.txt` before the later parser rejected
the PIN or payload; a password value could likewise remain in the persisted
evidence.

The adapter now rejects JSON-quoted PIN and media-payload fields before the
line is written, and redacts JSON-quoted password/passwd values before parser
validation and persistence. Focused host integration coverage captures each
trigger and confirms the secret value is absent from persisted output; a
non-secret JSON trace line remains accepted. Independent verifier invocation 24 passed: it exercised the adapter capture path,
confirmed rejected PIN/media values never reach persisted `serial.txt`, verified
password redaction and safe JSON acceptance, and reran the focused suite. Final
The earlier QUAL invocation 25 also reran the focused suite and completed its
then-current full root suite; that evidence is historical after the later FW
repair and is not the final-candidate suite result.
`LAB06-F001` is **closed at L1** for the software evidence boundary. This does
not claim that a device log exists.

The reviewer also noted an **unconfirmed residual input-size risk**: the model
truncates panel text above its declared 47-byte limit. The reviewer did not
classify this as reachable from current fixed short-string firmware callers.
No behavior change or model change is made for that unconfirmed risk.

## Acceptance ledger

“Achieved level” is the highest evidence level actually supported. A lower-level
pass does not satisfy a higher-level device criterion. For rows with no device
evidence, the disposition is explicitly pending/blocked rather than inferred
from firmware code or host fixtures.

| Requirement | Owner | Required level | Implemented path | Actual command / artifact | Achieved level | Disposition / blocker |
|---|---|---:|---|---|---:|---|
| S-01 ESP-IDF project and make build | L06-FW / L06-HOST | L0 | `client/labs/lab-06-stage-shell/{CMakeLists.txt,sdkconfig.defaults,main/Kconfig.projbuild}` | `make lab.lab06.build`; `make lab.lab06.scripted.build`; build identities above | L0 | Pass; both registered builds exited 0 and pool config is 65536 |
| S-02 manifest minimum fields | L06-MODEL | L1 | `client/labs/lab-06-stage-shell/manifest.lab.json` | `scripts.tests.test_lab06_model.test_lab06_manifest_and_scenarios` | L1 | Pass |
| S-03 trace schema and project parser | L06-HOST | L1 | `trace-schema.json`; `tools/parse_lab_trace.py` | Host parser tests and synthetic CLI; latest scripted and manual captures parsed; earlier invalid-resource and missing-harness failures retained | L1 | Software parser passes; latest scripted experiment PASS and manual exploratory capture INCOMPLETE |
| S-04 registry and make help | L06-HOST | L1 | `config/labs.registry.yaml`; `config/labs/labs.registry.schema.v1.json`; `scripts/lab_adapter_registry.py` | `make config.labs`; `make lab.help`; registry validation in HOST verifier | L1 | Pass |
| S-05 operator help and default duration | L06-HOST | L1 | registry instructions, pre-reset run banner, and LAB-06 README | `make lab.lab06.help`; focused host test | L1 | Pass; default manual capture is 30 seconds |
| S-06 distinct manual/scripted build directories and configs | L06-HOST | L0 | registry variants and per-variant sdkconfig defaults | Both exact make build commands and fingerprints above | L0 | Pass; separate directories, both pool values 65536, scripted symbol only in scripted |
| S-07 fail-closed boot marker | L06-FW / L06-HOST / DEVICE | L1 and L2 | `main/app_main.c`; `scripts/lab_adapter_lab06.py`; host reset/capture tests | Host reset tests; all nine serial captures contain post-reset ready markers; latest scripted run has the completed harness marker | L2 | Target boot and completed scripted lifecycle observed; formal DEVICE qualification remains pending |
| S-08 bounded queues and explicit overflow | L06-MODEL / L06-FW | L1 | model event queue and FW bounded publication; README capacity 8 | `test_lab06_event_overflow`; parser overflow fixture; raw scripted old-build trace has overflow row | L1 | Software pass; raw row is from a failed, unqualified run |
| S-09 exactly one terminal per accepted operation | L06-MODEL / L06-FW | L1 | production model operation IDs/queue and FW scenario accounting | Model/host tests; raw old-build serial contains operation terminal rows but its scripted scenario did not complete | L1 | Software pass; no complete device run qualifies the operation set |
| S-10 secret-free evidence | L06-HOST | L1 | adapter redaction and parser validation | Existing URL/password/Bearer checks plus `LAB06-F001` capture regressions for JSON-quoted PIN, password, media payload, and a safe JSON trace line; independent verification invocation 24 | L1 | Pass for software capture handling; no device log exists |
| S-11 versioned server fixture | Not applicable | L1 and L2 if server exists | Manifest `server_path: none`; adapter has no service/listener | `manifest.lab.json`; HOST adapter tests assert no server hook | — | N/A: server is `none` |
| S-12 no-server run metadata | L06-HOST / DEVICE | L2 | manifest and no-server adapter configuration | All nine user-run manifests record `server.path=none` and `post_reset=true`; latest scripted capture is post-reset and passed | L2 | No-server target capture observed; formal DEVICE qualification remains pending |
| S-13 host claims retain provenance | L06-HOST / DEVICE | L1 versus L3 | parser host-parse context and metrics provenance | Provenance tests; latest scripted and manual manifests retain run/build/serial identity; parsed metrics identify device capture provenance | L2 | L1 and capture provenance observed; no accepted L3 claim; performance UNMEASURED |
| S-14 workflow and experiment remain distinct | L06-HOST | L2 | adapter cleanup result and parser verdict fields | Latest scripted manifest records PASS/PASS; latest manual manifest records workflow PASS and experiment INCOMPLETE; older FAIL/UNMEASURED captures remain preserved | L2 | Separation observed in real runs; manual exploratory capture is incomplete by design |
| S-15 performance series and free-block reporting | DEVICE | L3 | firmware instrumentation in `main/lab06_fw.c`; parser schema | Parsed timing/resource samples and metrics JSON exist for the latest scripted and manual captures; firmware trace loss UNKNOWN | — | L3 pending; performance **UNMEASURED**, no threshold or budget decision |
| S-16 declared resource envelope | L06-MODEL | L1 | LAB-06 README `ResourceBudget` | `test_lab06_pending_capacity` checks declared concurrency 9 and model bound | L1 | Pass as provisional lab probe only; not a measured or approved product budget |
| S-17 single session writer and partial-input protection | L06-MODEL | L1 | production model session owner and event publication contract | `test_lab06_single_writer`; focused production-C model suite | L1 | Pass; no connected product/network worker or device behavior claimed |
| S-18 product bootstrap untouched | L06-QUAL | L0 | scoped LAB-06 source/docs/tests only | `git status --short`; change-path review | L0 | Pass; no `client/device/` or bootstrap artifact edit |
| S-19 archive untouched | L06-QUAL | L0 | scoped LAB-06 source/docs/tests only | `git status --short`; change-path review | L0 | Pass; no `poc-v1/` path changed |
| S-20 evidence report and index | L06-QUAL / DEVICE | L2 | this report and `docs/labs/evidence/README.md` | Report, index row, ledger, nine run artifact groups, including scripted experiment PASS and manual INCOMPLETE | L1 | Evidence published; report is not independent DEVICE qualification |
| S-21 scripted adapter and unsupported backend | L06-HOST | L1 | `scripts/lab_adapter_lab06.py` | host tests select scripted from mode and reject `backend=local` before target I/O; help check | L1 | Pass; no backend is supported and no port was bound |
| S-22 exploratory rows excluded from verdict | L06-HOST | L1 | parser scenario classification and fixtures | host tests for decided-failure/exploratory-success, decided-success/exploratory-failure, and empty inputs | L1 | Pass; H-placeholder observation is preserved but excluded |
| L06-01 both variant builds and pool size | L06-FW / L06-HOST | L0 | manual/scripted project configurations | Exact registered make commands; config and artifact identities above | L0 | Pass |
| L06-02 second stage writer rejected | L06-MODEL | L1 | `components/lab06_model/lab06_model.c` | `test_lab06_single_writer` | L1 | Pass |
| L06-03 split-pane geometry | L06-MODEL | L1 | production model geometry constants | `test_lab06_geometry` | L1 | Pass for 320×80 upper and 320×160 lower slots; no visual approval |
| L06-04 idempotent entry | L06-MODEL | L1 | production model timer ownership | `test_lab06_idempotent_enter` | L1 | Pass in model; no accepted target timer-cycle result |
| L06-05 scripted target lifecycle and post-reset serial | DEVICE | L2 | scripted harness and validated adapter capture | `20261020-scripted-run-77e3a3ce` post-reset capture on current source: workflow PASS, experiment PASS, 247 scenario passes, 0 failures; H-cancel op254 ended `cancelled`, hangout→hangout, with timer count 1 and 19 live widgets | L2 observed | Successful user-run scripted capture on current source `8a121031…`; formal independent DEVICE qualification is still pending |
| L06-06 animation/build quantiles and LVGL memory | DEVICE | L3 | trace instrumentation and resource ledgers in `main/lab06_fw.c` | Parsed timing and resource series exist in latest scripted and manual metrics; firmware trace loss UNKNOWN | — | **UNMEASURED**; no accepted quantiles/resource decision or threshold |
| L06-07 exploratory placeholder trace | DEVICE (host mechanism: L06-HOST) | L2 | parser classification; H-placeholder fixture | Host placeholder parser test; latest scripted metrics preserve the exploratory observation outside decided inputs | L1 | Host mechanism passes and exploratory row observed; no visual/product decision; OPEN per `MISSING.md` |
| L06-08 cancellation terminal and timer preservation | L06-MODEL / DEVICE | L1; target scenario L2 | production model cancellation; `main/lab06_cancel_schedule.h`; FW harness | `test_lab06_cancel`; compiled-C elapsed-schedule regression; current scripted run op254 ended `cancelled`, hangout→hangout, timer count 1; manual transition op3 also cancelled and preserved one timer | L2 observed | Successful scripted and manual cancellation terminals on current source `8a121031…`; formal independent DEVICE qualification remains pending |
| L06-09 exact decided input set and verdict | L06-HOST | L1; target experiment L2 | `fixtures/scenarios.json`; parser experiment evaluation | Model fixture assertion; host exact-input and verdict tests | L1 | Pass as software contract; synthetic parser `PASS` is not a device result; no complete target verdict |

## Device evidence still required

The nine user-run manifests, event journals, metrics, and serial logs remain
preserved with their original outcomes. The latest scripted capture provides
an observed successful decided experiment on the current visible-guidance
source. The latest manual capture is exploratory and incomplete; its serial
trace records a successful cancellation terminal. Performance qualification
remains UNMEASURED, firmware trace loss remains UNKNOWN, and formal independent
DEVICE qualification remains pending. Device criteria not established by the
recorded scripted run include:

- stage entry/return/cancel behavior, transition and operation terminal counts,
  stale callback behavior, touch/control observations, and repeated cycles;
- `anim_us` and `build_us` p50/p95/p99/worst, in microseconds;
- LVGL pool high-water and largest free block, plus internal heap and PSRAM
  high-water/largest-free observations from `heap_caps`;
- the exact selected kit, build identity, effective config, and capture
  provenance needed to qualify L2/L3 claims.

Those measurements have no fabricated values or thresholds in this report. The
400 ms duration is the motion under test, not a timing pass gate. The 65536-byte
LVGL pool is a build configuration and planning default, not an approved budget.

## Reuse boundary for LAB-07

LAB-07 may reuse the verified LAB-06 single-writer model at
`client/labs/lab-06-stage-shell/components/lab06_model/` and fixed geometry
contract at `client/labs/lab-06-stage-shell/README.md`, with L06-02, L06-03, and
S-20 coverage documented above. This report exposes those existing paths for
reuse only; it makes no extraction, ownership change, new screen decision, or
product approval.
