# Lab repository layout (projected)

Status: **normative for new lab work** once implementation starts. Paths below are relative to the repository root unless noted.

This document defines where lab **firmware**, **servers**, **shared client code**, **build outputs**, and **evidence** live. It keeps the shipping app ([`client/device/`](../../client/device/)) separate from disposable, heavily instrumented lab images.

## Design principles

1. **No lab code in the v1 archive** — read legacy demos for behavior; implement and instrument under `client/labs/` and `server/labs/`.
2. **One ESP-IDF project per lab** (or per lab family) — own `CMakeLists.txt`, `sdkconfig.defaults`, and build directory. Do not add LVGL carousel experiments to the bootstrap `client/device` main app until promoting reviewed modules.
3. **Version as a triple** — `lab_id` + monotonic lab revision (e.g. `v3`) + immutable firmware `build_id` and server/fixture revisions. Document the triple in the lab README and in the evidence report.
4. **Evidence in docs, binaries in build/artifacts** — traces, videos, and reports under `docs/labs/evidence/`; flashable packages under `artifacts/device/labs/` when packaging is wired.

## Top-level map

```text
family-link/
├── client/
│   ├── device/                    # Product firmware (bootstrap → full v2 app)
│   ├── shared/                    # Promoted modules (platform, trace, inbox, …)
│   └── labs/
│       ├── README.md              # Optional index; canonical registry is docs/labs/README.md
│       ├── kconfig/                 # Shared lab_common.projbuild (e.g. LAB_AUTO_HARNESS)
│       └── <lab-folder>/          # One ESP-IDF project per row in lab registry
├── server/
│   └── labs/
│       └── <lab-folder>/          # Python (or other) fixture servers; isolated test data
├── build/
│   ├── device/                    # Product build (host firmware.build)
│   └── labs/
│       └── <lab-folder>/          # IDF -B output per lab
├── artifacts/
│   └── device/
│       ├── bootstrap/             # Product packages (make build)
│       └── labs/
│           └── <lab-folder>/      # Lab packages when lab flash flow exists
├── logs/
│   └── device/
│       └── labs/<lab-slug>/<run-id>/  # Serial captures (slug lowercase, e.g. lab-02)
├── docs/
│   └── labs/
│       ├── README.md              # Lab index and ID registry
│       ├── assistant-guide.md
│       ├── repository-layout.md   # This file
│       ├── evidence/              # Reports and run metadata (not multi-GB media)
│       ├── inbox-carousel-playback-evaluation.md
│       └── v2-experience-demo-roadmap.md
└── config/
    ├── host.defaults.ini          # [labs] registry + schema paths; optional [command.lab.*]
    ├── labs.registry.yaml         # Host make names (see standards/labs-configuration.md)
    └── labs/
        └── labs.registry.schema.v1.json
```

## Firmware

### Per-lab project layout

Example: **`LAB-02`** → `client/labs/lab-02-bounded-inbox/`

**Authoritative tree, phases, scenarios, and milestones:** [LAB-02 implementation outline](lab-02-implementation-outline.md).

ESP-IDF labs should use **`components/`** for logic (not a flat `src/` only), optional **`Kconfig.projbuild`** for variants, and a dedicated **`lab02_platform`** (or equivalent) until BOX-3 display code promotes to `client/shared/platform/`.

```text
client/labs/lab-02-bounded-inbox/
├── README.md
├── manifest.lab.json
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── Kconfig.projbuild
├── main/
│   └── app_main.c
├── components/
│   ├── lab02_platform/       # Display + LVGL lock (lab-first, then promote)
│   ├── lab02_fixtures/       # Embed fixtures/*.json
│   └── lab02/                # Model, controller, views, snap, trace, harness
├── fixtures/
├── tools/                    # e.g. parse_lab_trace.py → metrics.json
└── tests/                    # L1 host/IDF tests where applicable
```

Other labs may use a slimmer tree; carousel/inbox labs should follow the LAB-02 outline pattern.

### Comparison and variant builds

Prefer **one tree**, multiple targets:

- Kconfig / CMake option e.g. `LAB02_EAGER_BASELINE` for the eager 16-card comparison in [LAB-02](inbox-carousel-playback-evaluation.md#lab-02--bounded-inbox-and-card-recycling).
- Use a **separate** `build/labs/<slug>-<variant>` directory for eager vs recycle images; separate `build_id` per flash.
- Link paired runs via `comparison_run_id` in [run manifest v1](schemas/run-manifest.v1.md).

### Shared client code

When lab code survives review, move it to **`client/shared/`** with product-oriented names (no `lab02_` prefixes in shared APIs):

```text
client/shared/
├── platform/       # Board, display, LVGL lock (ported when first display lab needs it)
├── trace/          # Trace line format, session/operation IDs
└── inbox/          # Example: carousel_controller after LAB-02 promotion
```

Labs keep thin wrappers and extra probes; the product app composes shared modules under `client/device/`.

### Module boundaries (carousel labs)

| Layer | Typical files | LVGL? |
|-------|----------------|-------|
| Fixture / model | `inbox_model`, JSON in `fixtures/` | No |
| Page provider | `page_provider_local`, `page_provider_http` | No |
| Controller | `carousel_controller`, focus, binding generation | Coordinates only |
| Views | `card_pool`, `card_view_*` | Yes |
| Instrumentation | `instrumentation`, `trace_emit` | Reads metrics |

Keep **network and decode off the gesture path**; LAB-02 explicitly requires no remote wait during scroll/snap.

## Servers

Example: **`LAB-02` Phase B** → `server/labs/lab-02-paging/`

```text
server/labs/lab-02-paging/
├── README.md
├── manifest.lab.json
├── pyproject.toml              # or requirements.txt pinned for reproducibility
├── fixture/
│   ├── inbox-summary.v2.json   # Schema sample + hashes in manifest
│   └── scripts/                # Delay, push, remove-focused-item, …
└── server.py                   # Serves pages; never points at live household data
```

Server-enabled labs ship **firmware + server + fixture** as one logical version. Changing response timing or bytes increments the fixture or server revision and requires rerunning affected comparisons.

## Evidence

```text
docs/labs/evidence/
├── README.md
└── lab-02/
    ├── REPORT-LAB-02-v1.md     # target / measured / decision
    └── runs/
        └── 20261011-phase-a-64/
            ├── run-manifest.json
            ├── metrics.json
            ├── trace.txt         # or pointer to logs/device/…
            └── video.md          # link or short note; large files outside git if needed
```

Do not commit credentials, PINs, or raw media from real households. Use synthetic fixtures only.

## Build and host configuration

| Target | Source | Build dir | Artifacts | Today |
|--------|--------|-----------|-----------|--------|
| Product bootstrap | `client/device` | `build/device` | `artifacts/device/bootstrap/` | `make build`, `make flash.*` |
| Lab image | `client/labs/<slug>` | `build/labs/<slug>` (per registry variant) | `artifacts/device/labs/<slug>/` (projected) | `make lab.<name>`, `make lab.<name>.build` |

**Preferred host flow** ([labs configuration](../standards/labs-configuration.md)):

- Register the lab in [`config/labs.registry.yaml`](../../config/labs.registry.yaml) with make name, `project_dir`, variants, and `build_dir` paths.
- Run **`make lab.<name>`** (build, flash, capture), **`make lab.<name>.build`**, **`.flash`**, **`.capture`**.
- Validate with `python scripts/lab_registry.py validate` and inspect with `make config.labs`.
- Optional: add `[command.lab.<name>]` sections to `config/host.defaults.ini` for the agent command catalog.
- Do **not** point `[firmware].source` at a lab tree or overwrite bootstrap `artifacts/device/bootstrap/current.json`.
- Lab boot markers differ from `V2_BOOTSTRAP_READY`; define a per-lab ready line in the lab README (e.g. `LAB02_READY build=<id>`).

Manual `idf.py` remains valid for debugging; registry `build_dir` values must match `-B` paths, for example:

```text
idf.py -C client/labs/lab-02-bounded-inbox -B build/labs/lab-02-bounded-inbox build flash monitor
```

## Lab version manifest

Each firmware and server tree should carry a **`manifest.lab.json`** (schema version may evolve). Minimum fields:

| Field | Description |
|-------|-------------|
| `document` | e.g. `family-link.lab-manifest/v1` |
| `lab_id` | `LAB-02` |
| `lab_revision` | Human increment when fixture or protocol changes (`v1`, `v2`, …) |
| `firmware_path` | Repo-relative project path |
| `server_path` | Repo-relative path or `none` |
| `fixture_schema` | e.g. `inbox-summary/v1` |
| `fixture_revision` | Revision of JSON under `fixtures/` or `server/.../fixture/` |
| `protocol` | Wire or stub name; `local-only` for Phase A |
| `trace_schema` | e.g. `lab-trace/v1` (define in assistant guide) |
| `reference_archive` | Optional note: which legacy demo informed behavior (no path required in product docs) |

At run time, record the ESP-IDF / app **`build_id`** in `docs/labs/evidence/<lab-slug>/runs/<run-id>/run-manifest.json` per [run manifest v1](schemas/run-manifest.v1.md). Parsed timing lives in [metrics v1](schemas/metrics.v1.md).

## Promotion to product

```text
client/labs/<slug>/src/*.c  →  client/shared/<area>/  →  client/device/
```

Update the relevant experience, contract, or UI assessment with a link to the evidence report. Promote numeric limits to [`FOUNDATION-LVGL-BUDGET`](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md) only after owner review.

## What not to do

- Instrument or edit demos inside the v1 archive.
- Store large videos or WAV/sketch blobs in `docs/` without `.gitignore` and a manifest hash.
- Treat demo PASS markers or static estimates as lab pass/fail gates.
- Use production deployment YAML or live archives as lab data sources.
