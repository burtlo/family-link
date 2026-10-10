# Project configuration

Family Link uses a single **`config/`** tree with two domains:

1. **Host / repository mechanics** — [`config/host.defaults.ini`](../../config/host.defaults.ini):
   install, inspect, build, test, evaluate, and flash mechanics as they become
   available.
2. **Product deployment roster** — YAML under [`config/deployment/`](../../config/deployment/):
   people, kits, endpoints, aliases (see
   [`deployment-configuration.md`](deployment-configuration.md)).

## Resolution

`scripts/project_config.py` loads `config/host.defaults.ini` unless
`PROJECT_CONFIG` names another file. Relative `PROJECT_CONFIG` paths resolve from
the repository root.

INI section names are lowercase dotted prefixes and keys are lowercase. Every
value has an automatic environment override formed by uppercasing its fully
qualified name and replacing dots with underscores:

```text
[idf] path                              -> IDF_PATH
[config.deployment] example            -> CONFIG_DEPLOYMENT_EXAMPLE
[command.install] timeout_seconds        -> COMMAND_INSTALL_TIMEOUT_SECONDS
```

An environment variable is authoritative even when its value is empty. Scripts
should request logical names such as `idf.path`; they should not implement
their own environment aliases.

## Commands and flows

Each `[command.<name>]` section defines:

- `argv`: a non-empty JSON array of argument strings (never a shell command);
- `cwd`: a repository-relative working directory without `..`;
- `timeout_seconds`: a positive integer.

Host inspection commands:

| Make target | Role |
|-------------|------|
| `make config.host` | Host INI snapshot (`family-link.project-config.snapshot/v1`) |
| `make config.deployment` | Roster snapshot (`family-link.deployment.snapshot/v1`, redacted) |
| `make config` | Alias for `make config.host` |

Each `[flow.<name>]` section lists command names in `commands`. Add build,
flash, or evaluate commands only when the active product tree implements them.

## Validation and inspection

```text
make config.host
make config.deployment
make config.host CONFIG=path/to/custom.ini
make test
python scripts/project_config.py dump
python scripts/project_config.py validate
python scripts/deployment_config.py validate
python scripts/deployment_config.py dump
```

Configuration errors return a non-zero process status.

## Agent snapshot (`make config.host`)

`make config.host` prints a versioned JSON document
(`family-link.project-config.snapshot/v1`) with separate top-level keys:

| Key | Role |
|-----|------|
| `document` | Snapshot format identifier |
| `meta` | Loaded file path, `PROJECT_CONFIG`, override rules, product YAML boundary |
| `catalog` | Types, requiredness, env var names, command/flow section shapes |
| `resolved` | Effective values after INI + environment overrides |
| `provenance` | Per-setting `file` vs `environment` for `resolved.settings` |

Use `python scripts/project_config.py catalog` for catalog + meta only.
