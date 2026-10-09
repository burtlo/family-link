# Family desk link (working title)

A **desk async audio mailbox** for a small hangout: each person signs in on an **ESP32-S3-BOX-3** endpoint; Lynn also uses **web admin** on the home server — without borrowing the other parent’s phone.

Photos, live hangout, and drawing are **later** than the current v1 merge (async audio only). See [`docs/product/vision.md`](docs/product/vision.md) and [`docs/plans/v1-product-spec.md`](docs/plans/v1-product-spec.md).

The folder name is a placeholder. Name candidates: [`docs/NAMES.md`](docs/NAMES.md). Not a continuation of the older e-ink messenger repos.

## Quick start

```bash
make v1-server      # leave running
make demo-v1        # smoke test
make x02            # build product firmware
make flash DEMO=x02 # flash when hardware connected
```

Admin UI: `http://<server-lan-ip>:8080/app/v1.html` · Box web twin: `/box/`

## Documentation

**Index:** [`docs/README.md`](docs/README.md)

| For | Start here |
|-----|------------|
| Agents | [`docs/AGENTS.md`](docs/AGENTS.md) |
| Product | [`docs/product/vision.md`](docs/product/vision.md) |
| What works (v1) | [`docs/features/README.md`](docs/features/README.md) |
| Backlog | [`docs/plans/README.md`](docs/plans/README.md) |
| Hardware | [`docs/hardware/overview.md`](docs/hardware/overview.md) |
| Open decisions | [`docs/decisions/open-questions.md`](docs/decisions/open-questions.md) |

## Status

**Hardware:** BOX-3 units on desk; USB serial `/dev/cu.usbmodem*` (Mac).

**Software:** Product host `demos/server/v1_product`, firmware `firmware/v1` (flash `x02`), plus island demos — map: [`docs/DEMO-MAP.md`](docs/DEMO-MAP.md). Replace stock wake-word firmware before remote deploy.
