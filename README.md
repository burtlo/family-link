# Family Link

Family Link is a **desk async audio mailbox**: ESP32-S3-BOX-3 endpoints at home talk to a household server so family members can leave and play voice messages without live calls. This repository is the **active v2 product tree**—specifications, standards, host tooling, product firmware bootstrap, and instrumented device labs—built on lessons from an earlier proof-of-concept.

## How the pieces fit together

| Layer | Role |
|-------|------|
| **Product intent** | Owner-approved experience specs, device UI, and client/server **communication contracts** live under [`docs/product/`](docs/product/). Portable engineering rules and v1 **evidence assessments** live under [`docs/standards/`](docs/standards/) and [`docs/v1-assessments/`](docs/v1-assessments/). |
| **Shipping firmware** | [`client/device/`](client/device/) is the v2 **product** ESP-IDF app (bootstrap today; grows into the full mailbox experience). Reviewed modules from labs may later be promoted into shared client code. |
| **Device labs** | [`client/labs/`](client/labs/) holds **one ESP-IDF project per lab**—focused, heavily instrumented firmware for paging, carousel, capture, and similar risks. Lab index and workflow: [`docs/labs/README.md`](docs/labs/README.md). Layout and evidence paths: [`docs/labs/repository-layout.md`](docs/labs/repository-layout.md). |
| **Lab servers** | [`server/labs/`](server/labs/) holds **fixture servers** (e.g. paging APIs) with isolated test data—not production household archives. Pairs with matching lab firmware over the network. |
| **Configuration** | [`config/`](config/) drives the host: [`host.defaults.ini`](config/host.defaults.ini) (commands and flows), [`deployment/`](config/deployment/) (desk roster YAML), and [`labs.registry.yaml`](config/labs.registry.yaml) (`make lab.<name>` targets). See [`docs/standards/project-configuration.md`](docs/standards/project-configuration.md). |
| **Host automation** | Root [`Makefile`](Makefile) and [`scripts/`](scripts/) implement install, test, build, deployment validation, and lab flash/capture. Python deps: [`requirements.txt`](requirements.txt). |
| **v1 POC archive** | A **read-only** snapshot of the original v1 firmware, demos, and legacy docs is preserved at the repository root for historical reference. It is **not** the active product tree—do not implement forward work there. Extract facts into `docs/` or new code instead. |

Labs prove behavior and gather evidence; **contracts and specs** define what the product must do; **`client/device/`** is where the shipping app lands once modules are ready.

## Top-level directories

| Path | Contents |
|------|----------|
| [`client/`](client/) | ESP-IDF **device** (product) and **labs** firmware; optional shared client code as it is promoted. |
| [`server/`](server/) | **Lab fixture servers** under `server/labs/`; no production server implementation in this layout yet. |
| [`docs/`](docs/) | Active documentation index—start at [`docs/README.md`](docs/README.md). |
| [`config/`](config/) | Host INI, deployment examples, lab registry, and JSON schemas. |
| [`scripts/`](scripts/) | Python helpers used by Make (`project_config`, firmware, labs, deployment). |
| [`.cursor/`](.cursor/) · [`.agents/`](.agents/) | Cursor/Codex **skills** and rules (e.g. archive boundaries for agents). |
| [`.vscode/`](.vscode/) | Editor and automation terminal profiles (Windows PowerShell 7, macOS zsh). |
| `poc-v1/` | Frozen **v1 proof-of-concept archive** (reference only). |

Generated trees such as `build/`, `artifacts/`, and `logs/` are described in [`docs/labs/repository-layout.md`](docs/labs/repository-layout.md) and are not checked in.

## Where to begin

**Contributors (humans)**

1. Read [`docs/README.md`](docs/README.md) for the doc map (product workflow, standards, labs).
2. Run `make help` and `make install` from the repo root; use `make config.host`, `make config.deployment`, or `make config.labs` to inspect configuration.
3. For hardware work: [`docs/standards/device-build-flash.md`](docs/standards/device-build-flash.md); for labs, `make lab.help` and [`docs/labs/assistant-guide.md`](docs/labs/assistant-guide.md).
4. Put new product firmware in [`client/device/`](client/device/); put new lab work in [`client/labs/<slug>/`](client/labs/) and matching [`server/labs/<slug>/`](server/labs/) per the layout doc.

**Coding agents**

1. Read [`docs/AGENTS.md`](docs/AGENTS.md) before changing behavior or code (authority, where to put work, shell conventions).
2. Follow [`.cursor/rules/poc-v1-reference-archive.mdc`](.cursor/rules/poc-v1-reference-archive.mdc): the v1 archive is **read-only**; do not modify it or link to its paths in new active artifacts—extract and rewrite under `docs/` or the v2 trees.
3. Use the skills indexed in [`docs/README.md`](docs/README.md) when authoring experience specs, device UI, or communication contracts.

When sources conflict, owner-approved specs under `docs/product/` and explicit user decisions win over legacy as-built code in the archive.
