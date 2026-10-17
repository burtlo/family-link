# Guide for lab assistants (agents and humans)

This guide is for anyone implementing, running, or documenting a **v2 device lab**. It complements the experiment definitions in [inbox carousel playback](inbox-carousel-playback-evaluation.md) and the [experience roadmap](v2-experience-demo-roadmap.md).

## Your job

1. **Answer a bounded product question** with on-device (or agreed host-only) evidence.
2. **Ship a new lab implementation** under [`client/labs/`](repository-layout.md#firmware) (and [`server/labs/`](repository-layout.md#servers) when the plan requires it).
3. **Publish a report** under [`docs/labs/evidence/`](evidence/README.md) with target, measured, trigger, and decision — without claiming OPEN product behavior is approved.

You are not replacing experience specs, communication contracts, or UI approvals.

## Before you write code

| Step | Action |
|------|--------|
| 1 | Read [`docs/AGENTS.md`](../AGENTS.md) — especially archive boundaries. |
| 2 | Read [lab README](README.md) for product **`LAB-NN`** IDs; check whether a **make name** exists in [`config/labs.registry.yaml`](../../config/labs.registry.yaml) (`make lab.help`). |
| 3 | Read the full **Run / Capture / Result sought** section for that lab in the plan doc. |
| 4 | Read [repository layout](repository-layout.md) and create only the paths for your lab. |
| 5 | For **LAB-02**, read [implementation outline](lab-02-implementation-outline.md) before scaffolding. |
| 6 | Skim [client coding standards](../standards/client-application-coding-standards.md) — single UI owner, terminal operations, verification ladder. |
| 7 | Before Phase B / L3 capture, optional: **client-code-review** on `client/labs/<slug>/` ([prompt](../standards/client-code-review-agent.md)); copy the **Fix and verify packet** from Handoff into a new chat (ID alone is not enough — see standard). |

**Legacy v1 demos:** You may read the v1 POC archive to recover behavior, timing, or wire shapes. Extract facts into lab code and active docs; do not modify archive sources or add markdown links to `poc-v1/...` in new artifacts outside the archive.

## Implementation rules

### New code only

- Implement the experiment in **`client/labs/<slug>/`**, not in `poc-v1/`.
- Reproduce **only** behavior needed for the experiment (stub cards, synthetic inbox, local page provider).
- Optional: run an **unmodified** legacy demo on hardware for qualitative comparison; record its git revision separately. Do not add probes to that binary.

### Versioning

Publish logical versions as:

```text
LAB-02/v2 = firmware <build-id> + server none + fixture inbox-summary/v1
```

Increment `lab_revision` or fixture revision when:

- Fixture JSON shape or content used for comparisons changes.
- Server timing scripts, delays, or response bytes change.
- Firmware trace schema or default harness scenario changes.

Never label two runs the same version if any of those differ.

### Instrumentation

Follow the **Common method and evidence** section in the [inbox plan](inbox-carousel-playback-evaluation.md#common-method-and-evidence) for carousel/media labs. For experience labs, use the capture bullets in the [roadmap](v2-experience-demo-roadmap.md#how-to-package-each-new-demo).

Always separate:

| Category | Meaning |
|----------|---------|
| Baseline observation | Uninstrumented legacy or control build |
| Candidate result | Your lab firmware (+ server) |
| Proposed gate | Numbers you set **before** the run |
| Decision | Owner-facing outcome or **OPEN** |

Record verification level **L0–L3** per [verification ladder](../standards/client-application-coding-standards.md#11-verification-ladder).

### Trace conventions (minimum)

Use one line per event on serial (prefix e.g. `LAB_TRACE`). Include:

- Monotonic time (µs or ms)
- `session_id`, `message_id` or `user_id` where applicable
- `operation_id` and binding `generation` for async work
- Event name: `bind`, `unbind`, `snap_start`, `snap_end`, `ui_tick`, `alloc_fail`, …

No credentials, PINs, or raw media payloads in logs.

Define the exact field list in the lab `README.md` and reference `trace_schema` in `manifest.lab.json`.

## Suggested implementation order

| Priority | Lab | Why |
|----------|-----|-----|
| 1 | **LAB-02** Phase A | Local inbox controller + three card views; unblocks carousel architecture without server. |
| 2 | **LAB-02** Phase B | Paging/push/removal during snap via `server/labs/lab-02-paging/`. |
| 3 | **LAB-03** | Needs stable bind model from LAB-02; paired audio+sketch server. |
| 4 | **LAB-06** | Stage shell in parallel if login journey is the focus. |
| 5 | **LAB-01** | Integrated x02-like baseline when holistic stall diagnosis matters. |

Skipping straight to **LAB-02** is consistent with the plans; do not skip evidence capture or the eager 16-card comparison build when evaluating LAB-02.

## LAB-02 quick reference

| Item | Location |
|------|----------|
| Make name | `lab02` → `make lab.lab02` |
| Full file tree and PR milestones | [lab-02-implementation-outline.md](lab-02-implementation-outline.md) |
| Scenario IDs (`S-fix-256`, `S-trav-20`, …) | Same outline § Verification scenarios |
| Run ID format | `YYYYMMDD-<phase>-<fixture>-<variant>-<seq>` |
| Serial logs | `logs/device/labs/lab-02/<run-id>/` |
| Report template | [evidence/lab-02/TEMPLATE-REPORT.md](evidence/lab-02/TEMPLATE-REPORT.md) |
| Run manifest | [schemas/run-manifest.v2.md](schemas/run-manifest.v2.md) for shared runs; [v1](schemas/run-manifest.v1.md) documents the legacy LAB-02 format |
| Inbox UI | Stub **message** cards — snap geometry may follow roster panel spec; do not ship roster chrome as inbox |

**LAB-02 boot marker:** the LAB-02 adapter accepts only a fresh `LAB02_READY` marker observed
after its capture-armed RTS reset. A missing marker makes device evidence incomplete.

**Eager comparison:** separate build directory; set `comparison_run_id` in both manifests.

## Host commands (`make lab.*`)

Lab firmware is **not** built by `make build` (that is product bootstrap in `client/device/`). Use the **host lab registry** and [`scripts/lab_host.py`](../../scripts/lab_host.py). The `make lab.<name>` and `make lab.<name>.scripted` targets enter the shared run lifecycle and create a common v2 run manifest and event journal. The generic adapter uses this lifecycle without configuring a service or scenario; LAB-02 selects its allowlisted adapter for the managed fixture and experiment-specific checks.

| Make target | What it does |
|-------------|----------------|
| `make lab.help` | List registered labs and variants (`lab_host.py list`) |
| `make lab.<name>.help` | Operator instructions from registry `run.instructions` / `run.instructions_doc` |
| `make lab.<name>` | Manual shared run: creates evidence, resolves one registered kit, builds, captures serial, and finalizes the run manifest/events |
| `make lab.<name>.scripted` | Scripted shared run; the generic adapter selects the registered `scripted` variant when present, otherwise the registry default. LAB-02 managed HTTP defaults to `scripted-phaseb` when `VARIANT` is unset |
| `make lab.<name>.build` | ESP-IDF build only for the selected variant |
| `make lab.<name>.flash` | Flash an existing build to one kit |
| `make lab.<name>.capture` | Lower-level serial capture only; it does not create the common run manifest/events |

**Make variables** (optional; same names work as environment variables for `lab_host.py`):

| Variable | Used on | Meaning |
|----------|---------|---------|
| `KIT` | `.flash`, `.capture`, `lab.<name>` | Deployment nickname (`lynn`, `mazi`, …) — same as `make flash.lynn` |
| `PORT` | same | Explicit serial port; overrides `KIT` |
| `VARIANT` | all build/flash/capture/run | Registry variant key (e.g. `recycle`, `eager`); generic scripted runs use registered `scripted` when available, else `variants.default`. For LAB-02 managed HTTP scripted runs, an explicit value overrides the `scripted-phaseb` default and the adapter applies its managed-plan mapping |
| `SECONDS` | `.capture`, `lab.<name>` | Capture duration; default from registry `run.capture_seconds_default` |
| `FLASH` | `.capture`, `lab.<name>` | `1` / `yes` force flash; `0` / `no` skip flash. Default for `make lab.<name>`: registry `run.flash_by_default` |
| `RUN_ID` | `.capture`, `lab.<name>` | Override auto-generated run folder name |

**LAB-02 commands:**

```text
make lab.help
make lab.lab02 KIT=lynn
make lab.lab02.scripted KIT=lynn
make lab.lab02 BACKEND=local VARIANT=eager KIT=lynn
make lab.lab02 BACKEND=local KIT=lynn
```

Both shared LAB-02 commands use managed HTTP by default. Manual mode allows free
carousel traversal, including loading cards. Scripted mode runs the device
harness; a successful experiment requires its expected completion evidence.
The host starts, health-checks, seeds, and stops the owned server. `BACKEND=local`
selects the explicit local-fixture comparison; add `VARIANT=eager` only for that
comparison. See the LAB-02 runbook for scenario-specific criteria.

The LAB-02 adapter writes common run evidence and retains its LAB-02 scenario
proofs and parser output. Record the **exact make command** in the evidence
report; a workflow pass or parser success alone is not a passing experiment
result. Shared run schemas: [manifest v2](schemas/run-manifest.v2.md) and
[metrics v2](schemas/metrics.v2.md).

**No-server example — LAB-05:**

```text
make lab.lab05 KIT=lynn-kit
make lab.lab05.scripted KIT=lynn-kit
```

LAB-05 omits `adapter` (therefore uses `generic`) and `workflow`. Its shared
manifest records `service` and `scenario` as `not_configured`; no placeholder
server settings are needed. The current manual and scripted evidence records
show workflow `PASS` and experiment `UNMEASURED`. The scripted serial capture
does not contain `LAB05_HARNESS_DONE`; neither run is a performance result.
See [LAB-05 evidence runs](evidence/README.md#shared-run-evidence-examples).

Host registry standard: [labs configuration](../standards/labs-configuration.md). Product bootstrap flash rules still apply for kit binding (`make devices`, `make device.bind`).

**Do not** point `[firmware].source` in `host.defaults.ini` at a lab tree or flash labs with `make flash` (bootstrap package only).

## Registering a new lab (host)

Two registries serve different purposes:

| Registry | Location | Purpose |
|----------|----------|---------|
| Product / plan | [docs/labs/README.md](README.md) § Lab ID registry | `LAB-NN` experiment definition, evidence slug convention, links to plan docs |
| **Host / make** | [`config/labs.registry.yaml`](../../config/labs.registry.yaml) | Short **make name** (`lab02`), ESP-IDF paths, build variants, capture defaults |

When firmware exists under `client/labs/<slug>/`, add a host entry so assistants and owners can run **`make lab.<name>`** without editing the Makefile (pattern rules in the root [`Makefile`](../../Makefile) already dispatch to `lab_host.py`). Use `adapter: generic` (or omit it) for the standard no-service/no-scenario lifecycle. Select a built-in adapter key only when the lab has matching host workflow code; adapter keys are fixed in the host allowlist and are not Python import paths.

### Steps to register

1. **Create** the ESP-IDF project at `client/labs/<slug>/` per [repository layout](repository-layout.md) (README, `manifest.lab.json`, `CMakeLists.txt`, components, fixtures).
2. **Choose a make name** — lowercase, no dots (e.g. `lab02`, `lab07`). This becomes `make lab.lab02`.
3. **Append** a block under `labs:` in `config/labs.registry.yaml`, including **`run.instructions`** (short steps) and/or **`run.instructions_doc`** (`path/to/doc.md#anchor`). Field reference: [labs configuration](../standards/labs-configuration.md#registry-fields-per-labsmake_name).

4. **Validate:** `.venv/bin/python scripts/lab_registry.py validate`, `make config.labs`, `make lab.help`, and `make test` — the new lab must appear with correct variants and defaults.
5. **Document** in the lab’s `client/labs/<slug>/README.md`: make name, variants, boot marker, menuconfig knobs.
6. **Optional — agent command catalog:** add `[command.lab.<name>]`, `.build`, `.flash`, `.capture` sections to [`config/host.defaults.ini`](../../config/host.defaults.ini) mirroring `make lab.lab02` entries (helps `make config.host` snapshots; not required for make to work).
7. **Evidence:** add a row to [evidence/README.md](evidence/README.md) and create `docs/labs/evidence/<evidence_slug>/` when first runs exist.

`project_dir` must exist when the registry is loaded (tests validate paths). Do not register planned labs until the project directory is present.

### LAB-02 (reference registration)

| Field | Value |
|-------|--------|
| Make name | `lab02` |
| Product ID | `LAB-02` |
| Evidence slug | `lab-02` |
| Variants | `recycle` (default), `eager` (separate `build_dir` + `sdkconfig.defaults.eager`) |

## Per-lab deliverables checklist

When closing a lab revision, ensure:

- [ ] Entry in [`config/labs.registry.yaml`](../../config/labs.registry.yaml) when firmware is runnable on device
- [ ] `client/labs/<slug>/README.md` — make name, variants, scenarios, boot marker, known limits
- [ ] `client/labs/<slug>/manifest.lab.json` — ids and fixture revision
- [ ] `fixtures/` or server `fixture/` — deterministic data; hashes in manifest
- [ ] `docs/labs/evidence/<lab-id>/REPORT-*.md` — target, measured, triggers, decisions
- [ ] For shared `.run` workflows, `docs/labs/evidence/<lab-id>/runs/<run-id>/run-manifest.json` and `events.jsonl` per [manifest v2](schemas/run-manifest.v2.md); link serial under `logs/device/labs/<slug>/<run-id>/`
- [ ] `metrics.json` follows [metrics v2](schemas/metrics.v2.md) when the lab produces metrics; record `UNMEASURED` when it does not
- [ ] Parsed [metrics v1](schemas/metrics.v1.md) + serial log under `logs/device/labs/<lab-slug>/` when captured
- [ ] [Measurement table](../product/device-ui/assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md#4-standard-measurement-table) for UI-heavy labs
- [ ] Link from affected assessment or contract (decision row only; do not approve OPEN items by assertion)

## Common mistakes

| Mistake | Instead |
|---------|---------|
| 256 LVGL card widgets for a large inbox fixture | Metadata for 256; **three** (or plan-defined) live views |
| Instrumenting archive demo source | New lab project with probes |
| Passing lab because p95 looks good while worst case hitches | Report p50/p95/p99 **and worst** |
| Using demo PASS as performance proof | Measured gates from the lab plan |
| Merging lab-specific names into `client/device` early | Promote via `client/shared/` with review |
| Pointing server at real household data | Isolated fixture only |
| Using `make build` / `make flash` for a lab | `make lab.<name>` and registry variants |
| Registering in docs only, not in `labs.registry.yaml` | Host entry + `make lab.help` shows the lab |

## When to ask the owner

- Changing an **approved** experience or contract based on lab results.
- Picking among OPEN wire or UX options (labs may label branches **exploratory** only).
- Promoting numbers into [`FOUNDATION-LVGL-BUDGET`](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md).

## Related documents

- [Repository layout](repository-layout.md) — folder tree and manifests
- [Lab index](README.md) — `LAB-01`–`LAB-13` registry
- [Deep implementation retrospective skill](../../.cursor/skills/deep-implementation-retrospective/SKILL.md) — after a major lab cycle, optional structured retrospective
