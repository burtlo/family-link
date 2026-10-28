# Device labs configuration

**Device labs** are long-lived ESP-IDF experiments under [`client/labs/`](../../client/labs/).
They are not product firmware, but their **host registry**, build dirs, and capture
paths are first-class configuration alongside deployment roster data.

Product intent (`LAB-NN` ids, evidence, promotion) lives in [`docs/labs/`](../labs/README.md).
This document defines the **host registry** that powers `make lab.<name>` without
editing the root Makefile.

## Two registries

| Registry | Location | Purpose |
|----------|----------|---------|
| **Product / plan** | [`docs/labs/README.md`](../labs/README.md) § Lab ID registry | Experiment definitions, folder slugs, evidence conventions |
| **Host / make** | [`config/labs.registry.yaml`](../../config/labs.registry.yaml) | Short make names (`lab02`), IDF paths, variants, capture defaults |

Every runnable lab needs **both**: a plan row in docs and a host entry when
`client/labs/<slug>/` exists. Do not register host entries for folders that do
not exist yet (semantic validation requires `project_dir` on disk).

Host path pointers are in [`config/host.defaults.ini`](../../config/host.defaults.ini)
section `[labs]` (`registry`, `schema`).

## Schema and validation (two layers)

| Layer | Source | What it checks |
|-------|--------|----------------|
| **Structure** | [`config/labs/labs.registry.schema.v1.json`](../../config/labs/labs.registry.schema.v1.json) | `document`, make names, required fields, adapter key, optional workflow shape, and variant map shape |
| **Semantics** | [`scripts/lab_registry.py`](../../scripts/lab_registry.py) | Repo-relative paths, `project_dir` present, default variant defined, adapter is allowlisted, optional `trace_parser` file exists, and operator instructions resolve |

`python scripts/lab_registry.py validate` runs both layers.
`make config.labs` prints a registry snapshot (`family-link.labs-registry.snapshot/v1`).

Optional top-level `$schema` in the YAML file may point at the JSON Schema file
for editor completion.

## Inspection

```text
make config.labs
make lab.help
python scripts/lab_registry.py catalog
python scripts/lab_registry.py validate
python scripts/lab_registry.py dump
```

`make test` includes [`scripts/tests/test_lab_registry.py`](../../scripts/tests/test_lab_registry.py).

## Files

| File | Role |
|------|------|
| `config/labs.registry.yaml` | Committed host registry (`document: family-link.labs-registry/v1`) |
| `config/labs/labs.registry.schema.v1.json` | Versioned structural contract |
| `config/host.defaults.ini` `[labs]` | Registry and schema paths; env overrides `LABS_REGISTRY`, `LABS_SCHEMA` |
| `client/labs/kconfig/` | Shared Kconfig and `sdkconfig.defaults.scripted` for auto-scroll builds |

## Shared firmware options (`client/labs/kconfig/`)

Every ESP-IDF lab sources [`client/labs/kconfig/lab_common.projbuild`](../../client/labs/kconfig/lab_common.projbuild) from its root `Kconfig.projbuild`. Operator-facing symbols use the **same names** in every lab:

| Kconfig | menuconfig location | Purpose |
|---------|---------------------|---------|
| `LAB_AUTO_HARNESS` | **Family Link device lab** → Auto-scroll inbox after boot | Device runs scripted traversal; serial `HARNESS_DONE` |

**Prefer behavior-named make targets** (no `VARIANT=` to remember):

| You want… | Make target |
|-----------|-------------|
| You scroll the UI during capture | `make lab.<name>` |
| Device auto-scrolls (scripted) | `make lab.<name>.scripted` |
| LAB-02 eager 16-widget comparison | `make lab.lab02 BACKEND=local VARIANT=eager` |
| Drop the default build tree | `make lab.<name>.clean` |

The registry keeps internal comparison variants such as `eager`; they are not separate operator targets. Advanced `VARIANT=` overrides remain available for evidence work. Lab-specific Kconfig (fixtures, etc.) stays in each lab’s `Kconfig.projbuild` menu.

## Registry fields (per `labs.<make_name>`)

| Field | Required | Description |
|-------|----------|-------------|
| `lab_id` | No | Product id (`LAB-02`); used in run manifests and evidence |
| `evidence_slug` | No | Evidence and log folder segment; defaults from the make name if omitted |
| `project_dir` | Yes | Repo-relative ESP-IDF project (`client/labs/...`) |
| `target` | No | IDF target chip (default `esp32s3`; must match host `idf.target` at build time) |
| `description` | No | One line for `make lab.help` |
| `adapter` | No | Built-in adapter key (`generic`, default; or `lab02`). The host resolves keys through a fixed allowlist; registry data cannot name Python modules or import paths. |
| `variants` | Yes | Map with `default` key plus one entry per variant (see below) |
| `trace_parser` | No | Repo-relative script to turn serial trace into metrics |
| `run` | Yes | Operator text and optional capture defaults (see below) |
| `workflow` | No | Adapter-specific workflow settings. If present, the current schema requires the complete LAB-02 settings set: server path, bind host/port, fixture size, scenario, and all stage deadlines. A generic no-server lab omits this object. |

The shared run engine records workflow stages and common evidence. The `generic`
adapter uses the standard host preflight/build/capture path and supplies no
service or scenario hook, so those stages are recorded as `not_configured`.
The allowlisted `lab02` adapter supplies LAB-02's managed workflow and preserves
its scenario-specific checks. Adapters are host code; a registry entry selects
an existing key and does not provide executable code or arbitrary module paths.

The LAB-02 firmware client URL is normally discovered at run time from the
workstation's current LAN interfaces. The committed
`config/host.defaults.ini` value is `client_url = auto`; the resolver prefers
the address selected by the host's default route and falls back to the only
eligible private IPv4 interface. The server still binds to `0.0.0.0`; the
resolved URL is written into the kit's generated `sdkconfig` defaults so the
BOX can reach the host.

For a workstation with several private networks, use the explicit
`LABS_LAB02_CLIENT_URL=http://<workstation-ip>:8765` override. The URL is
validated against the current local interfaces and registry port. This setting
does not belong in the private deployment roster or tracked labs registry.
Managed HTTP Phase B flash targets refresh this generated value before writing
the image, so a changed DHCP lease cannot leave the BOX using the previous IP.

### Variants and `run`

- `variants.default` names the variant used when `VARIANT` is unset.
- Each variant key (except `default`) defines `build_dir` (repo-relative, usually under `build/labs/`) and optional `sdkconfig_defaults` (paths relative to `project_dir`).
- Use distinct `build_dir` values per variant so recycle and eager builds do not overwrite each other.
- ESP-IDF applies `sdkconfig.defaults` only when `sdkconfig` is created. `scripts/lab_host.py` deletes a stale `build_dir/sdkconfig` when variant defaults are missing from it (so `make lab.<name>.scripted` picks up `CONFIG_LAB_AUTO_HARNESS`).

`run` is required and must contain at least one operator instruction field:
`instructions` or `instructions_doc`. The instruction document is a repo-relative
Markdown path with an optional anchor. `capture_seconds_default` and
`flash_by_default` are optional.

Every registered lab must tell the developer what to do on hardware. Set at least one of:

| Key | Required | Description |
|-----|----------|-------------|
| `instructions` | One of these | Short plain-language steps; printed by `make lab.<name>.help` and at the start of `make lab.<name>` |
| `instructions_doc` | One of these | Repo-relative markdown path, optional `#anchor` (file must exist) |
| `capture_seconds_default` | No | Serial capture duration for `make lab.<name>` (default 90). Printed in `make lab.<name>.help` and at run start; override with `SECONDS=` |
| `flash_by_default` | No | Whether `make lab.<name>` flashes before capture (default true) |

`make lab.<name>.help` and the start of `make lab.<name>` print the operator block.

Example (abbreviated; see committed [`config/labs.registry.yaml`](../../config/labs.registry.yaml)):

```yaml
document: family-link.labs-registry/v1
labs:
  lab02:
    lab_id: LAB-02
    evidence_slug: lab-02
    project_dir: client/labs/lab-02-bounded-inbox
    target: esp32s3
    description: Bounded inbox and three-view card recycling (Phase A)
    variants:
      default: recycle
      recycle:
        build_dir: build/labs/lab-02-bounded-inbox
      eager:
        build_dir: build/labs/lab-02-bounded-inbox-eager
        sdkconfig_defaults:
          - sdkconfig.defaults
          - sdkconfig.defaults.eager
    trace_parser: client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py
    run:
      capture_seconds_default: 120
      flash_by_default: true
      instructions: |
        Scroll the carousel during capture; see doc link for scenario IDs.
      instructions_doc: docs/labs/lab-02-implementation-outline.md#verification-scenarios
```

## Make targets and variables

Pattern rules in the root [`Makefile`](../../Makefile) dispatch to
[`scripts/lab_host.py`](../../scripts/lab_host.py). Adding a lab requires **only**
a registry entry and project tree — not new Makefile rules.

| Make target | Role |
|-------------|------|
| `make lab.help` | List registered labs |
| `make lab.<name>.help` | Operator instructions from the registry |
| `make lab.<name>` | Manual shared-runner workflow; creates run evidence, resolves one registered kit, builds, captures serial, then finalizes workflow evidence |
| `make lab.<name>.scripted` | Scripted shared-runner workflow. The generic adapter selects the registered `scripted` variant when present, otherwise the registry default; LAB-02 managed HTTP defaults to `scripted-phaseb` when `VARIANT` is unset |
| `make lab.<name>.build` | Build only (`VARIANT=…`) |
| `make lab.<name>.clean` | Remove that variant’s `build_dir` (`idf.py fullclean` when configured, else delete tree) |
| `make lab.<name>.flash` | Flash (`KIT=` or `PORT=`) |
| `make lab.<name>.capture` | Lower-level serial capture (`FLASH=0` to skip flash); does not run the shared manifest lifecycle |
| `make lab.<name>.scripted.capture` | Lower-level scripted-mode serial capture; does not run the shared manifest lifecycle |
| `make lab.<name>.parse` | LAB-specific parser: `serial.txt` → `metrics.json` (`RUN_ID=` or latest capture) |

| Variable | Applies to | Meaning |
|----------|------------|---------|
| `KIT` | flash, capture, run | Deployment kit nickname (see [deployment configuration](deployment-configuration.md)) |
| `PORT` | flash, capture, run | Explicit serial port; it must match exactly one connected kit registered in deployment configuration |
| `VARIANT` | build, flash, capture, run | Registry variant key; for LAB-02 managed HTTP scripted runs, an explicit value overrides the `scripted-phaseb` default and the adapter applies its managed-plan mapping |
| `SECONDS` | capture, run | Capture duration |
| `FLASH` | capture, run | Force or skip flash (`1`/`0`, `yes`/`no`) |
| `RUN_ID` | capture, run | Override run folder name under `logs/device/labs/` |
| `BACKEND` | LAB-02 run/capture | `managed-http` (default) or `local`; other adapters may reject this option |

Shared `.run` workflows write a run-scoped `run-manifest.json` and `events.jsonl`
under `docs/labs/evidence/<evidence_slug>/runs/<run_id>/`, and link serial output
under `logs/device/labs/<evidence_slug>/<run_id>/serial.txt`. See the [run
manifest v2 contract](../labs/schemas/run-manifest.v2.md) and [metrics v2
contract](../labs/schemas/metrics.v2.md). Metrics are lab-specific and may be
absent when no parser or measurement is supplied. A workflow `PASS` describes
completion of configured workflow stages; it does not by itself establish an
experiment verdict or performance result.

Step-by-step workflow for authors: [`docs/labs/assistant-guide.md`](../labs/assistant-guide.md).

## Boundaries vs product firmware

- Do **not** point `[firmware].source` at a lab tree or flash labs with `make flash`
  (bootstrap package only). See [device build and flash](device-build-flash.md).
- Lab serial logs use `[firmware].logs` / `labs/<evidence_slug>/` (not bootstrap artifacts).
- Optional: add `[command.lab.<name>]` (and `.build`, `.flash`, `.capture`) sections to
  `host.defaults.ini` so `make config.host` catalogs lab commands for agents; make targets
  work without these entries.

## Agent snapshot (`make config.labs`)

`make config.labs` prints JSON with:

| Key | Role |
|-----|------|
| `document` | `family-link.labs-registry.snapshot/v1` |
| `meta` | Registry path, schema path, lab count |
| `labs` | Make names, product ids, variants, build dirs, run defaults |

Use `python scripts/lab_registry.py catalog` for schema location and semantic rules only.
