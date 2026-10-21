# LAB-12/v1 — Profile settings propagation

## Visual phase

**Software verdict: FAIL.** The one fresh independent review found that the
screen links Montserrat-Medium rather than the foundation's required Montserrat
600. It also found the card hint outside the lower-pane inset; that position
was corrected from y=8 to y=12 and all four variants rebuilt, but the correction
has not received independent recheck. No target display/touch verdict is
claimed.

The visual phase implements an exploratory split-pane settings prototype. The
versioned fixture selects `split-pane` with the read-only LAB-06 L06-03 geometry
(upper 320×80, lower 320×160). There is no visual approval. `P-preview`,
`P-modal`, and `P-return` are the complete visual `experiment_inputs`. Their
model/parser checks cover cancel without success copy (`saved=0`), modal close
returning to `card-2` with zero overlay widgets, and return to the same static
three-card inbox with focus preserved and `update_level=1`.

Visual manual and scripted commands bind no server and start no listener on
8771. The scripted harness is a software trace, not a display or touch run.
The post-review screen recompose follows the 320×80 / 320×160 split pane:
Profile Settings uses a 24 px heading and 16 px instruction in the upper
content well. Pane containers have zero implicit padding. The lower content
well keeps its 12 px boundary; its one-line `Tap a card for details.` hint is
placed at y=12 within the lower pane. It uses LVGL's bundled Montserrat font,
generated from Montserrat-Medium rather than the foundation's specified weight
600; this remains an open standards finding. Lower-panel buttons have equal 16 px left and right
margins; the last row keeps 12 px bottom space. Touch targets are at least
136×48 px except the 101×48 px Back control. Settings cards stay inside the
lower pane. Back hides that child view and restores the existing three-card
inbox without recreating it.

The profile detail modal fits the 296×216 px content well. Accent and avatar
selection update a local preview tile; Save is not shown as acknowledged in the
visual variant, and Cancel restores the prior preview. Sketch stroke is edited
in a separate local control and does not change the profile preview accent.
Open/close deletes the modal subtree and its owned labels/buttons. The
post-review recompose addresses the earlier modal, touch-height, and pane
overflow findings, but the current code has not received a fresh independent
review. No target display or touch evidence is claimed. Side-by-side controls on card, profile,
and stroke detail panels use a 16 px gap and matching 12 px outer margins;
buttons remain 48 px high.

## Shared-profile phase

**Software verdict: PENDING independent recheck.** The fresh review found that
a stale conflict could be called a successful save when another endpoint wrote
the same field value. The result classifier now treats any refetched newer
revision as a conflict; the fixture regression asserts a stale request with a
matching field value remains 409. Focused tests, all four builds, and the final
full suite pass after this correction, but this post-review change has no fresh
independent verification. The latest user target run remains
workflow PASS / experiment FAIL.

The shared phase uses an owned, versioned synthetic HTTP fixture on port 8771.
The scripted harness uses the ESP Wi-Fi/`esp_http_client` implementation for
GET/PATCH. Parsed HTTP responses and canonical fixture state determine harness
results. The tests cover two endpoint edits at revision
7 (first accepted, second `revision_conflict`, first body unchanged), rejected
patch (`presented_saved=0`), invalid fixture-disallowed color (`invalid`),
dropped response with same-operation replay and one server side effect, delayed
competing updates, and refetch/reconciliation after the hint. A transport error
remains uncertain until fixture reconciliation. Profile updates are owned by
`lab12_profile_sync`: acknowledged profile, local preview, and immutable
in-flight update are distinct. Completion binds user, session generation,
operation ID, and expected/result revision; older revisions, other users or
sessions cannot replace acknowledged state, and an in-flight completion does
not erase a newer local preview.
Reusable entries are `lab12_profile_sync_preview`,
`lab12_profile_sync_begin`, `lab12_profile_sync_complete`,
`lab12_profile_sync_transport_uncertain`, and `lab12_profile_sync_reconcile`.
The visible shared Settings Save sends an immutable update through the same
`esp_http_client` fixture client used by the harness. A worker owns network I/O;
the LVGL timer applies parsed results only when session, user, and operation
still match. Successful writes require a parsed canonical profile and a newer
revision. A lost or malformed response retries the same operation ID/body once;
repeated uncertainty remains explicitly pending and Check status resubmits that
same operation. Conflict and repeated transport failure fetch canonical state.
Rejected writes roll back only the preview captured by that operation, so a
newer preview remains local. Cancel and Back are blocked while an operation is
unresolved. Focused model and fixture tests exercise this reusable reconciliation
path; a fresh independent reviewer is checking the integrated UI path. The
visual-only phase provides no shared-save proof.
For `P-invalid`, the scripted harness snapshots the canonical profile before
the invalid request and compares user, revision, avatar, and accent with the
post-rejection server response. The trace passes only when the profile is
unchanged and a valid fallback avatar remains available.

Shared decided inputs are exactly `P-save-fail`, `P-conflict`, `P-lost`,
`P-invalid`, and `P-hint`. Exploratory rows are retained with fixture-derived
observations and excluded from both verdict arrays: `P-save-policy` records
`reject_preview=rollback`; `P-offline` records `work=retained` with
`presented_saved=0`; `P-message-color` records `follow-profile` and
`frozen-on-message`; `P-layout` records `split-pane`. These observations are
not product choices. Message color, settings design/which settings, avatar
catalog/allowed palette, optimistic-save policy, and wire choices remain OPEN.
COM-OPEN-001–005 remain OPEN. No visual or product choice is approved here.

The user supplied scripted-shared target run
`20261021-scripted-shared-run-2bf19264` failed during startup with
`LAB12_RUNTIME_FAILED reason=fixture_transport`; it emitted no `LAB12_READY`
or harness traces, and the fixture log contains only the host `/health` probe.
The run is FAIL/UNMEASURED and provides no shared-profile device result. The
adapter had appended run-specific Wi-Fi/URL defaults but reused an existing
`sdkconfig` with empty LAB-12 connection settings. It now removes only that
variant's generated `sdkconfig` and `sdkconfig.old` when those settings do not
match its private override, forcing ESP-IDF to regenerate configuration. A
regression test covers changed and unchanged overrides; no device retry or
flash was performed after this software fix. The subsequent user-supplied run
`20261021-scripted-shared-run-ff894592` regenerated the connected firmware but
failed its app-size check: its 1,396,176-byte image exceeded the selected
1 MiB app partition by 347,600 bytes. LAB-12's existing 2 MiB `partitions.csv`
had not been selected; a 2 MiB app entry would also exceed the configured 2 MiB
flash once offsets were included. LAB-12 now selects its custom partition with
a 0x1E0000-byte factory app. Shared offline builds use nonfunctional
placeholders so Wi-Fi/HTTP code is linked without starting a server; run-time
builds replace them with private kit settings. All four variants now pass the
partition check; the largest scripted-shared image is 1,396,176 bytes with 29%
free. No device retry or flash was performed after these software fixes.
The later user-supplied target run `20261021-scripted-shared-run-3b74980d`
reached `LAB12_READY`, then reset with a FreeRTOS main-task stack overflow.
The captured trace mapped to `vApplicationStackOverflowHook`; the generated
sdkconfig used the 3,584-byte default. The scripted-shared defaults now set the
main task stack to 8,192 bytes, and a clean scripted-shared rebuild passes its
partition check. This repaired image has not been flashed or target-verified;
the original run remains FAIL/UNMEASURED. The visible shared Save lifecycle is
implemented in the current software candidate, while the visual phase gives no
shared-save proof. Update latency and
repeated open/preview/cancel/close resource retention need target measurements;
target responsiveness and display/touch behavior remain UNMEASURED.
The subsequent user-supplied run `20261021-scripted-shared-run-409224e4`
used the rebuilt image and completed workflow and parsing, but all five shared
scenarios failed. Its fixture PATCH accepted the first save, but the canonical
success response omitted `user_id`, which the firmware profile parser requires.
Reconciliation therefore retained a stale revision, causing later requests to
conflict; the lost-response path recorded zero new side effects. Successful and
idempotent PATCH responses now include the synthetic `user_id`, covered by a
focused response-contract regression test. The target run remains FAIL and has
not been rerun against the fixture fix. The visible shared Save path and
LAB-12-specific help output were corrected in this software repair; no new
target run was made.

## Identity and build evidence

- Lab/fixture: `LAB-12/v1`, `lab12-profile/v1`; firmware tree
  `client/labs/lab-12-profile-settings`; server tree
  `server/labs/lab-12-profile-settings`.
- Target/build environment: ESP32-S3; ESP-IDF v5.4.2; Xtensa GCC
  `esp-14.2.0_20241119`; CMake configured with `CCACHE_ENABLE=0`; isolated
  `build/labs/lab-12-profile-settings-<variant>`
  directories.
- Server fixture hash is bound into `/health` with run ID, fixture revision,
  and server version. Adapter run identity records source, sdkconfig, and binary
  hashes. `server.py` SHA-256 is
  `48e26b9bca690a18a1672970edefd486ff71b53d87877e92e48851359d7429ad` and the
  server fixture-set SHA-256 is
  `00bdc48a0ce4d947fcacfef688f45e34eaa45f1352ddc15424572466f9e2154e`.
  Synthetic fixture tokens are data only and are not credentials.
- Firmware source fingerprint SHA-256 is
  `e2944135ce53aef915b56bfc3b6d6f74927075557181b7eb33b689318708f85c` and the
  firmware fixture-set SHA-256 is
  `0c20f85097e7a2f2682e42d41de95c835496cbf00cb1591a553f713c16c786e4`.
  These generated build products are not checked in.

Commands run for qualification:

```text
.venv/bin/python -m unittest scripts.tests.test_lab12_model scripts.tests.test_lab12_host
.venv/bin/python scripts/lab_registry.py validate
make lab.help
make lab.lab12.build
make lab.lab12.build VARIANT=scripted
make lab.lab12.build VARIANT=shared
make lab.lab12.build VARIANT=scripted-shared
```

The two focused test modules passed (20 tests). Registry validation and both
general and LAB-12 help passed. After the fresh review, the conflict
classification and lower-pane inset were corrected; focused tests and all four
incremental builds pass. The final repository suite passed 441 tests in
94.884 seconds. Build
results and hashes are listed below. The initial independent
review ran repository-wide `make test` and reported 420 tests with 18 errors,
including the parser import collision and sandboxed loopback bind failures.
The initial independent reviewer ran `make test` and passed 438 tests. The
grouped repair's final suite passed 441 tests in 94.884s. The one user-authorized
fresh verifier found a false acknowledgement risk, an inset violation, missing
Montserrat 600, and stale source/test hashes. The false acknowledgement and
inset findings were corrected afterward, and the focused suite, all four
incremental builds, and full suite passed again. The current candidate has not
received an independent recheck; the 600-weight finding remains unresolved.
The latest target evidence remains the user-provided failed run above; no device
command or flash was performed by this agent, and no optional qualification
cycle was run.

| Variant | Build directory | Result |
|---|---|---|
| manual | `build/labs/lab-12-profile-settings-manual` | PASS; 710,688 bytes, 64% partition free; binary SHA-256 `d697efd219de7864480523495cc16fa43110a3bc77bb3653870c6416db815d3b`; sdkconfig SHA-256 `94cc2af7f8ec8dd25730ca12adc0650a3e4e3e83fbb497d7574d449a387d444a`. Scripted/shared flags are unset. |
| scripted | `build/labs/lab-12-profile-settings-scripted` | PASS; 1,020,784 bytes, 48% partition free; binary SHA-256 `40668d5b05199036a2bfb2bbaf1c43bd9c7b41a7ba2471cb0653f41baedd0a0d`; sdkconfig SHA-256 `4c1e2c59694f74815df391471f4fc1c46618f64f26ad36e8775ee981ef7f8dcb`. Scripted set; shared unset. |
| shared | `build/labs/lab-12-profile-settings-shared` | PASS; 1,396,432 bytes, 29% partition free; binary SHA-256 `ba269287821c380f29059803cd9e840526e00c30d498f9f199afff552d785410`; sdkconfig SHA-256 `50e8e3d452c862d6344a40faef4235fc12422f31593a40c1ce080cf8a7c87c62`. Scripted unset; shared set. |
| scripted-shared | `build/labs/lab-12-profile-settings-scripted-shared` | PASS; 1,399,872 bytes, 29% partition free; binary SHA-256 `f2c6de1106d28aec86b8be3b266b22947eaa7bf0977d8311697bc46c292a6ee4`; sdkconfig SHA-256 `d5f29cd71b4bf50c3dffaf04a00c867f4100a335d627e58148fcc580c43f17a5`. Both flags set; main task stack is 8,192 bytes. |

The app partition is 0x1E0000 bytes within the configured 2 MiB flash. This is
a build observation, not a performance or memory-budget pass; on-device
resource measurement remains open qualification work.

## Acceptance ledger

L0/L1 are the achievable software qualification level here. L2 requires an
authorized, post-reset kit capture; L3 requires target measurements and is not
claimed. A successful parser/workflow does not substitute for an experiment
verdict.

| ID | Result / evidence |
|---|---|
| L12-01 | L1: scripted default selects visual `scripted`, no server listener; manifest retains phase under `capture.experiment_phase` and drops the top-level key. Focused host tests. |
| L12-02 | L1: rejected fixture write is failure and leaves presented state unsaved. Native sync and parser/fixture tests. |
| L12-03 | L1: stale revision conflict does not overwrite the first endpoint body; server test and native sync revision rules. |
| L12-04 | L1: accent and sketch stroke color are separate fields; model test and UI controls. |
| L12-05 | L1: modal close returns the originating card ID; model/parser checks. |
| L12-06 | Pending L2: requires scripted visual kit run with post-reset serial and port 8771 absent. |
| L12-07 | Pending L2: requires scripted-shared kit run, matching health identity, post-reset serial, and owned-service cleanup. |
| L12-08 | L3 UNMEASURED: no device frame-time or memory samples. |
| L12-09 | L1: modal cancellation is terminal once, overlay count zero, saved zero; repeated model modal lifecycle test. |
| L12-10 | L1: exact visual/shared input arrays; four fixture probes retained as exploratory observations and excluded from verdict arrays. Parser tests. |
| S-01 | L0: prescribed four make build entry points; see build ledger. `make build` was not used. |
| S-02 | L1: versioned firmware manifest fields asserted by focused host coverage. |
| S-03 | L1: owned parser/schema; trace fixture covers phase-specific decided and exploratory rows. |
| S-04 | L1: registry validation and `make lab.help` passed; four variant keys are listed. |
| S-05 | L1: `make lab.lab12.help` states `BACKEND` is unsupported and shared variants own port 8771. |
| S-06 | L0: four distinct variant build directories and sdkconfig symbols; see build ledger. |
| S-07 | L1: reset capture accepts only post-reset ready/harness markers and fails closed on missing/mismatched markers. Not run on target. |
| S-08 | L1: native bounded model queue capacity 4 rejects overflow explicitly; model test. |
| S-09 | L1: sync/model terminal outcomes are single-operation and tested for cancel, reject, ack, uncertainty, and stale completion. |
| S-10 | L1: serial sanitizer bounds/redacts URLs, password assignments and bearer tokens, and rejects secret/media fields; fixture coverage. No raw values are recorded. |
| S-11 | L1: fixture and server manifests are versioned; host service test asserts fixture hash and health identity. No on-kit shared run. |
| S-12 | L1: visual variants record no server/service placeholder and bind no socket; explicit shared variants own only port 8771. |
| S-13 | L1: host/model evidence is labeled software-only; hardware timing/memory provenance remains unavailable. |
| S-14 | L1: adapter keeps workflow and experiment verdict separate; no device workflow evidence. |
| S-15 | L3 UNMEASURED: no performance budget is approved and no timed series was collected. |
| S-16 | L1: provisional `ResourceBudget` is declared in the project README; values are not hardware measurements. |
| S-17 | L1: harness stage transitions are owned by one adapter stage writer; native sync protects asynchronous completion ownership. |
| S-18 | L0: no changes under `client/device/` or `artifacts/device/bootstrap/`. |
| S-19 | L0: no archive paths changed. |
| S-20 | L1 report and index entry included. L2 unavailable. |
| S-21 | L1: `BACKEND=local` is rejected before host I/O; focused adapter test. |
| S-22 | L1: exploratory rows remain observable but outside decided verdict inputs; exact phase arrays tested. |

## Decision and verification levels

| Package | Verdict | Evidence/status |
|---|---|---|
| L12-MODEL | PASS | Fresh verifier passed the model and fixture layer; focused suite passes. |
| L12-FW | FAIL | Visual font uses Montserrat-Medium instead of required weight 600; shared conflict-classification repair awaits independent recheck. |
| L12-HOST | PASS | Registry validation, LAB-12 help, fixture/parser checks pass; visual/server boundary is documented. |
| L12-QUAL | PENDING | Builds and final full suite pass, but the latest shared target is FAIL and post-review software changes have no independent recheck. |

The one fresh verifier invocation authorized after the original cap has been
used. It returned FAIL on the then-current candidate; the conflict and inset
findings were repaired afterward, but the font-weight finding remains. No
further verifier invocation is authorized in this run. There is no product
decision.
UI design, wire contract, settings scope, profile palette/catalog,
rejected-preview policy, offline behavior, message-color behavior, and avatar
behavior remain OPEN. Device criteria L2 are pending an authorized capture; L3
performance is UNMEASURED.
