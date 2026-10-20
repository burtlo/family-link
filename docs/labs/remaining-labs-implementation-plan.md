# Remaining device labs — implementation plan

Status: **ready for scoped implementation orchestration, subject to the selected lab's prerequisite checks in section 8**. No measurement in this document is a lab result.
Date: **2026-10-19**.
Purpose: specify the device labs that still have to be built, on the host and firmware structure that already runs.

This plan covers **LAB-04**, the still-missing **LAB-05 resource-peak experiment**, **LAB-06**, **LAB-07**, **LAB-08**, **LAB-09**, **LAB-11**, **LAB-12**, and **LAB-13**.

**LAB-01 is excluded.** It is not future work, not a dependency, and not a baseline later labs wait for. Where an older lab note treats an integrated x02 run as a prerequisite, this plan replaces that comparison with a concrete control named in the lab section: an existing lab artifact, a fixed fixture, or an explicit no-baseline measurement.

**LAB-02, LAB-03, and LAB-10 are not re-planned.** They are the pattern. This plan states only the shared-structure obligations a new lab copies from them.

**Execution entry point:** select one lab using section 8. Its package IDs are distinct from the acceptance-criterion IDs in section 3. Default scope is software implementation and qualification; device execution is a separately selected package. Completing software does not mean the experiment has been measured.

## Authority

Owner decisions and root `docs/standards/` plus approved experience and contract text win over this plan and over the earlier lab plans. This plan does not close an OPEN product behavior, wire format, copy string, or screen.

When a lab scenario exercises an OPEN experience or contract item, the scenario is **exploratory**. An exploratory result may be recorded. It does not approve the open item.

Product questions come from `docs/labs/inbox-carousel-playback-evaluation.md` and `docs/labs/v2-experience-demo-roadmap.md`. Those documents propose experiments. They are not owner approval of the behaviors they ask labs to compare.

`docs/labs/reusable-lab-runner-impl.md` is an earlier gap analysis. Its package table is not the current tree. Where that map says a package failed or was not started, this plan does not treat the proposed design as shipped. The authority for shared execution is the current registry and the scripts named below.

## 1. Shared lab structure (as implemented)

### 1.1 Two registries

| Registry | File | Role |
|---|---|---|
| Product IDs | `docs/labs/README.md` | `LAB-NN` definitions. IDs in that table stop at `LAB-13`. |
| Host / make | `config/labs.registry.yaml` | Make names, project paths, variants, adapter key, capture defaults. Schema: `config/labs/labs.registry.schema.v1.json` (`family-link.labs-registry.schema/v1`, document `family-link.labs-registry/v1`). |

Registered today: `lab02` (`LAB-02`), `lab03` (`LAB-03`), `lab05` (`LAB-05`), `lab10` (`LAB-10`).

`scripts/lab_registry.py validate` checks repo-relative paths, that `project_dir` exists, that `variants.default` names a variant, that `trace_parser` exists when set, and that `run.instructions` or `run.instructions_doc` resolves. An unknown `adapter` fails that command only because `validate_document_schema` applies the JSON Schema `enum` (`generic`, `lab02`, `lab03`, `lab10`). `_parse_registry` stores the string and does not read `_ADAPTER_MODULES`. `resolve_adapter` raises `LabRegistryError` (`Unsupported adapter key`) when the key is missing from that map. `scripts/lab_registry.py dump` and `make config.labs` omit `adapter`; identity checks read `LabSpec.adapter_key` from `load_registry`. The validator does **not** today reject two entries that share `lab_id`, `project_dir`, or `evidence_slug`. L05-01 is the test that fails on the enum, the allowlist, and the missing uniqueness check, and passes after the edits in section 3.2. The root Makefile `lab.%` pattern already matches a name such as `lab05peak`. Registering the peak lab does not add a pattern rule.

### 1.2 Registry fields a new entry must set

| Field | Rule for a new lab |
|---|---|
| Make name | YAML key matching `^[a-z][a-z0-9]*$`. Becomes `make lab.<name>`. |
| `lab_id` | `LAB-NN` with `NN` from 04, 05, 06, 07, 08, 09, 11, 12, or 13. No new ID. |
| `evidence_slug` | `lab-NN` or `lab-NN-<suffix>` (`^lab-[0-9]{2}(-[a-z0-9-]+)?$`). |
| `project_dir` | Existing ESP-IDF tree under `client/labs/<slug>/` before the entry is committed to the registry. |
| `target` | `esp32s3`. |
| `adapter` | One key in `scripts/lab_adapter_registry.py` `_ADAPTER_MODULES`. Today: `generic`, `lab02`, `lab03`, `lab10`. A new key requires a schema `enum` edit and an allowlist entry. The YAML value is never a Python import path. |
| `variants` | `default` plus at least one build variant. Each variant has its own `build_dir` under `build/labs/`. `sdkconfig_defaults` paths are relative to `project_dir`. |
| `trace_parser` | Repo-relative `tools/parse_lab_trace.py` when the lab emits metrics. Omit only when the lab truly writes no metrics; the manifest must then keep `experiment_verdict` at `UNMEASURED`. |
| `run` | `instructions` and/or `instructions_doc`, plus `capture_seconds_default` and `flash_by_default`. |
| `workflow` | **Omit.** The schema object requires LAB-02's server path, fixture sizes `{8,16,64,256}`, and LAB-02 scenario names. LAB-03 and LAB-10 omit `workflow`. New labs do the same. Server ownership lives in the adapter. |

### 1.3 Make invocation

Root `Makefile` pattern rules dispatch to `scripts/lab_host.py`. Adding a lab does not add a `lab.%` rule.

| Target | Command | What the current runner does |
|---|---|---|
| List | `make lab.help` | `scripts/lab_host.py list` |
| Operator text | `make lab.<name>.help` | Registry `run.instructions` / `run.instructions_doc` |
| Manual run | `make lab.<name> KIT=<kit>` | Shared run. Flash follows `run.flash_by_default` unless `FLASH=0` or `FLASH=1`. |
| Scripted run | `make lab.<name>.scripted KIT=<kit>` | Passes `--mode scripted`. The generic adapter selects variant `scripted` when that key exists, otherwise `variants.default`. |
| Build | `make lab.<name>.build` | ESP-IDF build of the selected variant. Optional `VARIANT=`. |
| Scripted build | `make lab.<name>.scripted.build` | `scripts/lab_host.py build <name> --variant scripted`. `VARIANT=` is not forwarded. Another variant is `make lab.<name>.build VARIANT=<key>`. |
| Flash | `make lab.<name>.flash KIT=<kit>` | Flash one kit. Not `make flash`. |
| Capture only | `make lab.<name>.capture` | Serial capture. Does **not** write the v2 run manifest lifecycle. |
| Parse | `make lab.<name>.parse RUN_ID=<id>` | Lab parser over that run's serial log. |

Variables the pattern rules forward: `KIT`, `PORT`, `VARIANT` (not on `.scripted.build`), `SECONDS`, `FLASH`, `RUN_ID`. `BACKEND` is interpreted by the LAB-02 adapter. `scripts/lab_host.py` rejects a non-default backend only when `adapter.run` returns `None` and the host is about to fall through to the generic adapter. An adapter that returns an exit code must itself reject `args.backend` other than `managed-http` before it binds a port or starts capture. S-21 is that test.

Standalone fixture listeners exist only as explicit rules: `make lab.lab02.server` (port **8765**) and `make lab.lab03.server` (port **8766**). There is no `lab.%.server` pattern. A new lab that needs a detached listener adds one explicit Makefile rule and a distinct port. Shared runs still own the process themselves; the detached rule is for operator debugging, matching LAB-03.

Do not build or flash a lab with `make build` or `make flash`. Those targets publish and write the product bootstrap under `client/device` (`docs/standards/device-build-flash.md`). Do not point `[firmware].source` at a lab tree.

Validate after a registry edit:

```text
.venv/bin/python scripts/lab_registry.py validate
make config.labs
make lab.help
make test
```

`make config.labs` prints `family-link.labs-registry.snapshot/v1`. Optional `[command.lab.<name>]` sections in `config/host.defaults.ini` catalog the command for `make config.host`. Make works without them (`docs/standards/project-configuration.md`, `docs/standards/labs-configuration.md`).

### 1.4 Variant and sdkconfig

ESP-IDF applies `sdkconfig.defaults` when `sdkconfig` is created. `scripts/lab_host.py` deletes a stale `build_dir/sdkconfig` when the variant's defaults are not reflected, so a scripted build can pick up its flag.

As built, scripted mode is **not** one shared Kconfig symbol:

| Lab | Scripted flag | Where it is set |
|---|---|---|
| LAB-02 | `CONFIG_LAB_AUTO_HARNESS` | Copied into `client/labs/lab-02-bounded-inbox/components/lab02/Kconfig.projbuild`. Host file `client/labs/kconfig/sdkconfig.defaults.scripted` sets `CONFIG_LAB_AUTO_HARNESS=y`. The comment in `client/labs/kconfig/lab_common.projbuild` says ESP-IDF only loads Kconfig under component directories, so the symbol is copied, not `source`d. |
| LAB-03 | `CONFIG_LAB03_SCRIPTED` | `client/labs/lab-03-media-playback/sdkconfig.defaults.scripted` |
| LAB-05 lifecycle | `CONFIG_LAB05_SCRIPTED` | `client/labs/lab-05-transition-proof/sdkconfig.defaults.scripted` |
| LAB-10 | `CONFIG_LAB10_SCRIPTED` | `client/labs/lab-10-record-canvas/sdkconfig.defaults.scripted` |

A new lab copies the menu text if it wants `LAB_AUTO_HARNESS`, or it defines `CONFIG_LABNN_SCRIPTED` in its own component `Kconfig.projbuild` and sets that symbol only in its `scripted` sdkconfig overlay. It must not reuse another lab's build directory.

### 1.5 Adapter allowlist and runner lifecycle

`scripts/lab_adapter_registry.py` `resolve_adapter` imports only `_ADAPTER_MODULES`. Unknown keys raise `LabRegistryError`.

`scripts/lab_runner_adapter.py` (`generic`) calls `run_with_adapter` **without** `execute`. That path records service and scenario as `not_configured` when the hooks are absent, and `evidence.finalize` hard-codes `experiment_verdict="UNMEASURED"`. `make lab.lab05` uses this adapter. A lab that must classify an experiment cannot use `generic`.

The generic adapter selects the variant with `args.variant or (mode if mode in spec.variants else spec.default_variant)`. A new adapter copies that expression. With `mode=scripted` and `VARIANT` unset, the selected key is `scripted` when that key exists. S-21 asserts this for each new adapter. For `lab05peak`, that selection is variant `scripted` (`control=persistent`), not `scripted-replace`.

`scripts/lab_runner.py` `run_with_adapter`:

1. Allocates the evidence directory **before** preflight (`EvidenceRun` in `scripts/lab_run_evidence.py`).
2. Run id is `YYYYMMDD-<variant>-run-<8 hex>` unless `RUN_ID=` is set (`new_run_id`).
3. If the adapter passes `execute`, the adapter owns stages and returns `workflow_status`, `experiment_verdict`, `caller_completed`, `cleanup_succeeded`, and `terminal_reason`. Workflow `PASS` is rejected unless `caller_completed` and `cleanup_succeeded` are both true. Cleanup failure forces workflow `FAIL`.
4. If `execute` is omitted, the engine runs preflight, optional `hooks.server`, build, capture, optional `hooks.scenario`, then cleanup. Missing server and scenario hooks emit `stage=not_configured`. The experiment verdict is always `UNMEASURED`.

LAB-02's adapter (`scripts/lab_adapter_lab02.py`) and LAB-03's adapter (`scripts/lab_adapter_lab03.py`) use `execute`. LAB-02's managed server is `scripts/lab02_server.py`, which writes logs through `scripts/server_log.py` `BoundedRedactingLog` (default 64 KiB, per-line cap, URL and credential redaction). LAB-03 does **not** call that helper: it spawns `server/labs/lab-03-media/server.py` on port 8766, drains stdout itself with a 2 MiB cap, and health-checks `http://127.0.0.1:8766/health` for `run_id`, `fixture_sha256`, `fixture_version`, and `server_version` within 10 seconds.

LAB-02 registry deadlines, used only by that lab's workflow object, are startup 10 s, health 10 s, seed 10 s, readiness 15 s, action 5 s, cleanup 5 s. New adapters copy those integers as **named host deadlines in adapter code**, not as a `workflow:` block. They are operational limits for the fixture process, not product timeouts.

Fresh boot marker: LAB-02 and LAB-03 accept a ready line only after capture is armed and the kit is reset. A marker already in the buffer before reset does not count. A missing success marker leaves device evidence incomplete and must not produce workflow `PASS`.

Cleanup: the runner stops only resources the adapter started. Cleanup failure fails the workflow and still finalizes a partial manifest.

### 1.6 Evidence layout, documents, and verdicts

| Artifact | Path |
|---|---|
| Run directory | `docs/labs/evidence/<evidence_slug>/runs/<run_id>/` |
| Manifest | `run-manifest.json`, document `family-link.lab-run-manifest/v2` (`docs/labs/schemas/run-manifest.v2.md`) |
| Host journal | `events.jsonl`. Each row has `source`: `host`, `server`, `device`, or `unknown`. |
| Metrics | `metrics.json`, document `family-link.lab-metrics/v2` (`docs/labs/schemas/metrics.v2.md`), when a parser runs |
| Serial | `logs/device/labs/<evidence_slug>/<run_id>/serial.txt` |
| Owned server log | `<run_id>/server.log` inside the evidence run directory, attached only if the file is a real file under the run directory |
| Report | `docs/labs/evidence/<evidence_slug>/REPORT-<LAB-ID>-<rev>.md` |

`firmware.build_fingerprint` covers compiled inputs only. It must not include `run_id` or kit nickname. `run_id` is a separate top-level field.

`workflow_status` is `PASS`, `FAIL`, or `INCOMPLETE`. `experiment_verdict` is `PASS`, `FAIL`, `UNMEASURED`, or `INCOMPLETE`. They are independent. Parser success alone cannot set either one. Missing measurements stay missing or `null`; they are not zero. `event_loss.firmware_trace.status` stays `UNKNOWN` with a null count when the device trace does not report loss. Zero host-journal drops do not imply zero device loss.

**What workflow `PASS` is allowed to mean.** Configured host stages finished, the caller marked completion, and cleanup succeeded (`docs/standards/labs-configuration.md`). For a scripted adapter that this plan adds, workflow `PASS` also requires that adapter's harness-complete marker in the post-reset serial log. Workflow `PASS` is not an experiment result and is not a performance result.

**What experiment `PASS` is allowed to mean.** Every functional criterion for that scenario that comes from an already decided rule (coding standard or owner-decided experience) held, and required evidence for those criteria is `COMPLETE`. It is not approval of an OPEN product choice. It is not a performance pass. Where no product budget exists, the metrics record p50, p95, p99, and worst, plus largest free block, and the experiment verdict for the performance claim stays `UNMEASURED`.

**Run-manifest keys the writer keeps.** `EvidenceRun._normalize_context` drops top-level keys that are not in `_SAFE_FIELDS` in `scripts/lab_run_evidence.py`. That set is `lab_id`, `lab_revision`, `lab_version_string`, `evidence_slug`, `verification_levels`, `firmware`, `server`, `fixture`, `hardware`, `scenarios`, `comparison_run_id`, `operator`, `source`, `build`, `capture`, `workflow_status`, `experiment_verdict`, `terminal_reason`, `artifacts`, and `event_loss`. A new top-level key does not appear in `run-manifest.json`. Lab-specific facts go in a kept object: `capture` for phase and variant, `fixture` for schema and sha256, `server` for path, revision, and source sha256. `metrics.json` is a separate file. Its `lab_metrics` object is not filtered by `_SAFE_FIELDS`.

**Exploratory marker.** For every scenario this plan calls exploratory, `metrics.json` field `lab_metrics.scenarios.<scenario_id>.class` is the string `exploratory`, and `lab_metrics.scenarios.<scenario_id>.observation` is an object with `terminal` and the measured fields for that scenario. `lab_metrics.experiment_inputs` is an array of scenario ids whose `class` is `decided`. The adapter sets `experiment_verdict` only from `experiment_inputs`. An exploratory id is absent from that array. The report copies `observation` and the OPEN id. S-22 is the host test. Workflow `PASS` does not write `experiment_inputs` and does not satisfy an experiment criterion.

Logical version string, in the lab README and the report:

```text
LAB-07/v1 = firmware <build-id> + server <revision> + fixture <revision>
```

Use `server none` when there is no server. Changing fixture bytes, server timing, or the trace schema increments the revision. Two different triples must not share a version label.

### 1.7 Firmware tree, manifest, parser

Per `docs/labs/repository-layout.md`, each new lab is one ESP-IDF project:

```text
client/labs/<slug>/
├── README.md
├── manifest.lab.json          # document family-link.lab-manifest/v1
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.scripted
├── partitions.csv
├── main/
├── components/<lab>_*/
├── fixtures/                  # when data is embedded or hashed
├── tools/parse_lab_trace.py
└── tests/
```

`manifest.lab.json` minimum fields: `document`, `lab_id`, `lab_revision`, `firmware_path`, `server_path` (`none` or a repo-relative directory), `fixture_schema`, `fixture_revision`, `protocol`, `trace_schema`. LAB-03's manifest omits `document`; new labs include it, as LAB-02, LAB-05, and LAB-10 do.

The lab README names the make name, variants, boot marker, trace fields, and known limits. The trace schema file lives next to the firmware (`trace-schema.v1.json` or the parser's documented field list) and is referenced by `trace_schema` in the manifest.

Parser ownership stays in the lab tree. The adapter invokes it and writes `metrics.json`. `make lab.<name>.parse` must call that same script. There is no shared percentile library. A lab that reports percentiles implements nearest-rank itself: sort finite samples, one-based rank `ceil(p * n / 100)` clamped to `[1, n]`, and emit p50, p95, p99, and max only from real samples. An empty series is `null` with `sample_count` 0 and a `missing_reason`. Do not import `client/labs/lab-10-record-canvas/tools/aggregate_metrics.py`; its field names belong to LAB-10.

Serial lines use a lab prefix (`LAB04_TRACE`, and so on) with monotonic time, and `session_id`, `message_id` or `user_id`, `operation_id`, and binding `generation` where the coding standard's operation envelope applies (`docs/labs/assistant-guide.md`). No PIN, credential, or raw media payload.

**Operation counting.** `terminal_count=1` always means per accepted `operation_id`, not one terminal for an entire scenario containing repeated or concurrent operations. Use a distinct id per repetition, operation kind, or phase. A scenario observation includes its operation results and the expected repetition count; missing repetitions, duplicate ids, and extra terminals fail its completeness check. This applies in particular to LAB-05's 20 overlaps, LAB-06's stage cycles, LAB-11's four cancel phases, LAB-12's two-endpoint conflict, and LAB-13's composed journeys. Expected negative terminals (`failure`, `stale`, `cancelled`, `timed_out`) satisfy a decided scenario only when they match that scenario's expected fields; they are not automatically experiment failures.

**Verdict completeness.** The adapter evaluates the exact phase-specific `experiment_inputs` set below. Missing decided scenarios, missing operation results, malformed traces, or lost evidence cannot yield experiment `PASS`. An observed violation yields `FAIL`; absent or unusable required evidence yields `INCOMPLETE` (or `UNMEASURED` when no decided experiment was measured). Test fixtures can prove this evaluation logic at L1; they are never saved as device runs. Each report separates functional experiment results from performance observations whose budget remains OPEN.

### 1.8 Server fixture when a server exists

Follow the LAB-03 pair, not a new framework:

| Obligation | As built for LAB-03, required for a new server lab |
|---|---|
| Tree | `server/labs/<slug>/server.py`, `manifest.lab.json`, `fixtures/` with a manifest of byte lengths and SHA-256 |
| Identity | Health JSON echoes `run_id`, fixture version, fixture SHA-256, and server version |
| Version | Server source SHA-256 and fixture manifest SHA-256 land in the run manifest |
| Data | Synthetic fixtures only. No deployment roster, household archive, or live account |
| Process | Adapter spawns it, health-checks it, seeds it, and stops it. Detached `make lab.<name>.server` is optional and must not be required for `make lab.<name>.scripted` |
| Logs | `BoundedRedactingLog` from `scripts/server_log.py` (the helper LAB-02 already uses). Do not copy LAB-03's private drain |
| Ports | 8765 and 8766 are taken. Assignments are in each lab section |

Wi-Fi on the device, when needed, is a lab-local copy. LAB-03 already copied the LAB-02 station lifecycle into a LAB-03 namespace because importing LAB-02's session-coupled components would create a second session owner. New networked labs copy that lifecycle again under their own namespace. They do not edit LAB-02 or LAB-03 sources, and they do not promote the copy into `client/shared/` in the same change.

### 1.9 What is shared, what is still copied

**Call these. They exist.**

| Helper | Callers |
|---|---|
| `run_with_adapter` in `scripts/lab_runner.py` | Every new adapter. Pass `execute` when the experiment verdict is not the generic hard-coded `UNMEASURED`. |
| `EvidenceRun` in `scripts/lab_run_evidence.py` | Only through `run_with_adapter`. Adapters update context and append events; they do not invent a second manifest writer. |
| `resolve_adapter` in `scripts/lab_adapter_registry.py` | Registry key only. |
| `BoundedRedactingLog` and `redact_line` in `scripts/server_log.py` | Every new owned server. |
| Makefile `lab.%` patterns and `scripts/lab_host.py` | Operator commands. |
| `scripts/lab_registry.py` | Validation. |

**Still duplicated, because no helper exists. This plan does not add a framework to remove the duplication.**

| Behavior | Why it stays per lab |
|---|---|
| Adapter module `scripts/lab_adapter_labNN.py` | Experiment markers, scenario lists, and verdict rules differ. `generic` cannot classify them. |
| `tools/parse_lab_trace.py` and trace schema | Metric names are lab-specific (`metrics.v2` does not prescribe them). |
| `CONFIG_LABNN_SCRIPTED` | Only LAB-02 uses `LAB_AUTO_HARNESS`, and it keeps a private copy of the menu. |
| Server process supervision | LAB-02's `lab02_server.py` speaks LAB-02 HTTP. LAB-03 inlines a different health check. A generic supervisor would be a new framework. |
| Wi-Fi lifecycle | No promoted `client/shared/` network module. |
| Percentile reduction | LAB-10's aggregator is package-local. |

**Shared-helper package:** none. Section 4 gives the suggested lab order; section 8 gives the bounded packages within each lab. New labs call the helpers in the table above and keep the duplicated pieces lab-local.

### 1.10 Review and verification ladder

Coding standard `docs/standards/client-application-coding-standards.md`:

| Level | Meaning in a lab |
|---|---|
| L0 | `make lab.<name>.build` (and `.scripted.build`) succeeds for the ESP-IDF `v5.4.2` pin in `docs/standards/device-build-flash.md`. |
| L1 | Host or native tests under `client/labs/<slug>/tests/` and `scripts/tests/` cover transitions, terminal outcomes, queue overflow, and parser honesty. A virtual clock may compress a 60-second or five-minute schedule here. |
| L2 | `make lab.<name>.scripted KIT=<kit>` reproduces the path on target and saves the v2 manifest, events, and serial log. |
| L3 | The specific claim (pixels, audio, touch, heap, stack, scanout) is confirmed with device trace plus the required screenshot or synchronized recording. |

Build success is only L0. Timing, memory, input, and lifecycle claims need L3 evidence or they stay `UNMEASURED`. Host tests must not be labeled hardware.

**Reuse verification evidence by impact.** Keep the separate per-variant build directories and do not clean them for routine edits. The first required build for each prescribed variant must run through its designated entry point; HOST must still build every registered variant through `make` to verify registry and command wiring. Later verifiers may reuse a successful build when the exact compiled firmware inputs, variant, effective `sdkconfig`, ESP-IDF/toolchain identity, and resulting artifact hash match the saved build evidence. Rebuild the affected variant if any of those inputs changed or the evidence cannot be checked. Host parser, adapter, report, and documentation changes alone do not invalidate firmware build evidence. This rule reuses evidence; it does not weaken any package's source, configuration, or artifact checks.

**Use focused checks before final qualification.** MODEL, FW, and HOST run their package-specific native, host, parser, integration, and build checks. QUAL runs the full `make test` suite once against the final software state, after standards fixes and other repairs. If a later change affects code or checks covered by the suite, rerun it before recording QUAL pass. Do not repeat the full suite at earlier package boundaries when their focused checks cover the changed behavior.

Before the first L3 capture of a new tree, run the client review workflow in `docs/standards/client-code-review-agent.md` against `client/labs/<slug>/`. A review does not approve OPEN product choices and does not expand the target.

UI-heavy reports include the measurement table from `docs/product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md`. `docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md` requires two ledgers (LVGL pool, and pixel/audio/transient buffers) and states that **no firmware budget numbers are approved until measured on target**. The 64 KiB pool figure there is a v2 planning default (`CONFIG_LV_MEM_SIZE` 65536), not a pass threshold. Labs start from that default, record high-water and largest free block on both ledgers, and do not declare a performance `PASS` by comparing against it. Changing the pool size needs a measured row and owner sign-off; a lab must not treat a larger pool as approved.

## 2. Shared requirements for every new lab

Each row is a gate. "New lab" includes the LAB-05 peak registration (`lab05peak`) and excludes the existing lifecycle registration (`lab05`), which stays as it is.

| ID | Requirement | Level | Check |
|---|---|---|---|
| S-01 | ESP-IDF project under `client/labs/<slug>/` with `CMakeLists.txt`, `sdkconfig.defaults`, and a component `Kconfig.projbuild` | L0 | `make lab.<name>.build` exits 0. `make build` is not the command that produced the binary. |
| S-02 | `manifest.lab.json` document `family-link.lab-manifest/v1` with the minimum fields in section 1.7 | L1 | A host test loads the JSON and asserts `document`, `lab_id`, `firmware_path`, `server_path`, `fixture_schema`, `fixture_revision`, `protocol`, and `trace_schema`. |
| S-03 | Trace schema file referenced by the manifest, parser owned by that project | L1 | `trace_parser` in the registry points at `client/labs/<slug>/tools/parse_lab_trace.py`, except `lab05peak`, which uses section 3.2's `tools/parse_lab05_peak.py`. The test feeds a fixture serial snippet and asserts `metrics.json` `document` is `family-link.lab-metrics/v2` and `run_id` matches. |
| S-04 | Registry entry after the project directory exists | L1 | `.venv/bin/python scripts/lab_registry.py validate` exits 0. `make lab.help` prints the make name, `lab_id`, and variant keys. |
| S-05 | Operator help | L1 | `make lab.<name>.help` prints the registry instructions and the default capture seconds. |
| S-06 | Manual and scripted variants with distinct `build_dir`s | L0 | `make lab.<name>.build` and `make lab.<name>.scripted.build` write different directories under `build/labs/`. Scripted sdkconfig contains that lab's scripted symbol; manual sdkconfig does not. |
| S-07 | Fail-closed boot marker | L1 and L2 | Firmware prints `<MARKER> status=display_start_failed` (or the lab's equivalent failure status) and no success marker when display or required init fails. The adapter accepts a success marker only from bytes captured after the armed reset. A unit test of the adapter rejects a pre-reset success line. Missing success marker yields workflow `FAIL` or `INCOMPLETE` and experiment `UNMEASURED`. |
| S-08 | Bounded queues and explicit overflow | L1 | The lab README names each queue's capacity. A native test fills the queue, asserts an overflow trace or explicit rejection, asserts the count is preserved, and asserts no silent drop (`client-application-coding-standards.md` sections 2 and 3). |
| S-09 | Exactly one terminal outcome per accepted operation | L1 | Native tests assert one of `success`, `failure`, `cancelled`, `stale`, or `timed_out`, and reject a second terminal for the same operation id (`client-application-coding-standards.md` section 4). |
| S-10 | No secrets in evidence | L1 | A host test runs `redact_line` against a fixture line containing a URL, a `password=` assignment, and a `Bearer` token, and asserts the redacted forms. Parser tests reject a serial fixture that contains a raw PIN or media payload. Evidence files under `docs/labs/evidence/<slug>/` contain none of those. |
| S-11 | Server fixture versioning, when `server_path` is not `none` | L1 and L2 | Server `manifest.lab.json` names `lab_revision`. Health JSON matches `run_id`, fixture version, and fixture SHA-256. The run manifest records server source SHA-256 and fixture SHA-256. A byte change to the fixture without a revision bump fails the host identity test. |
| S-12 | Labs without a server omit `workflow` and record `service` and `scenario` as `not_configured` unless the adapter's `execute` path intentionally records a local scenario stage | L2 | Manifest JSON for a no-server run has no placeholder bind host or port. |
| S-13 | Host-verified claims stay host-verified | L1 versus L3 | Metrics fields that require heap, stack, scanout, touch, or audio carry `provenance`. Host-only runs use `host` or `unmeasured/blocked`, never `hardware`. The report's verification section lists L0–L3 per scenario. |
| S-14 | Workflow and experiment are separate fields | L2 | `run-manifest.json` contains both `workflow_status` and `experiment_verdict`. A test fixture with a successful parser and a failed cleanup asserts workflow `FAIL`. |
| S-15 | Performance numbers are recorded, not promoted | L3 | For every timed series the plan asks for, `lab_metrics` has p50, p95, p99, and worst in a named unit, plus largest free block in bytes for internal heap, PSRAM, and the LVGL pool. Experiment verdict for that performance claim is `UNMEASURED` unless a later owner-approved budget exists. This plan creates no such budget. |
| S-16 | Resource envelope declared before the measured run | L1 | The lab README contains a `ResourceBudget` with `max_live_nodes`, `max_transient_bytes`, `max_persistent_bytes`, `max_concurrent_operations`, `max_stack_or_call_depth`, and `maximum_input_size` (`client-application-coding-standards.md` section 8). The report labels those numbers **provisional lab probe**. |
| S-17 | Single session writer, when the lab has stages | L1 | One component applies stage transitions. A test shows a background result cannot replace the stage or clear partial input (`client-application-coding-standards.md` section 3). |
| S-18 | Product bootstrap untouched | L0 | The change set does not alter `client/device/` or `artifacts/device/bootstrap/`. |
| S-19 | Archive untouched | L0 | The change set contains no path under the v1 POC archive. |
| S-20 | Evidence report | L2 | `docs/labs/evidence/<evidence_slug>/REPORT-*.md` states lab version, target, measured, trigger, decision or **OPEN**, and L0–L3. `docs/labs/evidence/README.md` gains an index row. |
| S-21 | Scripted variant and `BACKEND` | L1 | Host test calls the new adapter with `mode=scripted` and `variant` unset and asserts the selected variant key is `scripted`. The same test calls `Adapter.run` with `backend=local` and asserts the result is non-zero or `ProjectConfigError`, and that no TCP port was bound. `workflow_status` is not `PASS`. |
| S-22 | Exploratory rows stay out of the experiment verdict | L1 | Host test writes one decided scenario with a failing terminal and one exploratory scenario with `observation.terminal=success`. `experiment_verdict` is `FAIL`. The exploratory object remains. A second fixture with decided success and exploratory failure does not set `experiment_verdict` to `FAIL` because of the exploratory id. An empty `experiment_inputs` array yields `experiment_verdict` `UNMEASURED`. |

## 3. Per-lab plans

### 3.1 LAB-04 — color sketch format and preview strategies

**Product question.** For one fixed set of drawings, how do timed colored points, timed stroke records with color per stroke, and a segment stream plus a small final-image preview compare in encoded bytes, decode and raster time, memory, malformed-input behavior, and legacy uncolored-point handling? If resume or seek is attempted, how do sparse checkpoints compare with replay-to-position?

**Non-goals.** Choosing the product wire format. Changing LAB-03 playback. Approving seek, cache, or checkpoint policy. Implementing send, inbox paging, or a recording timer.

**Decided inputs.**

| Input | Source |
|---|---|
| Logical sketch size 120×120 | `docs/product/experiences/sketch-record-and-playback.md` SK-002, EXP-SKP-001, EXP-SKP-002 |
| Record mapping 240×240 display to 120×120 at 2×, only in the capture portion of this lab | SK-004, EXP-SKP-003 |
| Six rail colors exist; per-color wire RGB565 is not decided | SK-007 / EXP-SKP-004 versus OPEN-SKP-01 |
| Legacy point records have no per-point color | Experience section "Baseline" and OPEN-SKP-03. This lab calls that encoding **legacy uncolored points**. It does not link into the v1 POC archive. |
| One 120×120 RGB565 image is 28,800 bytes | Arithmetic from the decided size (120 × 120 × 2). LAB-03's report uses the same figure as a measured resident size, not as a product cap. |

**Exploratory.** Scenario ids `F-points`, `F-strokes`, `F-preview`, `F-checkpoint`, and `F-legacy`. Each sets `lab_metrics.scenarios.<id>.class` to `exploratory` and is absent from `lab_metrics.experiment_inputs`. `observation` still records `terminal` and, for the three encodings, integer `encoded_bytes`. `OPEN-SKP-03` and message-delivery `COM-OPEN-002` stay open. The report copies those observations and does not contain a `selected_format` key in `metrics.json`. Decided experiment inputs are only `F-malformed`, `F-overflow`, and `F-cancel` (coding standard: bounded input, one terminal, release on cancel). L04-02 checks encoder round-trip and does not set `experiment_verdict`.

**Comparison.** Same drawings encoded three ways, plus a checkpoint variant. Corpus A is the existing LAB-03 fixture sketches addressed by `server/labs/lab-03-media/fixtures/manifest.json` (`fixture_version` `lab03-media/v1`): `short.sketch` SHA-256 `d2efa7e48bcb2eb021211d208acd231a05919edfb00d783dd9221720771d12ec` (128 bytes) and `dense.sketch` SHA-256 `1bac193a2482c8a4f743d1bf6ada17080c87ca2d58f7964fb20d481664c50985` (40,968 bytes). Those files are playback input, not a color-format specification. Corpus B is a lab-local drawing that adds background-colored strokes, because the LAB-03 report states the v1 fixture does not include them. No x02 baseline. If a drawing cannot be represented in a variant, the metrics record `unsupported` and the variant fails that scenario; the lab does not drop the strokes.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-04-sketch-format/` |
| Server | `none`. Encoders and decoders are on-device or host-side L1 tools. No paired server. |
| Registry name | `lab04` |
| Adapter | `lab04` (new allowlist key) |
| Evidence | `lab-04` |
| Make | `make lab.lab04`, `make lab.lab04.scripted`, `make lab.lab04.build`, `make lab.lab04.parse` |
| Variants | `manual` (default): operator steps through variants on device. `scripted`: harness encodes, decodes, seeks, and faults every variant. |
| Boot marker | `LAB04_READY build=<id> mode=<manual\|scripted> server=none trace_schema=lab04-trace/v1` |
| Harness marker | `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=<0\|1>` |
| Trace schema | `lab04-trace/v1` |

**Depends on.** The existing file `server/labs/lab-03-media/fixtures/manifest.json`, fields `assets.short.sketch` and `assets.dense.sketch`. L04-11 is the check. The dependency is not a LAB-03 performance verdict (the published report's performance verdict is `INCOMPLETE`). No dependency on LAB-05, LAB-06, or an integrated shell.

**Implementation outline.**

| Piece | Path |
|---|---|
| Firmware | `client/labs/lab-04-sketch-format/` with a format component (encode, decode, validate) and a 120×120 raster component. Capture portion maps a 240×240 touch into logical points only for the local generator; it does not become a record product screen. |
| Fixture | `client/labs/lab-04-sketch-format/fixtures/` copies hashes of corpus A and adds corpus B. `fixtures/manifest.json` records point counts, duration, and SHA-256 per variant output. |
| Parser | `tools/parse_lab_trace.py` |
| Adapter | `scripts/lab_adapter_lab04.py` calls `run_with_adapter(..., execute=...)`. No server hook. |
| Report | `docs/labs/evidence/lab-04/REPORT-LAB-04-v1.md` |

Raster work per audio interval uses a **provisional lab cap of 64 segments per update**, the cap LAB-03 declared before its run. Exceeding it is recorded. It is not a product fail.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| F-points | Corpus A and B as timed colored points | `success` if decode reproduces logical points, colors, and order | L1 model test plus scripted `LAB04_TRACE` rows `variant=points` |
| F-strokes | Same drawings as color-per-stroke segments | `success` under the same equality | `variant=strokes` |
| F-preview | Segment stream plus one 120×120 preview image | `success` if preview bytes are counted and the image is one 28,800-byte buffer, not a second full-LCD buffer | `preview_bytes` and `preview_buffers` in metrics |
| F-checkpoint | Early, middle, and late seek on the dense drawing | `success` or `unsupported` per variant. Time and extra bytes recorded. | `seek_us` p50/p95/p99/worst |
| F-malformed | Truncated, oversized (above the variant's declared max), and wrong color count | `failure` with a named reason. No partial accept that changes geometry. | L1 test `test_lab04_malformed` |
| F-legacy | Legacy uncolored points | `success` only as an explicit compatibility path that does not invent colors; otherwise `failure` `unsupported_legacy` | Trace `legacy=1` |
| F-overflow | Encode queue filled | Explicit overflow, one terminal `failure` | S-08 |
| F-cancel | Cancel mid-decode | Exactly one `cancelled`. `live_buffers=0` | L04-10 |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L04-01 | L0 | `make lab.lab04.build` and `make lab.lab04.scripted.build` | Exit 0. Distinct build dirs. |
| L04-02 | L1 | `test_lab04_roundtrip` | All three variants reproduce corpus B points, order, and pen-up boundaries. The test does not write `experiment_verdict`. |
| L04-03 | L1 | `test_lab04_preview_bytes` | Preview storage is ≤ 28,800 bytes for one image. Source: decided 120×120 RGB565 size, not a performance budget. This bound is not an experiment `PASS` of a preview strategy (`F-preview` stays exploratory). |
| L04-04 | L1 | `test_lab04_no_silent_truncate` | A drawing past `maximum_input_size` returns `failure`, not a shorter successful sketch. |
| L04-05 | L2 | Scripted run `make lab.lab04.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `docs/labs/evidence/lab-04/runs/<run_id>/run-manifest.json` and `logs/device/labs/lab-04/<run_id>/serial.txt`. `lab_id` is `LAB-04`. `workflow_status` is `PASS` only when post-reset serial contains `LAB04_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L04-06 | L3 | Metrics series `encode_us`, `decode_us`, `raster_us`, `seek_us` | Each has p50, p95, p99, worst in microseconds, `provenance=hardware` only on a device run. Performance verdict `UNMEASURED`. Source: no approved frame budget (`FOUNDATION-LVGL-BUDGET`). |
| L04-07 | L3 | Largest free block at the simultaneous peak of decode plus the live 28,800-byte image | Recorded in bytes for internal, PSRAM, and LVGL. No performance `PASS`. |
| L04-08 | L1 | `test_lab04_open_decision` | `REPORT-LAB-04-v1.md` contains the lines `OPEN-SKP-03: OPEN` and `COM-OPEN-002: OPEN`. `metrics.json` has no key `selected_format`. |
| L04-09 | L1 | `test_lab04_experiment_inputs` | `lab_metrics.experiment_inputs` equals `["F-malformed","F-overflow","F-cancel"]`. `experiment_verdict` is `PASS` only when each of those ids has `terminal_count` 1 and the terminal named in the scenario table. `F-points`, `F-strokes`, `F-preview`, `F-checkpoint`, and `F-legacy` have `class` `exploratory`, stay out of `experiment_inputs`, and still have `observation`. Workflow `PASS` with a failing decided terminal yields `experiment_verdict` `FAIL`. |
| L04-10 | L1 | `test_lab04_cancel` | One `operation_id` has `terminal` `cancelled`, `terminal_count` 1, and `live_buffers` 0. A second terminal for that id fails the test. |
| L04-11 | L1 | `test_lab04_corpus_hashes` | `server/labs/lab-03-media/fixtures/manifest.json` `assets.short.sketch.sha256` equals `d2efa7e48bcb2eb021211d208acd231a05919edfb00d783dd9221720771d12ec` and `bytes` is 128. `assets.dense.sketch.sha256` equals `1bac193a2482c8a4f743d1bf6ada17080c87ca2d58f7964fb20d481664c50985` and `bytes` is 40968. |

**Negative, cleanup, cancellation.** L04-10. Adapter cleanup with no server still runs the runner cleanup stage; a raised cleanup hook fails workflow (`workflow_status` `FAIL`) and does not change L04-09.

**Must not claim.** A selected product format, seek policy, checkpoint policy, or that matching LAB-03's 64-segment probe is an approved budget. Must not claim LAB-03 playback quality.

### 3.2 LAB-05 resource-peak experiment

**Choice.** Bounded addition inside the existing project, with a second make name. The lifecycle proof stays registered as `lab05`.

The shipped project is `client/labs/lab-05-transition-proof`. Its manifest revision is `v1-proof`. Its firmware loads two screens and prints `LAB05_READY`. Registry entry `lab05` omits `adapter` (so it is `generic`), uses evidence slug `lab-05`, and has variants `manual` and `scripted`. Generic `run_with_adapter` always finalizes `experiment_verdict=UNMEASURED`. Published runs `docs/labs/evidence/lab-05/runs/20261017-manual-run-fd3cc46d/` and `docs/labs/evidence/lab-05/runs/20261017-scripted-run-5a9c9dd1/` record workflow `PASS`, experiment `UNMEASURED`, service and scenario `not_configured`, and no metrics file. The scripted serial log does not contain `LAB05_HARNESS_DONE`. That limitation stays. This plan does not require the lifecycle firmware to emit that marker and does not reinterpret those runs as a resource measurement.

The product index row that projected a folder `lab-05-transition-peak` is not the directory that exists. Do not rename `client/labs/lab-05-transition-proof`.

**Product question (peak only).** With one persistent root, can stage motion allocate its largest buffer while one sketch image is live, audio is stopping, an inbox page is applying, and a second card image is being prepared? How does that peak compare with a full scene replacement control in the same binary family?

**Non-goals.** Replacing or re-scoring the lifecycle proof. Approving transition visuals. Measuring inbox paging (LAB-02) or playback sync (LAB-03) again. Any integrated login journey.

**Decided inputs.** 320×240 canvas. One logical sketch image is 120×120 RGB565 (28,800 bytes) from the sketch experience. Update levels in coding-standard section 5: data arrival is not, by itself, a reason to destroy the shell. Cover-down parameters in `docs/product/device-ui/patterns/TRANSITION-COVER-DOWN.md` (400 ms, incoming root from `y=+240` to `y=0`) are a draft pattern with no visual approval. The peak harness uses those numbers as the animation under test and does not treat them as a pass limit.

**Exploratory.** Scenario ids `P-overlap` and `P-replace` (whether a persistent root is the product transition, and any visual design). Each sets `lab_metrics.scenarios.<id>.class` to `exploratory`, keeps `observation`, and is absent from `lab_metrics.experiment_inputs`. The report decision for the persistent root stays **OPEN**. Decided experiment inputs are `P-alloc-fail`, `P-stale-callback`, and `P-cancel` (coding standard sections 4 and 5: one terminal, no use of a destroyed handle, release on cancel).

**Comparison.** Control is variant `replace` / `scripted-replace` inside this same project: the harness frees the old tree before allocating the incoming image. It is a measured control, not a baseline the peak waits for. There is no x02 comparison arm.

**Project.**

| Item | Lifecycle (unchanged) | Peak (new) |
|---|---|---|
| Make name | `lab05` | `lab05peak` |
| `lab_id` | `LAB-05` | `LAB-05` |
| `project_dir` | `client/labs/lab-05-transition-proof` | same directory |
| `evidence_slug` | `lab-05` | `lab-05-peak` |
| Adapter | `generic` (key omitted) | `lab05peak` |
| Default variant | `manual` | `manual` |
| Variants | `manual`, `scripted` | `manual`, `scripted`, `replace`, `scripted-replace` |
| Build dirs | `build/labs/lab-05-transition-proof-manual` and `-scripted` | `build/labs/lab-05-transition-proof-peak-manual`, `-peak-scripted`, `-peak-replace`, `-peak-scripted-replace` |
| Kconfig | `CONFIG_LAB05_PEAK` unset | `CONFIG_LAB05_PEAK=y` in `sdkconfig.defaults.peak`. Replace variants also set `CONFIG_LAB05_PEAK_REPLACE=y`. |
| Server | none | none |
| Boot marker | `LAB05_READY` (existing string) | `LAB05_PEAK_READY build=<id> mode=<manual\|scripted> control=<persistent\|replace> server=none` |
| Scripted completion | not required for the existing generic PASS | `LAB05_PEAK_HARNESS_DONE status=complete control=<persistent\|replace>` required for peak scripted workflow `PASS` |

`make lab.lab05peak.scripted` with `VARIANT` unset selects variant `scripted` (persistent root). `VARIANT=scripted-replace` is the only way to run the control. `make lab.lab05` and `make lab.lab05.scripted` must not grow a `VARIANT` that enables peak.

Manifest edit is additive. `lab_revision` stays `v1-proof`. A new object `peak` carries `lab_revision` `v1-peak`, `evidence_slug` `lab-05-peak`, `trace_schema` `lab05-peak-trace/v1`, and `server_path` `none`. Lifecycle fields are not retargeted at the peak experiment.

Peak code is compiled only under `#if CONFIG_LAB05_PEAK`, with CMake source/dependency selection also excluding the peak implementation from the lifecycle link. Kconfig `depends on` alone does not exclude C source files. The existing `app_main` two-screen path remains the `#else` branch. Do not change the lifecycle marker strings.

**Depends on.** Nothing from LAB-04. The 28,800-byte image is a fixed RGB565 buffer filled by the harness, not a chosen wire format. Audio-stop and page-apply are local synthetic workers with bounded queues (capacity 8 commands each). They do not open a socket and do not call LAB-02 or LAB-03 code.

**Implementation outline.**

| Piece | Path |
|---|---|
| Firmware | Same project. New component `components/lab05_peak/` excluded from the lifecycle link by conditional CMake source/dependency selection; its options also use Kconfig `depends on LAB05_PEAK`. |
| Fixture | In-tree synthetic page of 16 summary records and two 28,800-byte images. No server directory. |
| Parser | `client/labs/lab-05-transition-proof/tools/parse_lab05_peak.py`, set as `trace_parser` **only** on the `lab05peak` registry entry. `lab05` stays without `trace_parser`. |
| Adapter | `scripts/lab_adapter_lab05peak.py`. `execute` classifies the peak trace. It must not import `lab_adapter_lab02`. |
| Report | `docs/labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md` |
| Lifecycle evidence | Remains `docs/labs/evidence/lab-05/`. Peak runs must not write there. |

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| P-overlap | Persistent root. Start cover-down while audio-stop worker runs, a 16-item page applies, and the second 28,800-byte image allocates. Repeat 20 times. | One `success` per repetition if every allocation returns and no callback reads a freed handle | `LAB05_PEAK_TRACE` rows `scenario=overlap` |
| P-replace | Same workers, but the old tree is freed before the new image allocation | One `success` or `failure` `alloc` per repetition. Both outcomes are recorded. The control is not required to beat the persistent arm. | `control=replace` |
| P-alloc-fail | Injected allocation failure on the second image | Exactly one `failure` `alloc`. No use of the old handle after delete. | L1 `test_lab05_peak_alloc_fail` |
| P-stale-callback | Callback queued before delete, delivered after | `stale`. Zero writes through the old handle. | L1 `test_lab05_peak_stale_handle` |
| P-cancel | Cancel during the 400 ms animation | `cancelled`. Workers reach their own terminal. Live image count returns to the declared steady state. | Trace `terminal=cancelled` |

**Acceptance criteria.** These rows exist to prove the peak work was added as `lab05peak` and that the lifecycle proof was left intact.

| ID | Level | Check | Expected |
|---|---|---|---|
| L05-01 | L1 | `test_lab05peak_registry_gates` | Three fixtures, in order. (1) A registry document with make name `lab05peak` and `adapter: lab05peak` fails `validate_document_schema` while the schema `enum` is only `generic`, `lab02`, `lab03`, `lab10`. After `"lab05peak"` is added to that `enum` in `config/labs/labs.registry.schema.v1.json`, the same document passes schema validation. (2) Before `_ADAPTER_MODULES` contains `"lab05peak": "lab_adapter_lab05peak"`, `resolve_adapter` raises `LabRegistryError` matching `Unsupported adapter key`. After that map entry, `resolve_adapter(...).__class__.__module__` is `lab_adapter_lab05peak`. (3) Before the uniqueness check, two entries that share `lab_id` and use an existing `project_dir` pass `validate`. After it, `validate` exits non-zero unless the only shared `lab_id` is `LAB-05` on make names `lab05` and `lab05peak`, with `evidence_slug` `lab-05` and `lab-05-peak`, the same `project_dir`, and `adapter_key` `generic` and `lab05peak`. `lab05` variants are only `manual` and `scripted`. No Makefile `lab.%` rule is added. The test reads `LabSpec.adapter_key` from `load_registry`, not `make config.labs`, because the snapshot omits `adapter`. |
| L05-02 | L1 | `manifest.lab.json` | `lab_revision` remains `v1-proof`. `peak.lab_revision` is `v1-peak`. `peak.evidence_slug` is `lab-05-peak`. |
| L05-03 | L0 | Three sdkconfig reads | `make lab.lab05.build`: `CONFIG_LAB05_PEAK` is not `y`. `make lab.lab05peak.build`: `CONFIG_LAB05_PEAK=y` and `CONFIG_LAB05_PEAK_REPLACE` is not `y`. `make lab.lab05peak.build VARIANT=scripted-replace`: both symbols are `y`. `make lab.lab05peak.scripted.build` is the `scripted` variant, because that target passes `--variant scripted` and ignores `VARIANT`. |
| L05-04 | L1 | Strings or Kconfig dependency | Peak markers `LAB05_PEAK_READY` and `LAB05_PEAK_HARNESS_DONE` are compiled only when `CONFIG_LAB05_PEAK=y`. |
| L05-05 | L2 | `make lab.lab05.scripted KIT=<kit>` | Evidence directory is `docs/labs/evidence/lab-05/runs/<id>/`. `experiment_verdict` is `UNMEASURED`. Serial has no `LAB05_PEAK_READY`. Workflow may be `PASS` without `LAB05_HARNESS_DONE`, matching the existing generic adapter. This row fails if the lifecycle command starts writing `lab-05-peak` or sets an experiment `PASS`. |
| L05-06 | L2 | `make lab.lab05peak.scripted KIT=<kit>` | Evidence is `docs/labs/evidence/lab-05-peak/runs/<id>/`. Serial is `logs/device/labs/lab-05-peak/<id>/serial.txt`. Workflow `PASS` requires `LAB05_PEAK_HARNESS_DONE status=complete control=persistent` after reset. |
| L05-07 | L2 | `make lab.lab05peak.scripted VARIANT=scripted-replace KIT=<kit>` | Manifest `capture.variant` is `scripted-replace`. Metrics `control` is `replace`. This command is not `make lab.lab05.scripted`. |
| L05-08 | L1 | `test_lab05_peak_handles` | A callback after delete does not read the freed object. |
| L05-09 | L3 | Metrics `lvgl_high_water_bytes`, `lvgl_largest_free_bytes`, `internal_largest_free_bytes`, `psram_largest_free_bytes`, `anim_frame_us` p50/p95/p99/worst, `temp_image_bytes` | Recorded on device for `P-overlap` and `P-replace`. Performance verdict `UNMEASURED`. Source: foundation has no approved overlap budget; the inbox plan asks for the measurement. |
| L05-10 | L3 | `temp_image_bytes` peak | The simultaneous live RGB565 count is an integer in the trace. Two images are 57,600 bytes of pixel payload before any LVGL or draw-buffer overhead. The criterion checks the count and the byte field. It does not set a pass ceiling. |
| L05-11 | L1 | `test_lab05_peak_experiment_inputs` | `lab_metrics.experiment_inputs` equals `["P-alloc-fail","P-stale-callback","P-cancel"]`. `experiment_verdict` is `PASS` only when each id has `terminal_count` 1 and the terminal in the scenario table. `P-overlap` and `P-replace` have `class` `exploratory`, stay out of `experiment_inputs`, and still have `observation`. Workflow `PASS` on `lab05` or `lab05peak` does not satisfy this row. |
| L05-12 | L1 | `test_lab05_peak_cancel` | The `P-cancel` `operation_id` has `terminal` `cancelled`, `terminal_count` 1, and `live_image_count` equal to the integer `steady_live_images` in the lab README. The README declares that integer before the run. It is a provisional lab probe, not a product budget. |

**Negative, cleanup, cancellation.** L05-12. `P-alloc-fail` and `P-stale-callback` are the other decided terminals in L05-11. No server process is started; S-21 asserts the peak adapter binds no port. Keyboard interrupt during `execute` still finalizes a partial manifest with `workflow_status` `INCOMPLETE` and does not set `experiment_verdict` `PASS`.

**Must not claim.** That existing `lab-05` runs measured the peak. That workflow `PASS` on `lab05` is a performance result. That the persistent root or the replace control is the approved product transition. That cover-down 400 ms is a frame budget.

### 3.3 LAB-06 — stage shell and panel lifecycle

**Product question.** Can a 320×240 shell switch stages without stale handles, duplicate timers, or a whole-screen rebuild on a data change?

**Non-goals.** Real roster, PIN verification, inbox media, record capture, or send. Approving placeholder screens. Choosing theme colors (`LAYOUT-SPLIT-PANE` status: draft, no visual approval).

**Decided inputs.**

| Input | Source |
|---|---|
| Canvas 320×240. Upper slot `y=[0,80)`, lower `y=[80,240)`. Split stays at `y=80`. | `docs/product/device-ui/layouts/LAYOUT-SPLIT-PANE.md` |
| One panel module per slot. Stage changes swap panels. Shell geometry stays. | `docs/product/device-ui/patterns/PANEL-MODULE-CONTRACT.md` |
| Cover-down 400 ms, incoming `y=+240` to `y=0`, reverse on the way back | `docs/product/device-ui/patterns/TRANSITION-COVER-DOWN.md` (draft, no visual approval; use as the motion under test) |
| Level 2 only for a session-stage change. Data uses Level 0 or Level 1. Handles invalidated before destroy. | Coding standard section 5 |
| One component applies session transitions | Coding standard section 3 |

**Exploratory.** Scenario id `H-placeholder`. `lab_metrics.scenarios.H-placeholder.class` is `exploratory`, `observation.placeholder` is 1, and the id is absent from `experiment_inputs`. The report decision for those screens stays **OPEN** (`MISSING.md`). Screens in that set: connecting copy, Wi-Fi copy, message inbox, settings, recipient picker, record chrome beyond decided geometry, send progress, send result. Login screens that already exist as indexed device-UI documents may be represented as synthetic panels, still without live login. Decided experiment inputs are `H-cycle`, `H-data`, `H-data-during-exit`, `H-level2-control`, `H-destroyed`, and `H-cancel`.

**Comparison.** Negative control is a scripted scenario that forces Level 2 on a badge update. The expected terminal for that control is `failure` `level2_on_data` if the shell does it. There is no external baseline binary.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-06-stage-shell/` |
| Server | `none` |
| Registry | `lab06`, adapter `lab06`, evidence `lab-06` |
| Make | `make lab.lab06`, `make lab.lab06.scripted` |
| Variants | `manual` (default), `scripted` |
| Boot | `LAB06_READY build=<id> mode=<manual\|scripted> server=none trace_schema=lab06-trace/v1` |
| Harness | `LAB06_HARNESS_DONE status=complete` |
| Trace | `lab06-trace/v1` |

**Depends on.** The layout, panel, and transition documents above. No earlier new lab. LAB-07 may later reuse this stage owner; LAB-06 does not wait for LAB-07.

**Implementation outline.** Local session owner, slot mounter, and test panels: roster, PIN, empty hangout, inbox, record, receipt. Fixed synthetic strings. Enter and exit every stage. Apply progress, badge, and roster revision both while the stage is idle and while the 400 ms exit animation runs. Parser and adapter as in section 1. Report: `docs/labs/evidence/lab-06/REPORT-LAB-06-v1.md`.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| H-cycle | Enter and exit each test stage 20 times | `terminal=success`, `terminal_count=1` per enter. After 20 cycles `widget_live` equals the README integer `live_widgets.<stage>` and `timer_count` equals the count at cycle start | L1 `test_lab06_cycle` |
| H-data | Badge, progress, and roster revision while visible | `success` and update level 0 or 1. Identity keys (focused synthetic id) preserved. | Trace `update_level` |
| H-data-during-exit | Same data during the exit animation | Exactly one terminal, either `success` or `stale`. `old_panel_text_visible=0` | L1 `test_lab06_data_during_exit` |
| H-level2-control | Forced full rebuild on a badge | `failure` `level2_on_data` | Negative test |
| H-destroyed | Callback after panel delete | `stale`, zero handle uses | L1 `test_lab06_stale_panel` |
| H-cancel | Cancel mid-transition | Exactly one `cancelled`. `stage` equals README `cancel_stage` (`source` or `destination`). `timer_count` unchanged | L06-08 |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L06-01 | L0 | Both variant builds | Exit 0. Built sdkconfig `CONFIG_LV_MEM_SIZE` equals 65536. A different value fails this row. |
| L06-02 | L1 | `test_lab06_single_writer` | A second component calling the transition function is rejected. |
| L06-03 | L1 | `test_lab06_geometry` | Upper and lower slot bounds match the split-pane table (320×80 and 320×160). Source: `LAYOUT-SPLIT-PANE`. |
| L06-04 | L1 | `test_lab06_idempotent_enter` | Entering the current stage does not allocate a second timer. |
| L06-05 | L2 | Scripted run `make lab.lab06.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-06`. `workflow_status` is `PASS` only when serial contains `LAB06_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L06-06 | L3 | `anim_us` and `build_us` p50/p95/p99/worst in microseconds; LVGL high-water and largest free in bytes | Recorded. Performance verdict `UNMEASURED`. The 400 ms cover-down duration is the animation length under test, not a p95 gate. |
| L06-07 | L2 | Trace `placeholder=1` on inbox, record, receipt, settings-shaped panels | `lab_metrics.scenarios.H-placeholder.class` is `exploratory` and the id is absent from `experiment_inputs`. `observation.placeholder` is 1. Report decision for those screens stays **OPEN** (`MISSING.md`). |
| L06-08 | L1 | `test_lab06_cancel` | One `operation_id` has `terminal` `cancelled`, `terminal_count` 1, `stage` equal to README `cancel_stage`, and `timer_count` equal to the count sampled before the cancel. |
| L06-09 | L1 | `test_lab06_experiment_inputs` | `experiment_inputs` equals `["H-cycle","H-data","H-data-during-exit","H-level2-control","H-destroyed","H-cancel"]`. `experiment_verdict` is `PASS` only when each id has `terminal_count` 1 and the terminal in the scenario table. `H-placeholder` stays out of that array and still has `observation`. Workflow `PASS` does not satisfy this row. |

**Negative, cleanup, cancellation.** L06-08. Queue capacity for stage events is 8, a provisional lab probe named in the README; overflow emits one `failure` `overflow` and preserves the overflow count (S-08). No server.

**Must not claim.** Visual approval, production screens for MISSING entries, or a frame-time pass.

### 3.4 LAB-07 — readiness, roster, mute, and endpoint health

**Product question.** Can independent pollers keep roster stage, heartbeat, and mute outcomes aligned with the decided login timings, and what touch-to-trace gap is observed while a request is held?

**Non-goals.** Approving health or roster JSON. Defining stale-clock details. PIN submission (LAB-08). Treating transport reachability as application readiness.

**Decided inputs** from `docs/product/experiences/endpoint-login.md` and the decided restatements in `docs/product/communication-contracts/endpoint-health-and-login.md`:

| Rule | Value | Source |
|---|---|---|
| Active roster refresh | 60 seconds | EXP-LOGIN owner-decided roster policy |
| Idle refresh | 5 minutes after 10 minutes with no user activity and no roster change | same |
| Return to 60 seconds | after user activity or a roster change | same |
| Roster request timeout | 1 second | DEC-038 |
| Failed periodic check | one immediate retry; if that fails, leave the roster for connecting | same section |
| Health heartbeat | 60 seconds, independent of roster activity | EXP-035 |
| Stale endpoint | after three missed beats | COM-002. The clock, grace, and what clears stale are `COM-OPEN-002` |
| Readiness | HTTP 200 with valid response information before selectable cards | Experience model. Field-level validity is `COM-OPEN-004` |
| Wi-Fi absent | Distinct from connecting | Experience model. Exact v2 copy is OPEN (`SCR-WIFI-UNAVAILABLE` in `MISSING.md`) |
| Mute | Cancels in-progress login work and turns the display off. A background result must not change the user stage. | EXP-LOGIN mute; coding standard sections 3 and 6 |
| Eight users | The experience requires the roster to be able to present eight users | EXP-LOGIN revision history. This is a functional display requirement, not a memory budget. |

**Exploratory.** Scenario ids `R-wire` and `R-stale-clock`. Each has `class` `exploratory`, an `observation`, and is absent from `experiment_inputs`. `R-wire` records the fixture JSON keys actually parsed. `R-stale-clock` records the grace and clear-stale behavior the fixture used. `COM-OPEN-001` through `COM-OPEN-005` stay open. The fixture schema id is `lab07-health-roster/v1`, not an `fl.*` product schema. Wi-Fi copy stays **OPEN**; `R-wifi-absent` checks `stage` is not `connecting` and `selectable_cards` is 0, and does not check an English string. Decided experiment inputs are `R-wifi-absent`, `R-invalid-200`, `R-empty`, `R-eight`, `R-retry`, `R-retry-fail`, `R-idle`, `R-stale-beat`, `R-mute`, and `R-timeout-ui`. `R-invalid-200` passes only when the fixture flag `valid` is 0 and `selectable_cards` is 0. That flag is not a product body schema.

**Comparison.** L1 virtual-clock model versus L3 device run of the same scenario ids. The device run is not "better" by default; both are recorded at their levels. No integrated-shell baseline.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-07-readiness-roster/` |
| Server | `server/labs/lab-07-readiness-roster/` |
| Port | **8767** |
| Registry | `lab07`, adapter `lab07`, evidence `lab-07`, no `workflow` key |
| Make | `make lab.lab07`, `make lab.lab07.scripted`. Optional explicit `lab.lab07.server` rule for a detached listener. |
| Variants | `manual`, `scripted` |
| Boot | `LAB07_READY build=<id> mode=<manual\|scripted> fixture=lab07-health-roster/v1 harness=<0\|1>` |
| Harness | `LAB07_HARNESS_DONE status=complete` |
| Trace | `lab07-trace/v1` |

**Depends on.** Decided timings above (documents, not a lab artifact). Stage owner: if L06-02 (`test_lab06_single_writer`) and L06-03 (`test_lab06_geometry`) pass in `make test`, LAB-07 links that session-transition module and does not fork a second writer. If they have not landed, LAB-07 ships a login-local stage owner and the README line `stage_owner=lab07-local`. It must not block on a LAB-06 performance result or on L06-06. The satisfying artifacts, when present, are those two passing tests.

**Implementation outline.** Firmware stage reducer plus two pollers (roster, health) with bounded queues. Server fixture scripts: Wi-Fi absence (device-side, server idle), delayed HTTP 200, invalid HTTP 200, empty roster, one user, eight users, add, remove, reorder, two failed checks, Wi-Fi return, mute, unmute. Health body is not a person's presence. Virtual clock in L1 advances the 60-second, 10-minute, and 5-minute schedules without sleeping. Device scripted run uses real delays for one shortened representative set (one 1-second timeout, one heartbeat, one roster refresh) and records that the long idle schedule was L1-only unless a run actually waits. Report: `docs/labs/evidence/lab-07/REPORT-LAB-07-v1.md`.

Adapter deadlines: startup 10 s, health 10 s, seed 10 s, readiness 15 s, action 5 s, cleanup 5 s, copied from the LAB-02 workflow integers as host limits. Product request timeout remains 1 second and is enforced in firmware, not by the host deadline.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| R-wifi-absent | Station down | Stage `wifi_unavailable`. No selectable card. | Trace `stage` |
| R-invalid-200 | HTTP 200 with the fixture's invalid body | Not ready. Stage stays connecting. | Server script `invalid-200` |
| R-empty | Valid body, zero users | Empty hangout stage, not a crash | Trace |
| R-eight | Eight synthetic users | Eight identities addressable. Focus id stable across reorder. | L1 and scripted trace `user_count=8` |
| R-retry | One failed periodic check, then success | One retry, still on roster | Attempt count 2, stage unchanged |
| R-retry-fail | Failed check and failed retry | Stage `connecting` | Trace |
| R-idle | Virtual clock: 10 minutes idle | Next roster period is 300 seconds. Heartbeat period stays 60 seconds. | L1 `test_lab07_cadence` |
| R-stale-beat | Three missed health beats | `stale=1`. User `stage` unchanged if a roster is already showing. `missed_beats=3` | L1 `test_lab07_stale_flag` |
| R-stale-clock | Fixture varies grace and what clears stale | Recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-002`) |
| R-wire | Fixture health and roster JSON | Keys parsed are recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-001`, `COM-OPEN-004`) |
| R-mute | Mute during an in-flight roster response | Exactly one `stale`. `stage=muted`. `selectable_cards=0`. `inflight_requests=0` | L07-11 |
| R-timeout-ui | Server holds the response past 1 second | `terminal=timed_out`, `terminal_count=1`, `touch_during_wait=1`. No `wrong_pin` field is set | L1 virtual clock; L2 serial `touch_during_wait` |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L07-01 | L1 | `test_lab07_cadence` | Active period 60 s, idle period 300 s after 600 s idle, heartbeat 60 s. Source: EXP-LOGIN. Tolerance at L1 is exact virtual ticks. |
| L07-02 | L1 | `test_lab07_timeout` | Roster attempt terminalizes at 1000 ms on the virtual clock, plus or minus 0 ms. Source: DEC-038. Device L3 records the measured timeout in milliseconds and does not tighten the product rule. |
| L07-03 | L1 | `test_lab07_one_retry` | A single periodic failure schedules exactly one immediate retry. |
| L07-04 | L1 | `test_lab07_invalid_not_ready` | Fixture flag `valid` is 0. `selectable_cards` is 0. The test does not compare JSON field names to a product schema. |
| L07-05 | L1 | `test_lab07_mute_stale` | Late success after mute is `stale` and `stage` stays `muted`. |
| L07-06 | L2 | Scripted run `make lab.lab07.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and the server health JSON. Health `run_id` equals manifest `run_id`. Health `fixture_sha256` equals the fixture file hash. `workflow_status` is `PASS` only when post-reset serial contains `LAB07_HARNESS_DONE status=complete` and cleanup succeeded. `server.revision` equals the server `manifest.lab.json` `lab_revision`. This row does not read `experiment_verdict`. |
| L07-07 | L3 | Touch-to-trace gap during a held request; UI tick p50/p95/p99/worst in microseconds | Recorded. Performance verdict `UNMEASURED`. Source: no approved input-latency budget. |
| L07-08 | L3 | Eight-user live widget count, LVGL largest free block in bytes | Recorded in the metrics distribution. Performance verdict `UNMEASURED`. This row does not set `experiment_verdict`. |
| L07-09 | L1 | Fixture schema id is `lab07-health-roster/v1` | Report lines `COM-OPEN-001: OPEN` and `COM-OPEN-004: OPEN`. `R-wire.class` is `exploratory`. |
| L07-10 | L1 | `test_lab07_stage_enum` | A header under `client/labs/lab-07-readiness-roster/` defines the stage tokens `wifi_unavailable`, `connecting`, `roster`, `empty_hangout`, and `muted` as distinct constants. LAB-08 links this header. The test fails if a second writer module also defines those tokens. |
| L07-11 | L1 | `test_lab07_mute_release` | One roster `operation_id` has `terminal` `stale`, `terminal_count` 1, `stage` `muted`, `selectable_cards` 0, and `inflight_requests` 0. |
| L07-12 | L1 | `test_lab07_experiment_inputs` | `experiment_inputs` equals `["R-wifi-absent","R-invalid-200","R-empty","R-eight","R-retry","R-retry-fail","R-idle","R-stale-beat","R-mute","R-timeout-ui"]`. `experiment_verdict` is `PASS` only when each id has `terminal_count` 1 and the fields in the scenario table. `R-wire` and `R-stale-clock` stay out of that array and still have `observation`. Workflow `PASS` does not satisfy this row. |

**Negative, cleanup, cancellation.** L07-11. Invalid JSON, HTTP non-200, and queue overflow (roster result queue capacity 4, a provisional lab probe) each produce one terminal and do not clear focus. Adapter stops the child on every path. A late child that fails `wait` stays owned long enough for one cleanup retry, matching the ownership behavior LAB-02's server manager is required to keep. Orphaning the pid fails the adapter test.

**Must not claim.** A valid roster schema, a stale-clock definition, v2 connecting or Wi-Fi copy, or that L07-01's virtual-clock result is an L3 frame-time result.

### 3.5 LAB-08 — PIN operation, retry, and cooldown

**Product question.** Do retry, idempotency, and cancellation preserve one logical login?

**Non-goals.** Defining PIN JSON, session token format, or concurrent-key product behavior. Showing a real entered PIN in logs. Building the signed-in carousel.

**Decided inputs** from EXP-LOGIN and `endpoint-health-and-login.md` section 3:

| Rule | Value |
|---|---|
| Four digits, each 1–9, masked `*` immediately | EXP-014 |
| After digit four, copy `checking...`; further keypad input ignored while pending | EXP-014 |
| Wrong PIN: clear entry, show `wrong pin` for 2 seconds | EXP-014, SCR-PIN-ENTRY delegation in the experience |
| Timeout is not a wrong PIN | EXP-014, EXP-022 |
| Four transport attempts, 1-second attempt timeout, delays 1 s, 2 s, then 4 s | EXP-022. Owner accepts about 11 seconds total. |
| All four transport attempts fail | Stage connecting |
| Five wrong PINs | 60-second cooldown. Neutral copy `Too many attempts. Try again in <seconds>` | EXP-014 |
| Failure count tracks the selected user; switching users resets the count; success clears it; cooldown expiry clears it | DEC-033 |
| Same operation key and exact request on retry | EXP-014 |
| Server replays the stored result for a duplicate during the one-minute retention window | EXP-014. When retention starts and what happens after expiry are `COM-OPEN-008`. |
| BOOT: 0 digits returns to roster; 1–3 digits clears them and stays; after digit four BOOT is ignored | EXP-030 |
| Partial entry: 60 seconds since last digit returns to roster | Experience PIN table |
| Mute during verification cancels and turns the display off; a late success must not undo mute | Experience mute; roadmap result sought, consistent with coding standard section 3 |
| One remote login side effect per accepted submission | Coding standard section 9, and the experience's unique operation key |

**Exploratory.** Scenario ids `K-concurrent`, `K-mismatch`, and `K-mute-server`. Each has `class` `exploratory`, an `observation`, and is absent from `experiment_inputs`. `K-mute-server.observation` records the server side-effect count after mute and does not choose a product response (`COM-OPEN-009`). `COM-OPEN-006`, `COM-OPEN-007`, and `COM-OPEN-008` stay open. Decided experiment inputs are `K-ok`, `K-wrong`, `K-five`, `K-switch-user`, `K-timeouts`, `K-lost`, `K-mute-local`, `K-boot`, and `K-idle`.

**Comparison.** L1 virtual clock for the 1/2/4-second schedule and the 60-second cooldown, then one L3 device run of wrong-PIN and lost-response. Baseline is the decided schedule, not another firmware image.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-08-pin-cooldown/` |
| Server | `server/labs/lab-08-pin-cooldown/` port **8768** |
| Registry | `lab08`, adapter `lab08`, evidence `lab-08` |
| Make | `make lab.lab08`, `make lab.lab08.scripted` |
| Boot | `LAB08_READY build=<id> mode=<manual\|scripted> fixture=lab08-pin/v1 harness=<0\|1>` |
| Harness | `LAB08_HARNESS_DONE status=complete` |
| Trace | `lab08-trace/v1` |

**Depends on.** L07-05 (`test_lab07_mute_stale`) and L07-10 (`test_lab07_stage_enum`). LAB-08 does not start a second session writer. If either test is absent, LAB-08 stops and the report names the missing test. It does not add a parallel reducer.

**Implementation outline.** PIN reducer with an operation envelope. Server stores one result per idempotency key and counts side effects. Evidence stores a key fingerprint, never the PIN. Report: `docs/labs/evidence/lab-08/REPORT-LAB-08-v1.md`.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| K-ok | Correct fixture PIN | `success`. Side-effect count 1. | Server counter |
| K-wrong | One wrong PIN | `failure` credential. Copy `wrong pin` then clear. Count increments by 1. | Trace, no digit values |
| K-five | Five wrong PINs for one user | Fifth enters cooldown for 60 seconds | L1 virtual clock |
| K-switch-user | Wrong PINs, then other user | Count for the new user starts at 0 | L1 |
| K-timeouts | All four attempts time out | `timed_out` then stage connecting. Wrong-PIN count unchanged. Elapsed delays 1 s, 2 s, 4 s on the virtual clock. | `test_lab08_transport_schedule` |
| K-lost | First attempt succeeds, response dropped | Retry with the same key. Side-effect count stays 1. Client one `success`. | Server log |
| K-mute-local | Mute after submit | Exactly one terminal, `cancelled` or `stale`. `display_power=0`. `digit_len=0` | L08-10 |
| K-mute-server | Late server success after mute | Side-effect count recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-009`) |
| K-boot | BOOT at 0, 2, and 4 digits | Roster; digits cleared and stay; ignored | L1 |
| K-idle | 60 seconds after a digit | Return to roster. Digits discarded. | L1 virtual clock |
| K-concurrent | Two in-flight uses of one key | Side-effect count recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-007`) |
| K-mismatch | Same key, different body | Fixture status recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-007`) |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L08-01 | L1 | `test_lab08_transport_schedule` | Attempt timeout 1000 ms. Delays 1000 ms, 2000 ms, 4000 ms. Four attempts. Wrong-PIN counter unchanged. Source: EXP-022. |
| L08-02 | L1 | `test_lab08_cooldown` | Fifth wrong credential starts a 60,000 ms cooldown. Source: EXP-014. |
| L08-03 | L1 | `test_lab08_wrong_pin_copy_time` | `wrong pin` visible for 2000 ms on the virtual clock. Source: experience text. |
| L08-04 | L1 | `test_lab08_idempotent` | Duplicate exact request: server side-effect count remains 1. |
| L08-05 | L1 | `test_lab08_mask` | Serial fixture and metrics contain no entered PIN. Any correlation fingerprint is derived from a high-entropy operation key, never from the four-digit PIN (hashing a PIN permits enumeration). Tests use known PINs and assert none are emitted, including on error paths. |
| L08-06 | L1 | `test_lab08_boot` | Three BOOT cases match EXP-030. |
| L08-07 | L2 | Scripted run `make lab.lab08.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-08`. `workflow_status` is `PASS` only when serial contains `LAB08_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L08-08 | L3 | Input callback time p50/p95/p99/worst in microseconds during `checking...` | Recorded. Performance verdict `UNMEASURED`. |
| L08-09 | L1 | `test_lab08_one_terminal` | A poller tick does not emit a second terminal or clear digits while `checking...` is pending. `terminal_count` for that `operation_id` is 1. |
| L08-10 | L1 | `test_lab08_mute_release` | The muted submission has `terminal` `cancelled` or `stale`, `terminal_count` 1, `display_power` 0, and `digit_len` 0. |
| L08-11 | L1 | `test_lab08_experiment_inputs` | `experiment_inputs` equals `["K-ok","K-wrong","K-five","K-switch-user","K-timeouts","K-lost","K-mute-local","K-boot","K-idle"]`. `experiment_verdict` is `PASS` only when each id has `terminal_count` 1 and the fields in the scenario table. `K-concurrent`, `K-mismatch`, and `K-mute-server` stay out of that array and still have `observation`. Workflow `PASS` does not satisfy this row. |

**Negative, cleanup, cancellation.** L08-10. BOOT at four digits is ignored, not a second terminal. Queue overflow on the result queue (capacity 2, a provisional lab probe) is one `failure` `overflow` and does not start a second login. Cleanup stops port 8768. A PIN that appears in `server.log` fails S-10.

**Must not claim.** Session issuance, concurrent-key policy, retention-window start, or that cooldown copy layout is visually approved.

### 3.6 LAB-09 — signed-in authority and late-event isolation

**Product question.** After mute, sign-out, user switch, or session expiry, can a late result for user A be rejected so it does not become visible as user B?

**Non-goals.** Approving a token format, revocation protocol, or error-code mapping. Implementing inbox, media, or profile product behavior. Using a caller-supplied user id as the actor.

**Decided inputs.** Coding standard sections 3, 4, and 9: one session writer, exactly one terminal, frozen operation input, stale is a real outcome, remote success is not reported until the contract condition holds. The proposed envelope invariant in `docs/product/communication-contracts/user-scoped-envelope.md`: the server derives the actor from the credential; a body field or caller-supplied user id cannot select the actor. That invariant is the behavior under test. Issuance and revocation are still `COM-OPEN-001` on that contract. Mute cannot be undone by a late success (LAB-08 local rule).

**Exploratory.** Scenario ids `I-confused`, `I-revoke-branches`, and `I-expire-http`. Each has `class` `exploratory`, an `observation`, and is absent from `experiment_inputs`. `I-confused` records whether a body user id incremented the other user's fixture counter. That records the proposed envelope behavior and does not approve it (`user-scoped-envelope.md` status: engineering proposal, no v2 wire approval). `I-revoke-branches` runs fixture modes `revoke-on-mute` and `leave-server-session` and records both. `I-expire-http` records the HTTP status the fixture returned. `COM-OPEN-001` and `COM-OPEN-003` on that contract stay open. Inbox and profile side effects used here are fixture operations. Decided experiment inputs are `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client` (coding standard sections 3 and 4: one terminal, do not apply a late result to another user or restore a stage).

**Comparison.** Each late-result case compares "applied" versus "rejected" counts against the fixture script. No external baseline.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-09-session-isolation/` |
| Server | `server/labs/lab-09-session-isolation/` port **8769** |
| Registry | `lab09`, adapter `lab09`, evidence `lab-09` |
| Fixture schema | `lab09-session/v1` (explicitly not a product session schema) |
| Boot | `LAB09_READY build=<id> mode=<manual\|scripted> fixture=lab09-session/v1 harness=<0\|1>` |
| Harness | `LAB09_HARNESS_DONE status=complete` |
| Trace | `lab09-trace/v1` |

**Depends on.** L08-10 (`test_lab08_mute_release`). LAB-09 reuses that local cancel. It does not depend on L08-05, and it does not depend on `K-concurrent`, `K-mismatch`, or `K-mute-server`.

**Implementation outline.** A small signed-in client with four operation kinds: inbox fetch, media fetch, playback-state write, profile update. Each carries `operation_id` and a session generation. The server issues a fixture credential that binds endpoint, user, and session, and rejects a body that names a different user. Caches and media buffers are lab-bounded (one 28,800-byte image, one page of 16 summaries). Report: `docs/labs/evidence/lab-09/REPORT-LAB-09-v1.md`.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| I-mute | Start each operation kind for A, then mute, then deliver the body | Local `stale` or `cancelled`. A's bytes not visible. Display off. | Trace `applied=0` |
| I-signout | Sign out before the body | Same rejection. Stage signed out. | Trace |
| I-switch | Sign in as B before A's body | A's result not applied to B's cache. B's generation differs. | `session_generation` |
| I-expire-client | Server expires the fixture session before the response | `applied=0`. Stage is not restored. Exactly one terminal, not `success` | L1 `test_lab09_expire_client` |
| I-expire-http | Same response's HTTP status | Status integer recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-003`) |
| I-push | Duplicate and out-of-order fixture hints | No duplicate card ids in the local model | L1 |
| I-confused | Request names B while the credential is A's | Fixture counter for B recorded. Not an experiment input | `class=exploratory` (proposed envelope) |
| I-revoke-branches | Fixture modes `revoke-on-mute` and `leave-server-session` | Both observations recorded. Not an experiment input | `class=exploratory` |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L09-01 | L1 | `test_lab09_no_cross_apply` | After switch to B, A's media buffer length is 0 and B's stage is unchanged by A's body. |
| L09-02 | L1 | `test_lab09_actor_observation` | `I-confused.class` is `exploratory`. `observation.b_side_effects` is an integer. The id is absent from `experiment_inputs`. The test does not set `experiment_verdict` to `PASS`. Report line `COM-OPEN-001: OPEN` for `user-scoped-envelope.md`. |
| L09-03 | L1 | `test_lab09_one_terminal` | One terminal per operation id. |
| L09-04 | L2 | Scripted run `make lab.lab09.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-09`. `workflow_status` is `PASS` only when serial contains `LAB09_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L09-05 | L3 | Mute to local-cancel time, p50/p95/p99/worst in microseconds, from device trace | Recorded. Performance verdict `UNMEASURED`. |
| L09-06 | L1 | Grep of evidence fixtures | No raw credential strings in committed fixtures or in a redacted `server.log` sample. |
| L09-07 | L1 | Report | Line `COM-OPEN-001: OPEN`. |
| L09-08 | L1 | `test_lab09_mute_release` | The muted operation has `terminal` `stale` or `cancelled`, `terminal_count` 1, `applied` 0, and `image_bytes` 0. |
| L09-09 | L1 | `test_lab09_experiment_inputs` | `experiment_inputs` equals `["I-mute","I-signout","I-switch","I-push","I-expire-client"]`. `experiment_verdict` is `PASS` only when each id has `terminal_count` 1 and the fields in the scenario table. `I-confused`, `I-revoke-branches`, and `I-expire-http` stay out of that array and still have `observation`. Workflow `PASS` does not satisfy this row. |

**Negative, cleanup, cancellation.** L09-08. Queue capacity 8 for inbound results, a provisional lab probe; overflow terminalizes the operation as one `failure` `overflow` and drops nothing silently. Cleanup clears the one image buffer (`image_bytes` 0) and stops port 8769.

**Must not claim.** A token protocol, an approved revocation design, inbox paging policy, or profile field meaning.

### 3.7 LAB-11 — send, receipt, and recovery

**Product question.** Can one frozen capture reach exactly one terminal when parts, receipts, and pushes fail or their responses are lost, and which of those outcomes are still an open contract?

**Non-goals.** Deciding atomic multi-recipient delivery, outbox retention, retry policy, or user-facing send copy. Adding a recording timer. Treating LAB-10's experiment verdict as a send result. LAB-10's published qualification report records workflow `PASS` and experiment `UNMEASURED`; LAB-11 does not cite that verdict as evidence that capture succeeded on device.

**Decided inputs.** Coding standard sections 4 and 9: frozen input, exactly one terminal, idempotent retry key, success only when the chosen condition is met, and if work is discarded that fact is explicit. Sketch logical size 120×120 if the frozen payload includes a sketch (`EXP-SKP-001`). LAB-10 manifest capture bounds are the shape of a frozen local result: accepted points capped at 4096, duration cap 180000 ms, queue capacity 16, one terminal (`client/labs/lab-10-record-canvas/manifest.lab.json` `capture_policy`). LAB-11 consumes a **file** that already matches that shape. It does not link LAB-10's firmware.

**Exploratory.** Scenario ids `D-many`, `D-invalid-recipient`, `D-offline`, `D-push-fail`, and `D-restart`. Each has `class` `exploratory`, an `observation`, and is absent from `experiment_inputs`. `D-restart` records the fixture status and resumed sequence ids; it does not approve durable outbox or recovery policy (`COM-OPEN-003`). `fixtures/acceptance.json` field `condition` is `accept-on-archive-commit` or `accept-on-recipient-publish`. That string is copied into `observation.condition` and into the report. It is not an experiment input. The report line is `COM-OPEN-001: OPEN` for `Sent`. `fixtures/offline-probe.json` field `work` is `retained` or `discarded`; `D-offline.observation.work` copies it. `COM-OPEN-001` through `COM-OPEN-005` stay open. Decided experiment inputs are `D-one`, `D-drop-ack`, `D-drop-receipt`, `D-full`, and `D-cancel`. `D-one` passes only when the client `terminal` is `success` if and only if server `accepted` is 1 under the active fixture `condition`. That does not select the condition as product `Sent`.

**Comparison.** Two chunk sizes are lab probes, declared before the run: 1024 bytes (the chunk size already used in `server/labs/lab-03-media/fixtures/manifest.json`) and 4096 bytes. They are compared to each other on the same frozen payload. Neither is a product chunk size (`COM-OPEN-005`).

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-11-send-receipt/` |
| Server | `server/labs/lab-11-send-receipt/` port **8770** |
| Registry | `lab11`, adapter `lab11`, evidence `lab-11` |
| Fixture | `lab11-send/v1` plus a frozen capture file `fixtures/frozen-capture.json` |
| Boot | `LAB11_READY build=<id> mode=<manual\|scripted> fixture=lab11-send/v1 harness=<0\|1>` |
| Harness | `LAB11_HARNESS_DONE status=complete` |
| Trace | `lab11-trace/v1` |

**Depends on.** `client/labs/lab-10-record-canvas/manifest.lab.json` object `capture_policy`: `accepted_point_cap` 4096, `duration_cap_ms` 180000, `queue.capacity` 16. L11-01 reads those three fields. It is not a LAB-10 metrics verdict. If `docs/labs/evidence/lab-04/REPORT-LAB-04-v1.md` exists, an optional payload may copy `lab_metrics.scenarios.F-points.observation.encoded_bytes` and is labeled `format-candidate` with `class` `exploratory`. LAB-11 does not wait on L04-09. Absent that report, the payload is an opaque byte blob and the report line is `OPEN-SKP-03: OPEN`.

**Implementation outline.** Send operation owner, bounded part queue (capacity 8), receipt lookup by the frozen client message id. Server records durable part count, accepted flag, recipient count, and push result. Progress text is a Level 0 update on a static layout. Report: `docs/labs/evidence/lab-11/REPORT-LAB-11-v1.md`.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| D-one | One recipient, all parts acked, receipt kept | One terminal. Client shows the fixture's success phase only after `acceptance.json` condition. | Server `accepted=1`, client terminal `success` |
| D-many | Three recipients | Recorded under the active acceptance branch. Exploratory. | Not part of functional PASS |
| D-invalid-recipient | Recipient removed before commit | Outcome follows the branch. Exploratory (`COM-OPEN-001`). | Labeled |
| D-offline | Upload while station is down | `observation.work` equals `fixtures/offline-probe.json` `work` (`retained` or `discarded`). Not an experiment input | `class=exploratory` (`COM-OPEN-003`) |
| D-drop-ack | Part ack dropped | Retry of the same part sequence does not increase durable part count past the unique sequences. | Server part set |
| D-drop-receipt | Accepted response dropped | Lookup by client id returns the same receipt. No second accept. | Server `accepted` count stays 1 |
| D-push-fail | Push fails after commit | Receipt remains accepted. Push result is separate. Exploratory notification recovery. | Two fields in metrics |
| D-restart | Restart mid-upload under the fixture's recovery branch | Record status (`open`, `accepted`, or `unknown`) and resumed sequence ids. Exploratory; no product recovery claim. | L1, `class=exploratory` |
| D-full | Storage-full script | Exactly one `failure` `storage`. `live_buffers=0` | L11-09 |
| D-cancel | Four operation ids, one per phase: before first byte, mid-upload, after accept, during lookup | Each id has `terminal_count` 1. Before `accepted=1`, `terminal=cancelled`. After `accepted=1`, server `accepted` stays 1 | L1 `test_lab11_cancel_phases` |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L11-01 | L1 | `test_lab11_frozen_hash` | Frozen file hash matches the manifest. `accepted_point_count` ≤ `capture_policy.accepted_point_cap` (4096). `duration_cap_ms` in the frozen file is ≤ 180000. Logical dimensions are 120 and 120. Source: LAB-10 `capture_policy` and EXP-SKP-001. |
| L11-02 | L1 | `test_lab11_one_accept` | Lost complete response then lookup: server accepted count is 1. |
| L11-03 | L1 | `test_lab11_part_retry` | Duplicate part bytes do not create a second durable copy. Different bytes for the same sequence are `identity_conflict` in the fixture and do not overwrite. |
| L11-04 | L1 | `test_lab11_progress_level` | Progress text updates are level 0. A test fails if the scene root is recreated. Source: coding standard section 5. |
| L11-05 | L1 | `test_lab11_terminal` | Exactly one terminal. `checking` equivalent phase cannot remain without a terminal. |
| L11-06 | L2 | Scripted run `make lab.lab11.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-11`. `workflow_status` is `PASS` only when serial contains `LAB11_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L11-07 | L3 | Peak bytes of old plus new buffers, largest free block in bytes, part timing p50/p95/p99/worst in microseconds | Recorded for both chunk probes. Performance verdict `UNMEASURED`. Source: `COM-OPEN-005` has no chunk size or budget. |
| L11-08 | L1 | `acceptance.json` `condition` is copied into the report | Report line `COM-OPEN-001: OPEN`. `metrics.json` has no key `sent_approved`. |
| L11-09 | L1 | `test_lab11_storage_full` | One `operation_id` has `terminal` `failure`, `reason` `storage`, `terminal_count` 1, and `live_buffers` 0. |
| L11-10 | L1 | `test_lab11_experiment_inputs` | `experiment_inputs` equals `["D-one","D-drop-ack","D-drop-receipt","D-full","D-cancel"]`. `experiment_verdict` is `PASS` only when each id has the fields in the scenario table. `D-cancel` is four operation ids, and each of those ids has `terminal_count` 1. `D-many`, `D-invalid-recipient`, `D-offline`, `D-push-fail`, and `D-restart` stay out of that array and still have `observation`. Workflow `PASS` does not satisfy this row. `D-one` `PASS` does not change `acceptance.json` `condition`. |

**Negative, cleanup, cancellation.** L11-09. `D-cancel` is four operation ids, each with `terminal_count` 1, as in the scenario row. Queue overflow on the part queue (capacity 8, a provisional lab probe) is one `failure` `overflow`. Cleanup sets sketch and PCM buffer lengths to 0 and stops port 8770. A report that contains the product copy `Sent` as an approved outcome fails L11-08.

**Must not claim.** Atomic recipient delivery, outbox policy, user-facing failure copy, sketch wire format, or a LAB-10 hardware capture pass.

### 3.8 LAB-12 — profile and settings propagation

**Product question.** Which settings can be previewed, saved, and reconciled across two endpoints without presenting a failed save as saved?

**Non-goals.** Defining message color, the settings set, avatar catalog, or optimistic-save policy. Recoloring existing inbox cards. Conflating profile accent with sketch stroke color.

**Decided inputs.** Coding standard sections 5 and 9: level 0 or 1 for a field change, no whole-inbox rebuild, failed remote write is not local success, stale revision must not overwrite a newer one if the fixture uses `expected_revision`. Sketch stroke color stays on the sketch rail (`sketch-record-and-playback.md`); profile accent is a different field in the profile contract proposal. `docs/product/device-ui/screens/MISSING.md` says `SCR-SETTINGS` waits on an experience specification. Split-pane geometry from LAB-06's decided layout applies if a settings card is shown in the shell.

**Exploratory.** Scenario ids `P-save-policy`, `P-offline`, `P-message-color`, and `P-layout`. Each has `class` `exploratory`, an `observation`, and is absent from `experiment_inputs`. `fixtures/reject-preview.json` field `reject_preview` is `rollback` or `keep`; `P-save-policy.observation.reject_preview` copies it. `P-save-fail` itself stays decided for `presented_saved=0` only. `fixtures/layout.json` field `layout` is `split-pane` or `full-canvas`. When `layout` is `split-pane`, slot sizes match L06-03 (320×80 and 320×160). The layout value is not an experiment input. `COM-OPEN-001` through `COM-OPEN-005` in `user-profile.md` stay open. Visual approval of `SCR-SETTINGS` stays **OPEN**. Decided experiment inputs for the visual phase are `P-preview`, `P-modal`, and `P-return`. Decided experiment inputs for the shared phase are `P-save-fail`, `P-conflict`, `P-lost`, `P-invalid`, and `P-hint`.

**Comparison.** Visual phase has **no baseline** (there is no approved settings screen). Shared phase compares two fixture endpoints against each other. No x02 settings binary.

**Project.** One project, two phases that cannot share a default command.

| Item | Visual phase | Shared-profile phase |
|---|---|---|
| Slug | `client/labs/lab-12-profile-settings/` | same tree |
| Server | `none` | `server/labs/lab-12-profile-settings/` port **8771** |
| Make | `make lab.lab12` and `make lab.lab12.scripted` | `make lab.lab12 VARIANT=shared` and `make lab.lab12.scripted VARIANT=scripted-shared` only |
| Variants | `manual` (default), `scripted` | `shared`, `scripted-shared` |
| Adapter | `lab12` | same adapter, branches on variant |
| Evidence | `lab-12` | same slug. `run-manifest.json` `capture.experiment_phase` is `visual` or `shared`. A top-level `experiment_phase` key is dropped by `EvidenceRun` and fails L12-01 |
| Boot | `LAB12_READY ... phase=visual server=none` | `LAB12_READY ... phase=shared fixture=lab12-profile/v1` |
| Harness | `LAB12_HARNESS_DONE status=complete phase=visual` | `LAB12_HARNESS_DONE status=complete phase=shared` |

`make lab.lab12.scripted` must not start port 8771. A test fails if it does.

**Depends on.** L06-03 (`test_lab06_geometry`) when `fixtures/layout.json` `layout` is `split-pane`. The layout value is the exploratory fixture field in `P-layout`, not an implementer choice of product chrome. No dependency on a profile decision. The shared phase does not depend on L09-02. Synthetic tokens in the fixture are not an approved credential.

**Implementation outline.** Visual: settings cards, a modal, local preview of accent and avatar id, return to a fake inbox of three static cards. Shared: partial update, two-endpoint revision conflict, lost response, missed hint, invalid color, offline choice. Stroke-color swatches are a separate control that does not write `profile_accent`. Report: `docs/labs/evidence/lab-12/REPORT-LAB-12-v1.md` with two sections.

**Scenarios.**

| ID | Phase | Input | Terminal outcome | Proof |
|---|---|---|---|---|
| P-preview | visual | Change accent locally and cancel | Preview discarded. No success copy. | Trace `saved=0` |
| P-modal | visual | Open and close detail | Focus returns to the same card id. Overlay widget count returns to 0. | L1 |
| P-return | visual | Back to inbox | Inbox root not recreated. Focus id kept. | `update_level` ≤ 1 |
| P-save-fail | shared | Server rejects the patch | `presented_saved=0`. Exactly one `failure` | L12-02 |
| P-save-policy | shared | Same reject | `observation.reject_preview` equals `fixtures/reject-preview.json` `reject_preview` (`rollback` or `keep`). Not an experiment input | `class=exploratory` (`COM-OPEN-004`) |
| P-conflict | shared | Two endpoints edit revision 7 | First `success`. Second `failure` `revision_conflict`. First body unchanged. | Server revision |
| P-lost | shared | Response dropped | Same operation id replays one result. | Side-effect count 1 |
| P-invalid | shared | Color not in the fixture's allow list | `failure` `invalid`. Layout still has a fallback avatar tile. | Trace |
| P-offline | shared | Save while offline | `presented_saved=0`. `observation.work` copies `fixtures/offline-probe.json` `work` (`retained` or `discarded`). Not an experiment input | `class=exploratory` |
| P-hint | shared | Profile hint arrives | Does not by itself recolor sketch strokes or rebuild inbox cards. | `stroke_color_changed=0` |
| P-message-color | shared | Fixture values `follow-profile` and `frozen-on-message` | Both observations recorded. Not an experiment input | `class=exploratory` (`COM-OPEN-001`) |
| P-layout | visual | `fixtures/layout.json` `layout` | Observation copies `split-pane` or `full-canvas`. Not an experiment input | `class=exploratory` |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L12-01 | L1 | Adapter test with mode scripted and variant unset | Selected variant is `scripted`. No server socket. `capture.experiment_phase` is `visual`. A host test that calls `EvidenceRun.update_context` with top-level `experiment_phase` asserts the finalized manifest has no such key. |
| L12-02 | L1 | `test_lab12_not_saved` | Rejected patch leaves `presented_saved=0`. Source: coding standard section 9. |
| L12-03 | L1 | `test_lab12_conflict` | Stale revision does not overwrite. Side effects stay 1. |
| L12-04 | L1 | `test_lab12_stroke_isolated` | Profile accent write does not change the sketch swatch value. |
| L12-05 | L1 | `test_lab12_focus` | Modal close preserves focus id. |
| L12-06 | L2 | `make lab.lab12.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-12`. `capture.experiment_phase` is `visual`. `workflow_status` is `PASS` only when serial contains `LAB12_HARNESS_DONE status=complete phase=visual` and cleanup succeeded. Port 8771 is not bound. This row does not read `experiment_verdict`. |
| L12-07 | L2 | `make lab.lab12.scripted VARIANT=scripted-shared KIT=<kit>` | Actor: scripted run. `capture.experiment_phase` is `shared`. `capture.variant` is `scripted-shared`. Health JSON `run_id` and `fixture_sha256` match the run. `workflow_status` is `PASS` only when serial contains `LAB12_HARNESS_DONE status=complete phase=shared` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L12-08 | L3 | Frame time p50/p95/p99/worst in microseconds while a preview changes | Recorded on a visual device run. Performance verdict `UNMEASURED`. |
| L12-09 | L1 | `test_lab12_cancel_modal` | One `operation_id` has `terminal` `cancelled`, `terminal_count` 1, `overlay_widgets` 0, and `saved` 0. |
| L12-10 | L1 | `test_lab12_experiment_inputs` | Visual `experiment_inputs` equals `["P-preview","P-modal","P-return"]`. Shared `experiment_inputs` equals `["P-save-fail","P-conflict","P-lost","P-invalid","P-hint"]`. `experiment_verdict` is `PASS` only for the phase whose harness marker matched, and only when each id in that array has `terminal_count` 1 and the fields in the scenario table. `P-save-policy`, `P-offline`, `P-message-color`, and `P-layout` stay out of both arrays and still have `observation`. Workflow `PASS` does not satisfy this row. |

**Negative, cleanup, cancellation.** L12-09. Invalid color and conflict are the decided `failure` rows in L12-10. Queue overflow on the save result (capacity 4, a provisional lab probe) is one `failure` `overflow`. Shared cleanup stops port 8771. A visual run binds no port (L12-01).

**Must not claim.** Message-color meaning, which settings exist, optimistic save policy, or visual approval of `SCR-SETTINGS`.

### 3.9 LAB-13 — composed v2 journey

**Product question.** When decided behaviors run in one shell, do ownership, identity, bounded work, and terminal outcomes still hold, and which measurements move relative to the focused labs?

**Non-goals.** Approving any OPEN branch by including it. Repeating LAB-02, LAB-03, LAB-04, or the LAB-05 peak microbenchmarks as a single opaque end-to-end `PASS`. Claiming product parity. Promoting modules into `client/device/` inside this lab.

**Decided inputs.** Only behaviors whose owning experience or standard already decided them, composed in order: boot muted or unmuted, readiness and roster, PIN, a bounded inbox surface, playback only to the extent LAB-03's operation envelope is reused as a library of decisions (not as a claim that LAB-03's performance verdict passed), record only as a frozen LAB-10-shaped payload, receipt as the LAB-11 fixture branch under test, then mute or sign-out. Eight roster users (EXP-LOGIN). Inbox fixture sizes 8, 16, 64, and 256 summaries where a page provider is included (inbox plan common method). Those sizes exercise metadata. They must not create 256 widgets (LAB-02's live-view bound is the control: the composed inbox keeps a fixed live card count declared in the LAB-13 README, copied from the count LAB-02's README currently documents as four reusable card views).

**Exploratory.** Scenario id `J-experimental`. The trace field `branch` is `experimental`. `lab_metrics.scenarios.J-experimental.class` is `exploratory`, `observation` copies the terminal, and the id is absent from `experiment_inputs`. A focused scenario whose `class` is `exploratory`, and a focused run whose `experiment_verdict` is `UNMEASURED`, stay out of LAB-13 `experiment_inputs`. `lab_metrics.comparisons.<series>.source_experiment_verdict` equals that focused run's `experiment_verdict`. LAB-13 does not write `PASS` over `UNMEASURED`. Decided experiment inputs are `J-normal`, `J-pin-roster`, `J-push-snap`, `J-send-change`, `J-fetch-mute`, `J-outage`, and `J-cycles`. `J-send-change` checks the frozen-capture hash only. Send copy and the receipt branch name stay in `J-experimental.observation` when that step is included.

**Comparison.** Each comparable series names the focused lab's `metrics.json` path and run id. The delta is reported in the same unit. There is no x02 arm. If the focused artifact does not exist yet, that scenario's verification level is `blocked` and LAB-13 does not invent a number. The implementation order puts LAB-13 last so those artifacts exist as reports, including reports whose experiment verdict is `UNMEASURED`.

**Project.**

| Item | Value |
|---|---|
| Slug | `client/labs/lab-13-full-journey/` |
| Server | `server/labs/lab-13-full-journey/` port **8772** |
| Registry | `lab13`, adapter `lab13`, evidence `lab-13` |
| Fixture | `lab13-journey/v1`, one manifest that references the fixture revisions of the included labs by version string, not by copying their servers into one process without a revision |
| Boot | `LAB13_READY build=<id> mode=<manual\|scripted> fixture=lab13-journey/v1 harness=<0\|1>` |
| Harness | `LAB13_HARNESS_DONE status=complete` |
| Trace | `lab13-trace/v1` |

The integrated server implements the operations the journey actually calls. It does not proxy to ports 8765–8771 during the scripted run (one process, one health document). Its fixture manifest lists the source fixture hashes it vendored. A hash mismatch with the named focused fixture revision fails L1.

**Depends on.** Published reports and the criterion artifacts below, not performance passes. Section 8.2 separates mandatory journey implementation artifacts from optional comparison samples. A missing mandatory owner/module/rule blocks the affected implementation package. A missing focused measurement blocks only the comparison that cites it; L13-10 records that comparison as `blocked` and does not invent a number or a `PASS`.

| Need | Satisfying artifact |
|---|---|
| Inbox live-view count and page sizes | `client/labs/lab-02-bounded-inbox/README.md` states four reusable card views. L13-04 compares `widget_live` to 4. Page sizes 8, 16, 64, and 256 are metadata counts, not widget counts. |
| Playback operation ordering | `docs/labs/evidence/lab-03/REPORT-LAB-03-v2.md`. Its performance verdict is `INCOMPLETE`. L13-10 copies only fields that report lists as measured, and copies `source_experiment_verdict` as `INCOMPLETE` or `UNMEASURED` when that is the report's verdict. |
| Format byte tables | L04-09 `lab_metrics.scenarios.F-points.observation.encoded_bytes` when `docs/labs/evidence/lab-04/REPORT-LAB-04-v1.md` exists. If the report is absent, the journey uses an opaque byte blob and the report line is `OPEN-SKP-03: OPEN`. The byte field stays `class` `exploratory` and is not an experiment input. |
| Transition peak | L05-09 metrics and `docs/labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md`. Performance verdict there is `UNMEASURED`. Lifecycle `docs/labs/evidence/lab-05/` runs are not this artifact. |
| Stage owner | L06-02, L06-03, and S-20 report `docs/labs/evidence/lab-06/REPORT-LAB-06-v1.md` |
| Login timings | L07-01, L07-02, L07-03, and S-20 report `docs/labs/evidence/lab-07/REPORT-LAB-07-v1.md` |
| PIN mute terminal | L08-10 and S-20 report `docs/labs/evidence/lab-08/REPORT-LAB-08-v1.md` |
| Late-result rejection | L09-08, L09-09, and S-20 report `docs/labs/evidence/lab-09/REPORT-LAB-09-v1.md`. L09-02 is not a satisfying artifact. |
| Frozen capture shape | `client/labs/lab-10-record-canvas/manifest.lab.json` `capture_policy` (`accepted_point_cap` 4096, `duration_cap_ms` 180000, `queue.capacity` 16) |
| Receipt branch | L11-08 and L11-10. `acceptance.json` `condition` is copied and stays out of `experiment_inputs`. |
| Settings, only if the journey includes a settings step | L12-10 and S-20 report `docs/labs/evidence/lab-12/REPORT-LAB-12-v1.md`. The journey may omit settings. `lab_metrics.settings_step` is `omitted` or `included`. `omitted` is not a failure. |

**Implementation outline.** New shell that links or copies the focused modules. Copy is allowed; editing LAB-02, LAB-03, LAB-05 lifecycle, or LAB-10 sources is not. One session owner. Report: `docs/labs/evidence/lab-13/REPORT-LAB-13-v1.md` with a scenario-by-scenario level and a delta table against focused metrics.

**Scenarios.**

| ID | Input | Terminal outcome | Proof |
|---|---|---|---|
| J-normal | Unmute, ready, eight users, PIN success, inbox 16, play one item, stop, mute | Each included operation has one terminal. No cross-user cache. | Scripted trace |
| J-pin-roster | Roster refresh during PIN entry | Partial PIN preserved or cleared only by the decided PIN rules (60 s idle or BOOT). A roster body must not clear digits. | L1 |
| J-push-snap | Inbox push during snap or during play | Focus id kept or explicitly moved by the declared page rule. Widgets do not grow with inbox size 64 or 256. | `widget_live` versus size |
| J-send-change | Send while a page arrives | Send input stays the frozen capture. Inbox mutation does not change recipient or bytes. | Hash in trace |
| J-fetch-mute | Media fetch then mute | Exactly one `stale`. `audio_started_after_mute=0`. `media_bytes=0` | L13-11 |
| J-outage | Server stops mid-journey, then returns | Connecting versus ready follows LAB-07 rules. No false ready. | Stage trace |
| J-cycles | 10 full stage cycles | Timer and widget counts do not climb across cycles | High-water table |
| J-experimental | One labeled OPEN send or session branch | `branch=experimental`. `class=exploratory`. Observation recorded. Not an experiment input | L13-10 |

**Acceptance criteria.**

| ID | Level | Check | Expected |
|---|---|---|---|
| L13-01 | L1 | `test_lab13_fixture_hashes` | Vendored hashes equal the focused fixture revisions named in `lab13-journey/v1`. |
| L13-02 | L1 | `test_lab13_one_owner` | A background poller cannot set the stage. |
| L13-03 | L1 | `test_lab13_terminals` | Each scripted operation id appears with one terminal. |
| L13-04 | L1 | `test_lab13_bound_inbox` | Metadata count 256. `widget_live` equals 4. Source: `client/labs/lab-02-bounded-inbox/README.md` four reusable card views. |
| L13-05 | L1 | `test_lab13_pin_isolated` | Roster success during PIN does not clear digits. Source: EXP-LOGIN partial-PIN rules and coding standard section 3. |
| L13-06 | L2 | `make lab.lab13.scripted KIT=<kit>` | Actor: scripted run. Artifacts: `run-manifest.json` and post-reset `serial.txt` under evidence slug `lab-13`. Exactly one listening socket, on port 8772. `workflow_status` is `PASS` only when serial contains `LAB13_HARNESS_DONE status=complete` and cleanup succeeded. This row does not read `experiment_verdict`. |
| L13-07 | L3 | Delta table: UI tick, cancel time, largest free block, underrun count | Each cell names the focused run id, the unit, and `measured` or `UNMEASURED`, plus `source_experiment_verdict`. A missing sample or a source verdict of `UNMEASURED` stays `UNMEASURED`. The cell is not an `experiment_inputs` entry. No performance `PASS`. |
| L13-08 | L2 | Journey manifest `lab_id` | `LAB-13`. It does not overwrite `docs/labs/evidence/lab-02/` or other focused evidence directories. |
| L13-09 | L1 | Report parity sentence | Report line `OPEN branches are not product parity` and the id `J-experimental`. |
| L13-10 | L1 | `test_lab13_experiment_inputs` | `experiment_inputs` equals `["J-normal","J-pin-roster","J-push-snap","J-send-change","J-fetch-mute","J-outage","J-cycles"]`. `experiment_verdict` is `PASS` only when each included id has `terminal_count` 1 and the fields in the scenario table. `J-experimental` has `class` `exploratory`, trace `branch` `experimental`, and is absent from `experiment_inputs`, and `observation` is present. For each `lab_metrics.comparisons.<series>`, `source_experiment_verdict` equals the focused run manifest `experiment_verdict`. If that value is `UNMEASURED` or `INCOMPLETE`, or the focused scenario `class` is `exploratory`, the series id is not in `experiment_inputs`. A missing focused file sets that scenario's `verification` to `blocked` and does not synthesize a sample. Workflow `PASS` does not satisfy this row. |
| L13-11 | L1 | `test_lab13_fetch_mute` | The fetch `operation_id` has `terminal` `stale`, `terminal_count` 1, `audio_started_after_mute` 0, and `media_bytes` 0. |

**Negative, cleanup, cancellation.** L13-11. Queue overflow on the journey event queue (capacity 16, a provisional lab probe) is one `failure` `overflow`. Cleanup stops port 8772 and sets `media_bytes` to 0. A stage failure still finalizes a partial manifest with `workflow_status` `FAIL` or `INCOMPLETE` and does not set `experiment_verdict` `PASS`. `J-cycles` fails L13-10 when `timer_count` or `widget_live` increases across the 10 cycles, even if L13-06 is `PASS`.

**Must not claim.** End-to-end product approval, a performance pass, that LAB-03's incomplete acoustic evidence became complete, or that LAB-05 lifecycle runs were a peak measurement.

## 4. Implementation order

LAB-13 is last. Nothing waits on LAB-01.

| Order | Lab | Satisfying artifact from the earlier step |
|---|---|---|
| 1 | LAB-04 | No new-lab dependency. Corpus check is L04-11 against `server/labs/lab-03-media/fixtures/manifest.json`. Later send and journey steps may cite L04-09 `observation.encoded_bytes` and must not treat it as a chosen format. |
| 2 | LAB-05 peak | No dependency on L04-09. Produces L05-09 (performance `UNMEASURED`) and `docs/labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md` for the LAB-13 peak comparison. Lifecycle `lab05` stays runnable. L05-01 is the registry gate. |
| 3 | LAB-06 | Layout documents only. Produces L06-02 and L06-03. |
| 4 | LAB-07 | Decided login timings are already written. When L06-02 and L06-03 pass, LAB-07 links that owner. Produces L07-05 and L07-10. |
| 5 | LAB-08 | Needs L07-05 and L07-10. Produces L08-10. |
| 6 | LAB-11 | Needs LAB-10 `capture_policy` as read by L11-01. May cite L04-09 `encoded_bytes` when that report exists. Does not need L08-10. |
| 7 | LAB-09 | Needs L08-10. Does not need L09-02, which stays exploratory. |
| 8 | LAB-12 | When `layout` is `split-pane`, needs L06-03. Produces L12-10. Does not block LAB-09 or LAB-11. Placed here so S-20's LAB-12 report exists before LAB-13 records `settings_step`. |
| 9 | LAB-13 | Needs the mandatory implementation artifacts named in sections 3.9 and 8.2. Reports with `experiment_verdict` `UNMEASURED` can supply documented behavior; missing numeric samples leave comparisons blocked. L13-10 refuses to promote source verdicts. |

LAB-02, LAB-03, and LAB-10 stay in place and are not rebuilt by this order.

## 5. Decisions made in this plan

| Topic | Decision |
|---|---|
| LAB-01 | Out of scope. No section, no dependency, no baseline. |
| LAB-02, LAB-03, LAB-10 | Pattern only. Not implementation tasks. |
| LAB-05 peak | Second registry make name `lab05peak`, same `lab_id` `LAB-05`, same project `client/labs/lab-05-transition-proof`, evidence slug `lab-05-peak`, adapter `lab05peak`, Kconfig `CONFIG_LAB05_PEAK`. Lifecycle entry `lab05` stays `generic` and `UNMEASURED`. |
| Peak control | Variants `replace` and `scripted-replace`. Default scripted peak command stays `control=persistent`. |
| New lab IDs | None. Product IDs stop at LAB-13. |
| Registry `workflow` object | Not used by new labs. It is LAB-02-shaped. |
| Experiment verdict | New labs that classify results get their own allowlisted adapter and pass `execute`. They do not use `generic`. |
| Servers | Adapter-owned. Ports 8767 through 8772. Logs through `BoundedRedactingLog`. |
| Shared helper extraction | None. Call `run_with_adapter`, `EvidenceRun`, the allowlist, and `BoundedRedactingLog`. Parsers, scripted Kconfig symbols, Wi-Fi copies, and server supervision stay per lab. |
| Performance PASS | Not granted. Record p50, p95, p99, worst, and largest free block. |
| LAB-12 phases | Default make targets are visual-only. Shared profile runs only with `VARIANT=shared` or `VARIANT=scripted-shared`. |
| LAB-13 server | One process on port 8772. It vendors fixture hashes. It does not call the other lab ports during the scripted run. |
| Duplicate `lab_id` | Allowed only for `lab05` plus `lab05peak`. L05-01 is the test that fails before the validator change and passes after it. |
| `lab05peak` registration | No new Makefile pattern. Schema `enum` gains `lab05peak`. `_ADAPTER_MODULES` gains `"lab05peak": "lab_adapter_lab05peak"`. Identity is `LabSpec.adapter_key`, not the config snapshot. |
| Scripted variant build | `make lab.<name>.scripted.build` always passes `--variant scripted`. Replace and shared builds use `make lab.<name>.build VARIANT=<key>`. |
| Exploratory rows | `lab_metrics.scenarios.<id>.class` is `exploratory`. Those ids are absent from `experiment_inputs`. The report still copies `observation`. |
| LAB-12 phase | `capture.experiment_phase` is `visual` or `shared`. A top-level `experiment_phase` key is not a manifest field. |
| Workflow versus experiment | Each lab has a workflow criterion and a separate experiment criterion. Workflow `PASS` does not satisfy the experiment row. |

## 6. Remains OPEN

These are product or contract decisions. Labs in this plan record evidence and must not close them.

| Item | Where it stays open |
|---|---|
| Inbox page size, changing-inbox consistency, older-message navigation, focus outside the loaded page | `docs/product/communication-contracts/inbox-and-media.md` `COM-OPEN-002` |
| Media format, range, cache, offline playback | inbox/media `COM-OPEN-003`; sketch `OPEN-SKP-03`; message-delivery `COM-OPEN-002` |
| Resume, seek, and checkpoints as product behavior | Inbox plan decision table. LAB-04 measures options only. |
| Frame, callback, node, and byte budgets | `FOUNDATION-LVGL-BUDGET.md`: no approved firmware budget numbers |
| Health and roster wire fields, stale clock, outage policy | `endpoint-health-and-login.md` `COM-OPEN-001`–`COM-OPEN-005` |
| PIN wire fields, concurrent keys, retention start, late success after mute on the server | `COM-OPEN-006`–`COM-OPEN-009` |
| Session credential issuance, transport, and error mapping | `user-scoped-envelope.md` `COM-OPEN-001`–`COM-OPEN-003` |
| Atomic recipients, outbox, receipt retention, part transport, `Sent` meaning | `message-delivery.md` `COM-OPEN-001`–`COM-OPEN-005` |
| Message color, settings set, avatar catalog, optimistic profile save | `user-profile.md` `COM-OPEN-001`–`COM-OPEN-005` |
| Placeholder screens and their copy | `docs/product/device-ui/screens/MISSING.md` |
| Cover-down and split-pane visual approval | Those pattern documents are drafts |
| Recording timer and recording indicator | Sketch experience `OPEN-SKP-05` |
| Per-swatch wire RGB565 | `OPEN-SKP-01` |

A lab result that answers a feasibility question still leaves the decision row **OPEN** until the owner changes the experience or contract through that document's own authoring path.

## 7. Independent review

Date: **2026-10-19**.

Reviewer boundary: read-only on firmware, servers, product experiences, and contracts. The only edited file is this plan. No firmware or server implementation, no commit, and no change under the v1 POC archive.

The criteria below were checked against `docs/standards/client-application-coding-standards.md`, `docs/standards/labs-configuration.md`, `docs/standards/client-code-review-agent.md`, `docs/AGENTS.md`, the host registry and its schema, the lab runner and adapters, and the LAB-02, LAB-03, LAB-05, and LAB-10 trees as they are registered today.

### Findings

| Id | Severity | What was wrong | What changed |
|---|---|---|---|
| R-01 | defect | `make lab.lab05peak` is already matched by the Makefile `lab.%` pattern. It cannot be registered until the schema `enum` and `_ADAPTER_MODULES` include `lab05peak`. `validate` does not reject a duplicate `lab_id`. `make config.labs` omits `adapter`, so a "registry dump" cannot show the adapter key. The plan described the allowlist as a `lab_registry.py` semantic check. | Section 1.1 and L05-01 name the enum edit, the allowlist edit, and the uniqueness edit, with the test that fails before each edit and passes after it. The check reads `LabSpec.adapter_key`. |
| R-02 | defect | `make lab.<name>.scripted.build` always passes `--variant scripted` and does not forward `VARIANT`. The peak replace build had no command that selects `scripted-replace`. | Section 1.3 and L05-03 name `make lab.lab05peak.build VARIANT=scripted-replace`. |
| R-03 | defect | The host rejects `BACKEND` only when `adapter.run` returns `None`. New adapters return an exit code, so they must reject it themselves. They also do not inherit the generic scripted-variant expression. | S-21. Section 1.5 requires the generic expression, so an unset `lab05peak` scripted run stays `control=persistent`. |
| R-04 | defect | `experiment_phase` is not in `EvidenceRun._SAFE_FIELDS`. A top-level manifest field would be dropped. | LAB-12 uses `capture.experiment_phase`. L12-01 asserts a top-level key is absent. |
| R-05 | defect | S-16 omitted `max_stack_or_call_depth`, which coding-standard section 8 includes in `ResourceBudget`. | S-16 lists that field. Values stay a provisional lab probe. |
| R-06 | defect | L04-05, L06-05, L07-08, L08-07, L09-04, L11-06, L12-06, L12-07, and L13-06 let one row treat workflow `PASS` as the experiment result. | Each of those labs now has a workflow row that does not read `experiment_verdict`, and a separate experiment row (L04-09, L05-11, L06-09, L07-12, L08-11, L09-09, L11-10, L12-10, L13-10). |
| R-07 | defect | Exploratory scenarios were "labeled" in prose. Format round-trip, the proposed actor rule, and the send-acceptance branch could be read as experiment `PASS`. L09-02 treated a proposed envelope as a pass/fail product rule. | Section 1.6 and S-22 define `lab_metrics.scenarios.<id>.class` and `experiment_inputs`. Exploratory ids stay in `observation` and out of the verdict. L09-02 records the fixture counter and does not set experiment `PASS`. |
| R-08 | defect | Cancellation and resource release were narrative. They did not require exactly one terminal at a stated level. | L04-10, L05-12, L06-08, L07-11, L08-10, L09-08, L11-09, L12-09, and L13-11. |
| R-09 | defect | Section 4 did not name the artifact that satisfies each dependency. LAB-08 cited a stage enum that was not a criterion. LAB-09 cited `test_lab08_mask`, which does not prove mute. LAB-13 could copy an `UNMEASURED` or exploratory result into a pass. LAB-04's corpus hashes were not a check. | Section 4 and section 3.9 cite criterion ids. L07-10 is the stage-token artifact. LAB-09 depends on L08-10. L04-11 checks the two fixture hashes. L13-10 keeps source verdicts and blocks missing files without synthesizing a sample. |
| R-10 | defect | L06-01 allowed a different `CONFIG_LV_MEM_SIZE` when the report called it an unapproved experiment. That is not a comparison. | L06-01 requires the built value 65536. |
| R-11 | note | The LAB-07 and LAB-11 product questions used "truthful" and "responsive" as the thing to be shown. The acceptance rows did not define those words. | The questions now name the fields the criteria already measure. No product budget was added. |

### Structural decisions closed

| Decision | Criterion that tests it |
|---|---|
| `lab05peak` needs a schema enum entry, an allowlist entry, and a uniqueness check. It does not need a new Makefile pattern. | L05-01 |
| Replace and other non-`scripted` variants are built with `make lab.<name>.build VARIANT=<key>`. | L05-03 |
| New adapters copy the generic variant expression and reject `backend=local` themselves. | S-21 |
| Exploratory rows use `class=exploratory`, stay out of `experiment_inputs`, and still store `observation`. | S-22, and each lab's experiment-input criterion |
| Workflow `PASS` and experiment `PASS` are separate criteria. | The workflow and experiment rows named in R-06 |
| LAB-12 phase is `capture.experiment_phase`. | L12-01, L12-06, L12-07 |
| `ResourceBudget` includes `max_stack_or_call_depth`. | S-16 |
| Peak cancel compares `live_image_count` to the README integer `steady_live_images`. This plan does not choose that integer. | L05-12 |
| Offline work and rejected-preview policy are fixture fields, not an implementer choice of product policy. | L11-10 (`D-offline`), L12-10 (`P-save-policy`) |
| Settings layout is `fixtures/layout.json` `layout`, not a product chrome decision. | L12-10 (`P-layout`) |
| The proposed actor rule is an observation, not an experiment pass. | L09-02, L09-09 |

### Product items that remain OPEN

Section 6 is unchanged in substance. This review did not choose wire formats, copy, screen designs, session-token protocols, revocation, `Sent` meaning, chunk size, or a performance budget. Cover-down 400 ms and the 64 KiB LVGL pool remain the motion and the compile default under test, not pass limits. Queue capacities named in the lab sections are provisional lab probes.

### Residual gaps

These are measurements the labs still have to collect. They are not missing plan checks.

- Device distributions (p50, p95, p99, worst, and largest free block) stay `UNMEASURED` until an L3 run records them. This plan sets no pass threshold for those series.
- The existing LAB-05 lifecycle runs stay workflow `PASS` and experiment `UNMEASURED`, with no `LAB05_HARNESS_DONE`. L05-05 forbids treating them as the peak experiment.
- The published LAB-03 performance verdict remains `INCOMPLETE`. L13-10 does not complete it.
- L1 virtual-clock tests do not establish device timeout jitter. The L3 rows record the device sample and do not tighten the decided product timeout.

## 8. Execution contract for one selected lab

This section turns sections 1–6 into assignable packages for the [orchestrate-build skill](../../.agents/skills/orchestrate-build/SKILL.md). Section 7 records an earlier document review; it is not an implementation or verifier verdict. The execution contract was added on **2026-10-19** after checking the current registries, Makefile, runner/evidence APIs, existing lab trees, fixture manifest, and LAB-10 capture policy. Recheck those inputs at dispatch; this document is not a live inventory.

Repository readiness at this review (file/code inspection, not runtime qualification):

| Area / requirement group | Present state | Required delta / owner |
|---|---|---|
| [Registry](../../config/labs.registry.yaml), [schema](../../config/labs/labs.registry.schema.v1.json), [adapter allowlist](../../scripts/lab_adapter_registry.py) | **Present:** lab02/lab03/lab05/lab10. **Missing:** all registrations/adapters in this plan. Uniqueness validation remains missing. | Each HOST adds its own entry/enum/adapter. L05P-HOST alone owns the narrow uniqueness rule. |
| [Shared runner](../../scripts/lab_runner.py), [evidence writer](../../scripts/lab_run_evidence.py), [log helper](../../scripts/server_log.py) | **Present:** APIs described in section 1. Their existence does not verify a new adapter. | Call unchanged from HOST; independently test new lifecycle and verdict logic. |
| LAB-04/06/07/08/09/11/12/13 trees and lab-specific S-*/LNN-* outcomes | **Missing:** selected project trees and their planned models, firmware, adapters, and qualification evidence. | The selected row's MODEL/FW/HOST/QUAL packages; DEVICE for target evidence. |
| [LAB-05 lifecycle](../../client/labs/lab-05-transition-proof/manifest.lab.json) | **Partial for peak:** lifecycle project exists, but does not implement or measure the peak addition. | L05P packages add the guarded peak path while preserving lifecycle behavior. |
| [LAB-03 corpus](../../server/labs/lab-03-media/fixtures/manifest.json), [LAB-10 capture policy](../../client/labs/lab-10-record-canvas/manifest.lab.json) | **Present:** both sketch asset lengths/hashes and the 4096/180000/16 capture bounds match the cited inputs. | Revalidate in L04-MODEL/L11-MODEL; these are data prerequisites, not performance passes. |
| Toolchain, named kit, network/serial access, synchronized capture | **Unknown for a future run:** not exercised in this document review. | FW/HOST must prove actual builds. DEVICE preflight resolves target access and required recording. |

Strengths retained from the original plan are its stable scenario/acceptance IDs, explicit OPEN boundaries, separate workflow/experiment verdicts, and concrete control fixtures. Existing runner APIs and lab patterns supply the host structure; the packages below supply the previously missing assignment and independent-verification boundaries.

### 8.1 Selection, readiness, and scope

- Select exactly one of `LAB-04`, `LAB-05-peak`, `LAB-06`, `LAB-07`, `LAB-08`, `LAB-09`, `LAB-11`, `LAB-12`, or `LAB-13`. `LAB-05-peak` is an execution selector; the product `lab_id` remains `LAB-05`. Resolve a bare `LAB-05` to this peak addition and state that resolution.
- Default `execution_scope=software`: select that lab's four packages `MODEL → FW → HOST → QUAL`. `execution_scope=device` selects its `DEVICE` package after current software passes; `execution_scope=full` selects all five. Never select every lab merely because this map covers them all.
- Default progress path: `docs/labs/evidence/<evidence_slug>/orchestration-progress.md`. Use `lab-05-peak` for peak. Do not reuse `docs/labs/orchestration-progress.md`, which belongs to other work. Follow the skill's owned-section, unique-run, collision, and append-history rules.
- Read `docs/AGENTS.md`, `docs/README.md`, sections 1–2 and 4–8 here, the entire selected section 3.N, its linked applicable experience/UI/contract sections, `docs/standards/client-application-coding-standards.md`, `docs/standards/labs-configuration.md`, and `docs/labs/repository-layout.md`. The selected brief is self-contained only together with those resolvable sources. Do not implement from the package table alone.
- Before launch, record HEAD, staged/unstaged/untracked paths, and relevant ignored build/evidence state without printing secrets. Protect preexisting work. This plan may itself be an untracked owner-authored input: read and fingerprint it, and exclude it from all agent edits. Its untracked status alone is not a scope overlap. An overlapping change in a file an agent needs to edit requires an ownership decision; never reset, stash, or overwrite it.
- Freeze the selected lab section, common requirements, package briefs, dependency choices, source revisions, and content fingerprints in the progress record. Later packages may edit earlier package outputs within the selected lab, so reverify affected acceptance conditions and refresh their evidence before relying on an earlier pass. A historical pass without current content/evidence is `stale`.
- Present the resolved package agenda and prerequisite evidence. Explicit authorization to run now and continue through this lab's packages satisfies the skill's agenda and inter-package checkpoints. Otherwise use the skill's approval defaults. No commits, pushes, PRs, merges, publication, product promotion, or changes to OPEN decisions are authorized by selecting a lab.

**Readiness rule.** The plan is ready to dispatch when the selected package's hard prerequisites below have current evidence, its source references resolve, its file scope is clear, and separate agents with fresh verifier context are available. Missing hardware does not prevent `MODEL`, `FW`, `HOST`, or `QUAL`; it prevents `DEVICE`. Missing ESP-IDF prevents a passing `FW` build and must be reported, not replaced by host-model success. A missing hard prerequisite blocks only affected packages; do not silently implement prerequisite labs or add a parallel session owner.

### 8.2 Hard prerequisites versus optional reuse and comparisons

The order in section 4 is a suggested cross-lab order, not a chain of dependencies. For packages within a selected lab, use section 8.3. For artifacts from another lab, obtain current independent verifier evidence tied to its source fingerprint, or dispatch a fresh read-only prerequisite verifier to inspect the files and rerun the named checks. A file's existence, an old progress row, or an experiment verdict alone is insufficient. Prerequisite verification does not authorize editing that other lab.

| Selected lab | Hard prerequisite before MODEL | Optional or conditional context; required disposition |
|---|---|---|
| LAB-04 | Corpus A manifest **and actual asset bytes** satisfy L04-11; hash the files as well as checking manifest declarations. | LAB-03 performance status does not gate work. Corpus B is new synthetic data, not a product format decision. |
| LAB-05-peak | Existing lifecycle manifest, variants, markers, and `lab05` registration match section 3.2; record their baseline. | No new-lab dependency. Published lifecycle evidence is read-only reference material. |
| LAB-06 | Layout, panel, transition, and coding-standard sources resolve with the authority/status stated in section 3.3. | Draft visuals remain exploratory. No earlier lab result is required. |
| LAB-07 | Decided login timing sources resolve. | If the LAB-06 module and current L06-02/L06-03 passes exist, link that owner read-only. Otherwise freeze `stage_owner=lab07-local` before implementation. Do not switch ownership mid-run without rechecking affected packages. No LAB-06 performance gate. |
| LAB-08 | L07-05 and L07-10 pass against current LAB-07 source; its reusable owner/header and dependencies are available. | Do not create another writer or wait on LAB-07 performance. |
| LAB-09 | L08-10 passes against current LAB-08 source and the reusable cancellation path is available. | No dependency on exploratory credential/revocation outcomes. |
| LAB-11 | LAB-10 `capture_policy` matches the fields in section 3.7; L11-01 will validate the new frozen file against it. | LAB-04 byte observations are optional; absent them use an opaque synthetic payload. Neither LAB-10 nor LAB-04 performance verdict gates work. |
| LAB-12 | For a `split-pane` fixture arm, current L06-03 and its geometry artifact must exist. The `full-canvas` arm needs no LAB-06 module. | Freeze the tested layout arm(s) in MODEL and record the choice as exploratory. Missing split-pane prerequisite blocks that arm, not a full-canvas run; report the omitted arm. Both visual and shared-profile implementations are in software scope. |
| LAB-13 | Current LAB-06 L06-02/L06-03, LAB-07 L07-01/L07-02/L07-03, LAB-08 L08-10, LAB-09 L09-08/L09-09, LAB-11 L11-08/L11-10; their reports; LAB-02 live-view contract; LAB-03 ordering report; LAB-10 capture policy. | Missing modules/contracts for mandatory journey steps block implementation. Missing focused numeric samples (including LAB-05 peak) block only the corresponding comparison cells. LAB-04 format observations are optional. Settings may be omitted; if included, require LAB-12 L12-10 and its report. Record `settings_step`. No performance pass is required. |

For LAB-13, distinguish the required journey scenario from its optional focused comparison. All seven decided `J-*` scenarios remain implemented and tested. A missing comparison file sets that comparison's `verification=blocked`; it does not remove `J-fetch-mute`, `J-send-change`, or another decided scenario from `experiment_inputs`. Missing evidence for the journey scenario itself prevents its functional pass. Reports without numeric runs satisfy only the documented-behavior prerequisite, not a measured comparison.

### 8.3 Stable package IDs and build order

Package IDs below are new orchestration IDs. Existing `L04-01`…`L13-11` and `S-01`…`S-22` remain acceptance IDs; do not submit them as package IDs. Package definitions are the common phase brief in section 8.4 **plus** the selected row in section 8.5 **plus** the selected lab's section 3.N and applicable common criteria. These are inherited requirements, not optional guidance.

| Selector / section | MODEL | FW (depends on MODEL) | HOST (depends on FW) | QUAL (depends on HOST) | DEVICE (depends on QUAL; separately selected) |
|---|---|---|---|---|---|
| LAB-04 / 3.1 | `L04-MODEL` | `L04-FW` | `L04-HOST` | `L04-QUAL` | `L04-DEVICE` |
| LAB-05-peak / 3.2 | `L05P-MODEL` | `L05P-FW` | `L05P-HOST` | `L05P-QUAL` | `L05P-DEVICE` |
| LAB-06 / 3.3 | `L06-MODEL` | `L06-FW` | `L06-HOST` | `L06-QUAL` | `L06-DEVICE` |
| LAB-07 / 3.4 | `L07-MODEL` | `L07-FW` | `L07-HOST` | `L07-QUAL` | `L07-DEVICE` |
| LAB-08 / 3.5 | `L08-MODEL` | `L08-FW` | `L08-HOST` | `L08-QUAL` | `L08-DEVICE` |
| LAB-09 / 3.6 | `L09-MODEL` | `L09-FW` | `L09-HOST` | `L09-QUAL` | `L09-DEVICE` |
| LAB-11 / 3.7 | `L11-MODEL` | `L11-FW` | `L11-HOST` | `L11-QUAL` | `L11-DEVICE` |
| LAB-12 / 3.8 | `L12-MODEL` | `L12-FW` | `L12-HOST` | `L12-QUAL` | `L12-DEVICE` |
| LAB-13 / 3.9 | `L13-MODEL` | `L13-FW` | `L13-HOST` | `L13-QUAL` | `L13-DEVICE` |

These are linear dependencies within each row; there are no implicit dependencies on other package rows. External artifact prerequisites are exactly section 8.2. A later package may refine its own lab's earlier implementation, but may not widen the experiment or silently invalidate an earlier verification.

### 8.4 Common implementer and independent-verifier briefs

**MODEL — bounded domain model, fixtures, and experiment contract.**

- **Implementer:** Create the selected project scaffold, README, manifest, trace contract, deterministic fixtures, pure domain components, and native/model tests from section 8.5. Declare resource bounds, operation envelopes, writer/worker ownership, wait map, update levels, and scenario-class/expected-terminal inventory before device implementation. Implement the model's success and failure behavior, not a second test-only algorithm. Networked labs also implement their synthetic fixture server, health identity, versioned data, and server unit tests. Freeze any permitted probe choice (queue/resource bounds, layout arm, offline/reject-preview/acceptance branch) in a versioned fixture or README; test the branches the lab says to compare. All are lab probes, never product approval.
- **Scope:** selected firmware tree, selected server tree if any, and focused tests under `scripts/tests/` to expose native/model/server checks to root discovery. For LAB-05 peak, make only the additive files/manifest fields allowed in section 3.2; preserve lifecycle fields and baseline. Do not register an unfinished lab, edit shared runner code, or write a measured report.
- **Verifier:** Independently compile/run the native model and server tests, inspect the actual production model source, recompute fixture hashes, and cross-check decided/exploratory classification against section 3.N. Prove the listed MODEL acceptance conditions and S-02, S-08, S-09, S-10, S-16, and S-17 where applicable. A Python reimplementation or assertions about source strings alone do not prove the C behavior. Record exemptions with reasons. No L0 device-build or L2/L3 claim is required at this phase.

**FW — device integration and instrumentation.**

- **Implementer:** Connect MODEL components to the real ESP-IDF/LVGL/task/input/network paths needed for this lab. Implement every selected scenario's device harness, manual controls, fail-closed ready/completion markers, bounded worker publication, trace counters, two resource ledgers, and cleanup. Provide all prescribed sdkconfig variants and isolated build directories. Use current LAB-03/LAB-10 build/toolchain patterns, with explicit sdkconfig defaults and a separate output directory per variant. Before host registration, use the configured ESP-IDF `idf.py` entry point directly against the lab project; record the exact command/environment resolution and resolved sdkconfig for reproducibility. Root `make build` is never a substitute.
- **Scope:** selected firmware tree and its tests/fixtures only. Existing focused modules may be linked unchanged or copied/namespaced where this plan permits it. No edits to their source, no shared abstraction extraction, and no registry/adapter edits yet.
- **Verifier:** Run independent ESP-IDF builds for the prescribed variants, verify scripted/phase/control Kconfig values, inspect callback/worker ownership and actual trace emission, and rerun the model tests against the integrated source. Inspect the compiled failure and cleanup paths. Prove the FW conditions in section 8.5 and firmware portions of S-06–S-09, S-13, S-16–S-19. S-01's make command and S-06's registry build-directory check are completed in HOST; direct builds here do not waive them. Device timing and visual/audio claims remain unmeasured.

**HOST — parser, runner adapter, registration, and reproducible builds.**

- **Implementer:** Implement the lab-owned parser and its malformed/missing/duplicate/overflow trace fixtures; add its allowlisted adapter with `execute`, matching markers, named deadlines, backend rejection, variant selection, service ownership, bounded/redacted logs, cleanup, and partial evidence. Use `EvidenceRun` through `run_with_adapter`. Add only this lab's registry block, schema enum member, allowlist entry, and focused tests. For LAB-05 peak include the explicit uniqueness change below. If useful, add the one detached-server make target named in section 3.N; do not add another generic runner. Run registry/help checks and every required variant build through the real make entry point. Resolve build/integration defects only within this lab's scope.
- **Scope:** selected tree's tools/tests and necessary integration fixes; new `scripts/lab_adapter_<name>.py`; focused `scripts/tests/`; surgical edits to `config/labs.registry.yaml`, its schema, `scripts/lab_adapter_registry.py`, and the optional explicit Makefile server rule. Shared runner/evidence API redesign and `config/host.defaults.ini` edits are excluded. If an existing API cannot meet a requirement, report the concrete blocker before expanding scope.
- **Verifier:** Rerun parser and adapter tests including real temporary fixture-server lifecycle where applicable, build each registered variant through make, and inspect the bounded changed-file set. Prove S-01–S-07, S-10–S-14, S-21–S-22 at their achievable levels, and all HOST conditions in section 8.5. Mocked reset/serial tests prove L1 fail-closed logic, not the lab's on-target L2 row. Test parser success with failed cleanup, pre-reset markers, missing harness markers, unsupported backend, variant/phase mismatch, and missing decided samples. None may manufacture a passing device run.

**QUAL — regression, standards review, and software qualification report.**

- **Implementer:** Complete any remaining failure, overflow, cancellation, stale-result, repetition, and cleanup coverage from sections 2–3. Ensure all new host/native/server checks are discoverable from `make test`; lab-local tests must not be silently skipped by root discovery. Publish the section 3.N report with an acceptance ledger covering every applicable S-* and every selected LNN-* row, exact commands and artifacts, source/build/fixture identity, L0–L3 achieved, OPEN decisions, and honest device blockers. Add an evidence-index row labeled software-qualified, with hardware pending unless real evidence exists. Do not create fake run directories, serial logs, or empty metrics presented as observations.
- **Standards review substep:** Once the candidate is stable, the orchestrator runs the client-code-review workflow in review mode against the selected firmware tree and its dependencies. Use a fresh read-only reviewer and saved source fingerprint. An implementer's self-review does not satisfy this gate. Confirmed in-scope defects go to an implementation agent, then a fresh independent verifier, sequentially; the run's repair cap applies and cannot reset through nested review. The orchestrator writes only its configured progress file; agents return structured reports there, and the implementer places the resulting qualification references in the lab report. Do not delegate a second orchestrator that concurrently edits or exceeds the repair budget.
- **Scope:** selected lab's implementation/tests and evidence report/index; standards fixes limited to that scope. Shared-file defects outside HOST's allowed edits require a scope decision. Recheck MODEL/FW/HOST conditions affected by qualification changes.
- **Verifier:** Independently rerun focused native/host/server tests and registry validation, inspect the HOST-verified build evidence against the current compiled-input fingerprints, and rebuild any variant whose inputs changed or whose evidence cannot be checked. Run the full `make test` suite against the final software state; if a later repair changes code or checks covered by the suite, rerun it. Reconcile the acceptance ledger, standards review findings, and current source fingerprint. Check S-18–S-20, all remaining L1 conditions, and all prior package regressions. Verify that unavailable L2/L3 evidence is marked `blocked`/`UNMEASURED` and that the report cannot be mistaken for an experiment pass. A QUAL `pass` means software qualification at L0/L1 only, with every deferred criterion listed; it does not pass the DEVICE package.

**DEVICE — target execution and evidence qualification.**

- **Implementer/operator:** Require explicit selection, a named kit/serial binding, flash/reset authorization, toolchain, synthetic fixture/network access, and the screenshot/audio capture needed by the intended claims. Require a current QUAL pass and standards review before capture. Run the exact section 3.N make commands, all required controls/phases, and representative failures/repetitions. Preserve real manifest/events/serial/metrics and external capture. Update the report/index with per-scenario L2/L3 evidence and functional versus performance outcomes. Do not edit source to make a failed measurement appear passing; return defects for a bounded implementation repair and requalification.
- **Verifier:** In a fresh read-only invocation, check the actual raw files and identities, rerun the parser, independently reconcile operation counts and verdicts, inspect screenshots/recordings or cited operator observations, and verify every applicable L2/L3 row and S-07, S-11–S-15, S-20 at its required level. Missing or unsynchronized required evidence is `blocked`; unexpected behavior is `fail`. A completed measurement may correctly retain performance verdict `UNMEASURED` because no budget was approved. Never interpret that as a missing numeric sample if a real measurement is present.

**Common exclusions for every package:** plan/spec/standard edits; `client/device/`; bootstrap artifacts; historical archive; unrelated lab source or evidence; `client/shared/` extraction; real household/account data; implicit commits or publication. Reading and unchanged linking to specified prerequisite artifacts are permitted. Normal ignored build/test outputs are permitted only in owned output directories; preexisting outputs remain protected.

### 8.5 Per-lab package deltas and acceptance ownership

Each row is an implementer brief and its separate verifier brief, supplementing section 8.4. Every selected lab's `DEVICE` package owns its section 3.N L2/L3 rows in full; software packages implement and host-test the corresponding mechanisms without claiming those levels. All labs also inherit all S-* rows; server/stage-only rows require an explicit applicability note rather than silent omission.

| Package | Implementer outcome / owned acceptance conditions | Independent verifier must additionally prove |
|---|---|---|
| L04-MODEL | Three encoding/decoding strategies, checkpoint/legacy arms, corpus A/B identity, bounded format/raster model. L04-02–L04-04, L04-10–L04-11. | Same drawing semantics, colors and pen-up boundaries; all corpus bytes match; malformed/oversized inputs cannot silently truncate; cancellation releases buffers. |
| L04-FW | Device encode/decode/raster/seek harness and manual strategy selection; emit L04-06–L04-07 series and preview-buffer counts. | Real model is linked and exercised; one 28,800-byte preview storage bound is respected; all variants/scenarios emit identity and missing-data diagnostics. |
| L04-HOST | `lab04` adapter/parser/registry; L04-01, L04-09 and L04-05 mechanism. | Exact decided-input set; exploratory encoded byte observations remain available; injected cleanup or malformed evidence cannot yield a false pass. |
| L04-QUAL | L04-08 report and all LAB-04 regression/fault checks. | No selected format or performance approval; all 11 L04 criteria have a disposition. |
| L05P-MODEL | Additive peak manifest/fixtures/model; fixed images, synthetic workers, handle generations. L05-02, L05-08, L05-12. | Lifecycle manifest fields unchanged; no socket or LAB-02 dependency; cancel returns to the declared steady image count. |
| L05P-FW | Conditional peak component and persistent/replace arms; 20 repetitions each; L05-03–L05-04 and L05-09–L05-10 emitters. | Build all lifecycle and peak variants; lifecycle link/markers remain unchanged; peak count/byte accounting includes simultaneous images. |
| L05P-HOST | `lab05peak` adapter/parser/registry, narrow uniqueness gate. L05-01, L05-11 and L05-05–L05-07 mechanisms. | Both schemas/allowlist resolution and lifecycle preservation; test negative registry documents, wrong slugs/adapter/project identity, and default persistent selection. |
| L05P-QUAL | Peak report, interruption/fault/repeat checks, lifecycle regression. | Existing lifecycle evidence untouched and never scored as peak; device reruns L05-05–L05-07 remain pending without hardware. |
| L06-MODEL | Stage reducer, geometry, slot ownership, panel-generation model. L06-02–L06-04, L06-08. | Second writer rejected; idempotent entry; callbacks after destroy and data during exit cannot mutate old handles. |
| L06-FW | Slot mounter, six synthetic panels, 20 cycles/stage, bounded data updates and 400 ms probe; L06-01 build configuration. | 65536 LVGL pool; stable shell; Level 2 only for stage changes; real timer/widget counters and generation invalidation. |
| L06-HOST | `lab06` integration; L06-09 and L06-05/L06-07 mechanisms. | Forced Level 2 negative control is a detected expected failure; placeholder observation stays exploratory; repeated operations have distinct ids. |
| L06-QUAL | Report with placeholder OPENs, cycle/cancel/overflow regression. | No visual approval or fabricated frame budget; expose reusable single-writer/geometry artifacts for LAB-07. |
| L07-MODEL | Login-local or linked owner as frozen in preflight; virtual-clock pollers and fixture server. L07-01–L07-05, L07-10–L07-11. | Timing ticks, one retry, invalid-body rejection, stage tokens and mute release; health does not become a second user-stage writer. |
| L07-FW | Two independent pollers, bounded queues, eight-user surface, held-response input probe, shortened real-time harness. | Real 1-second request timeout; touch remains observable during wait; declare which long schedules remain L1-only. |
| L07-HOST | `lab07` integration on 8767; L07-12 and L07-06 mechanism. | Run/fixture/server hashes match; process stays owned until cleanup; exploratory JSON/clock fields cannot affect decided verdict. |
| L07-QUAL | L07-09 report, outage/mute/overflow/child-cleanup regression. | Required OPEN lines; current reusable owner/header evidence for LAB-08; no L1-to-L3 timing promotion. |
| L08-MODEL | Reuse LAB-07 owner/header; PIN reducer, retry/cooldown/idempotency fixture. L08-01–L08-06, L08-09–L08-10. | Four exact attempts and delays; lost-response side effect stays one; BOOT/idle/user-switch rules and masked/error logging. |
| L08-FW | Keypad masking/checking/wrong/cooldown UI and on-device lost-response/mute paths. | Real input gating and display power path; no second writer, raw PIN trace, or PIN-derived correlation hash. |
| L08-HOST | `lab08` integration on 8768; L08-11 and L08-07 mechanism. | Fixture counters are checked, not asserted by client; concurrent/mismatch/server-after-mute observations excluded from verdict. |
| L08-QUAL | PIN report and result-queue/retry/mute regression. | Local mute terminal/release artifact available for LAB-09; no token issuance or retention policy approval. |
| L09-MODEL | Four operation kinds, credential fixture, generation/cancel isolation. L09-01–L09-03, L09-08. | All four kinds reject cross-user/stale completion; actor observation remains exploratory; no duplicate terminal. |
| L09-FW | Device signed-in probe, bounded cache/image, user switch/expiry/mute paths. | Real worker completion cannot restore a stage or publish A's media to B; trace supports L09-05 without claiming a budget. |
| L09-HOST | `lab09` integration on 8769; L09-09 and L09-04 mechanism. | Actor/revoke/HTTP-status observations retained separately; reset/cleanup failures preserve honest evidence. |
| L09-QUAL | L09-06–L09-07 report/security checks and operation-matrix regression. | No raw fixture credentials in evidence; no issuance/revocation/error mapping approval. |
| L11-MODEL | Frozen capture/hash, bounded part/send/receipt model and fixture branches. L11-01–L11-03, L11-05, L11-09. | Frozen payload matches LAB-10 shape; duplicate part identity conflict; lost receipt does not accept twice; storage failure releases buffers. |
| L11-FW | Real part queue/send owner, Level 0 progress, two chunk probes, all four cancel phases. L11-04. | Frozen recipients/bytes survive data updates; terminal cardinality per cancel phase; restart/offline behavior is a labeled fixture probe. |
| L11-HOST | `lab11` integration on 8770; L11-10 and L11-06 mechanism. | Both acceptance fixture values preserve condition-dependent truth; no exploratory recovery/recipient/push branch becomes a decided verdict. |
| L11-QUAL | L11-08 report, send fault/repeat/cleanup matrix. | Condition copied with OPEN line, no approved `Sent` or outbox/format claim; no synthetic hardware capture claim. |
| L12-MODEL | Both visual/shared models, explicit layout fixture arm(s), two-endpoint profile server and preview/offline fixtures. L12-02–L12-05, L12-09. | Rejected save never presented saved; stale revisions cannot overwrite; focus, modal cleanup and stroke/accent isolation. |
| L12-FW | Visual/manual/scripted and shared/scripted-shared firmware paths, preview/modal/inbox preservation. | Four builds; visual path needs no server; split-pane arm honors its dependency; preview/reject policy remains a probe. |
| L12-HOST | `lab12` integration, 8771 only for shared; L12-01, L12-10 and L12-06–L12-07 mechanisms. | Default scripted is visual with no socket; kept `capture.experiment_phase`/`capture.variant`; exact phase-specific input sets and matching marker. |
| L12-QUAL | One report with two phase sections; settings failure/repeat/cleanup tests. | Software qualification covers both phases even though default run is visual; omitted layout arm and all five profile OPENs visible. |
| L13-MODEL | Compose mandatory focused rules in one owner, frozen fixture hashes, optional settings choice. L13-01–L13-05, L13-11. | Prerequisite artifacts current; seven mandatory scenarios stay present; 256 metadata entries yield four live views; PIN and frozen-send identity isolated. |
| L13-FW | One-shell journey and 10-cycle device harness; one integrated server on 8772. | No proxy listeners on other lab ports; no second owner; real timer/widget/media counters and mute audio-start suppression. |
| L13-HOST | `lab13` integration; L13-10 and L13-06/L13-08 mechanisms, focused comparison reader. | Source verdict/hash/run identity preserved; absent comparisons blocked without deleting mandatory journey scenarios; no fabricated numeric deltas. |
| L13-QUAL | L13-09 report with per-scenario ledger, focused deltas, settings disposition, cycle/fault regression. | `OPEN branches are not product parity`; no promotion of focused incomplete or exploratory results into functional/performance approval. |

**LAB-05 registry fixture detail.** L05-01's before/after assertions use frozen old-schema/old-allowlist fixtures or isolated test doubles; never temporarily remove live keys or edit the working registry to simulate history. Enforce unique `evidence_slug` across all entries and unique `lab_id`/`project_dir` except the exact `lab05`/`lab05peak` pair specified there. Other registrations and later new enum members must remain valid. This semantic validation is owned by L05P-HOST; labs implemented before it do not invent their own uniqueness refactor. Once it lands, later registration tests must preserve it.

### 8.6 Commands, evidence, and missing environments

Current root host tests use Python **unittest**, not an assumed pytest suite:

```text
.venv/bin/python -m unittest discover -s scripts/tests
.venv/bin/python scripts/lab_registry.py validate
make config.labs
make lab.help
make lab.<name>.help
make lab.<name>.build
make lab.<name>.scripted.build
make test
```

Replace `<name>` from section 3.N. These commands exist today; the new lab targets become usable only after HOST registration. Use the repository-configured Python if `.venv/bin/python` is unavailable, and record the resolution. Native C test compile commands and new focused unittest module names are implementation outputs: MODEL documents the exact commands in the lab README and hooks them into `scripts/tests/`. Do not claim a proposed `test_labNN_*` name already exists. The verifier must run the real resulting test, not a similarly named placeholder or source-text-only proxy.

HOST builds **all** selected-lab variants through their registered `make` entry points. QUAL verifies and reuses those build results when their compiled-input fingerprints still match; it rebuilds affected variants when inputs changed or evidence is unavailable. LAB-05 peak additionally builds existing `lab05` manual/scripted as preservation checks, plus all four peak variants. LAB-12 builds all four phase variants. Non-default variants use `make lab.<name>.build VARIANT=<key>`; `.scripted.build` cannot select them. Preserve preexisting build directories rather than cleaning them to obtain a baseline. QUAL runs the full `make test` suite once on the final software state, repeating it only when a later change affects code or checks covered by the suite.

For new adapters, L1 tests can inject serial/reset/capture and fault hooks, but also exercise the real local fixture server and parser where applicable. Their generated inputs and test output must be labeled `host`. No host test writes invented data under a real run id in the evidence tree. Every executed check records command, exit/outcome, source fingerprint, artifact paths, and limitations. A failed check is not `not_run`; a missing tool/service is a named blocker. Report any preexisting unrelated test failures with baseline evidence; do not silently call `make test` passing or repair outside scope.

The acceptance ledger has at least these columns:

| Acceptance ID | Owner package | Required level | Implemented path | Actual check / artifact | Achieved level | Disposition / blocker |
|---|---|---|---|---|---|---|
| S-* or LNN-* | stable package ID | L0–L3 | concrete source | reproducible command or raw artifact | actual level or none | pass / fail / blocked / not applicable with reason |

Mixed-level rows (for example S-07) have separate L1 and L2 subrows. MODEL/FW/HOST/QUAL/DEVICE together must cover every applicable shared and per-lab criterion; partial phase passes never erase deferred subrows. No actual implementation, build, host test, or device capture was performed as part of this document-readiness review.

### 8.7 Agent dispatch, repair, and result contract

The orchestrator inspects and coordinates; it edits only its owned progress state. Implementers edit the assigned package. Verifiers and standards reviewers are read-only on maintained files; tests may produce normal owned build/test output. Execute one package at a time with no editing while review/verification is running.

**Batch work within each package.** A package is the smallest independent acceptance and review checkpoint; files, scenarios, and individual findings are not separate agent tasks. Give one implementer the complete bounded package brief and all currently known in-scope requirements/findings, and have that implementer finish the package's related changes and focused checks before requesting independent verification. Do not spawn additional implementers for individual files, tests, or findings in the same package. If verification finds defects, group all in-scope findings into the package's single permitted repair assignment, then use one fresh verifier to check the repair. Keep the package order and acceptance criteria unchanged. Reopen an earlier package only when a later change modifies its owned code/contract or produces evidence that its prior result is stale; record the specific invalidated checks and rerun those checks.

For **each** package, pass a new implementer the frozen complete brief, prerequisite verdicts/artifact fingerprints, permitted paths, protected baseline paths, and relevant source context. On completion compare the actual changed paths to that scope, then launch a **fresh independent verifier** with the same brief, acceptance IDs, current bounded diff/untracked-file evidence, and the implementer's structured report. Do not reuse the implementer as verifier, inherit its private transcript, or let a verifier fix code. With the current agent interface, use a new invocation with `fork_turns=none` and provide the explicit bounded context. If the available interface cannot supply separate fresh context, stop and report that limitation.

Use the implementation/verifier JSON schemas in the orchestrate-build skill. Implementation reports include status, actual changed paths, actual checks/outcomes, limitations, and `commit_created=false`. Verifier reports return `pass`, `fail`, or `blocked`, acceptance-linked findings and reproductions, checks/evidence, scope review, and `commit_created=false`. Add the acceptance ledger/dispositions and snapshot identity as supporting evidence. Assertion-only or incomplete reports do not pass.

Default repair limit is **one implementation rerun per package**, followed by another fresh verifier. QUAL standards-review fixes count against that same package budget. A `blocked` result is not a failed test to retry until it passes. Stop on exhausted repair budget, missing hard prerequisite, unexpected scope edits, unresolved source conflict, or required evidence unavailable for the selected package. Do not advance dependencies on a `fail`, `blocked`, or stale result. Preserve partial work and report the exact next action.

### 8.8 Completion and resume

- **Software scope complete:** all four selected software packages have current independent passes; real variant builds and required host/native/server tests are evidenced; standards findings are reconciled; the report/index and full acceptance ledger exist. Report **software qualified; device evidence pending** when appropriate. Mark DEVICE `not selected`, with its L2/L3 criteria explicitly pending. This does not waive them or make the full lab experimentally complete.
- **Device/full scope complete:** DEVICE also has a current independent pass against actual L2/L3 evidence. Functional, exploratory, and performance dispositions remain separate. Unapproved performance budgets and product decisions remain OPEN even when measurements are complete.
- **Blocked:** identify the failing package/criterion, actual evidence, what is already implemented/verified, and the missing input or environmental action. A report that merely lists missing evidence cannot turn a selected required DEVICE package into a pass.
- **Resume:** reread this plan, governing sources, progress records, dependency artifacts, dirty state, and relevant content fingerprints. Reverify changed prerequisites and affected packages. Resume the first requested eligible package without silently rerunning other labs or trusting old verdicts. Keep prior runs, repairs, and failed evidence visible.
