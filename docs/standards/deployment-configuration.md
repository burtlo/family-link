# Deployment configuration

Desk roster data (endpoints, kits, users, aliases) lives in
[`config/deployment/`](../../config/deployment/). Host path pointers are in
[`config/host.defaults.ini`](../../config/host.defaults.ini) section
`[config.deployment]`.

## Schema and validation (two layers)

| Layer | Source | What it checks |
|-------|--------|----------------|
| **Structure** | [`config/deployment/deployment.schema.v1.json`](../../config/deployment/deployment.schema.v1.json) | Required keys, types, `additionalProperties: false`, id patterns |
| **Semantics** | [`scripts/deployment_config.py`](../../scripts/deployment_config.py) | Unique ids/tokens, alias and kit `endpoint_id` targets, peer links, one kit per endpoint |

`python scripts/deployment_config.py validate` runs both layers.
`make config.deployment` prints a redacted roster snapshot.

Optional top-level `$schema` in a YAML file may point at the JSON Schema file
for editor completion.

## Inspection

```text
make config.deployment
python scripts/deployment_config.py catalog
python scripts/deployment_config.py validate
python scripts/deployment_config.py dump
```

## Files

| File | Role |
|------|------|
| `config/deployment/example.yaml` | Committed sample (generic placeholders) |
| `config/deployment/local.yaml` | Desk roster (gitignored); loaded when present |
| `config/deployment/deployment.schema.v1.json` | Versioned structural contract |

Copy `example.yaml` to `local.yaml` and expand to your full kit count.
