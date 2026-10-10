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

## Physical kit selection

For flash operations, aliases are kit nicknames describing positions, not owners
or logged-in users. Resolve alias -> endpoint -> kit, then match `kit.usb_serial`
against currently enumerated ports. A port name is transient and is not saved as
identity. Missing bindings, duplicate bindings and ambiguous connected matches
are rejected before writing firmware.

`make devices` displays the current mapping. `make device.bind KIT=<nickname>
PORT=<port>` explicitly binds one connected kit in the resolved local roster.
This honors host roster-path overrides and never edits the example roster.
Flash operations do not change bindings. Endpoint tokens, PINs and secret profiles
are neither printed by device discovery nor embedded in the bootstrap image.
