# Project configuration

Family Link separates two configuration domains:

1. **Repository mechanics and host defaults** live in root
   `project.defaults.ini`. These settings describe how to install, inspect,
   build, test, evaluate, and flash the active project as those operations
   become available.
2. **Product-domain configuration**—people, kits, devices, deployments, and
   secrets—belongs in purpose-specific YAML files. Those documents may contain
   nested entities and relationships and are not repository command metadata.

## Resolution

`scripts/project_config.py` is the stdlib-only loader. It uses the root
`project.defaults.ini` unless `PROJECT_CONFIG` names another file. A relative
`PROJECT_CONFIG` path is resolved from the repository root.

INI section names are lowercase dotted prefixes and keys are lowercase. Every
value has an automatic environment override formed by uppercasing its fully
qualified name and replacing dots with underscores:

```text
[idf] path                         -> IDF_PATH
[command.install] timeout_seconds -> COMMAND_INSTALL_TIMEOUT_SECONDS
```

An environment variable is authoritative even when its value is empty. Scripts
should request logical names such as `idf.path`; they should not implement
their own environment aliases.

## Commands and flows

Each `[command.<name>]` section defines:

- `argv`: a non-empty JSON array of argument strings (never a shell command);
- `cwd`: a repository-relative working directory without `..`;
- `timeout_seconds`: a positive integer.

Each `[flow.<name>]` section lists command names in `commands`. The initial
contract exposes setup, implementation, and post-repair flows. Add build,
flash, or evaluate commands only when the active product tree implements them;
absence means that the operation is not yet available.

This borrows the useful repository-command contract from implementation-flow
systems without adopting a flow engine, job ledger, or agent runtime. Make
targets remain the executable public interface.

## Validation and inspection

```text
make config
make config CONFIG=path/to/custom.ini
make test
python scripts/project_config.py dump
python scripts/project_config.py validate
python scripts/project_config.py get idf.path
python scripts/project_config.py command install
python scripts/project_config.py flow implementation
```

Configuration errors return a non-zero process status.

## Agent snapshot (`make config`)

`make config` prints a versioned JSON document (`family-link.project-config.snapshot/v1`)
with separate top-level keys:

| Key | Role |
|-----|------|
| `document` | Snapshot format identifier |
| `meta` | Loaded file path, `PROJECT_CONFIG`, override rules, product YAML boundary |
| `catalog` | Types, requiredness, env var names, command/flow section shapes |
| `resolved` | Effective values after INI + environment overrides |
| `provenance` | Per-setting `file` vs `environment` for `resolved.settings` |

Use `python scripts/project_config.py catalog` for catalog + meta only.
