# shared/v1 — v1 product contracts

Single source for timing values shared between firmware (`firmware/v1/`) and the web twin (`demos/server/v1_product/web/`).

## timing.yaml

Edit `timing.yaml` when changing connect probe/retry, carousel snap duration, toast delay, record caps, PIN lockout, or sleep/dim policy.

## Regenerate outputs

From the repo root:

```bash
make v1-timing
```

This runs `shared/v1/gen_timing.py` and writes:

| Output | Consumer |
|--------|----------|
| `firmware/v1/v1_timing.h` | x02 / v1 modules (Phase 2+) |
| `demos/server/v1_product/web/v1_timing.js` | `box.js` (ES module import) |

Commit both generated files with your YAML change.

## Dependencies

- Python 3.9+
- PyYAML (`pip install pyyaml` or `make install-server`)
