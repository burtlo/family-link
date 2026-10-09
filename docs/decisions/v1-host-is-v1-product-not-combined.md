# ADR-004: v1 host is `v1_product`, not `combined`

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-10-08 |
| **Labels** | **Specification** (pilot target); **Observed** (route/API differences in tree) |

## Context

The repository contains multiple Python servers:

| Tree | Role |
|------|------|
| `demos/server/combined/` | Earlier glue host: parent `/app/`, island demos, legacy message shapes |
| `demos/server/v1_product/` | FastAPI product server: disk archive, admin routes, PCM chunk subset, WebSocket inbox |
| Island servers (`h34_message_store`, etc.) | Experiments; incompatible contracts |

[`plans/v1-product-spec.md`](../plans/v1-product-spec.md) still lists “Host today: `demos/server/combined/`” in References — that reflected historical tryouts. Server recovery evidence and architecture docs now treat **`v1_product` as authoritative** for v1 pilot behavior.

## Decision

1. **v1 product pilot and documentation** should run and cite **`python -m demos.server.v1_product`** (see [`demos/server/v1_product/README.md`](../../demos/server/v1_product/README.md)), not `demos.server.combined`, unless explicitly reproducing a legacy island demo.
2. **Contract authority** for hangout, session, inbox, multipart `POST /v1/messages`, outgoing receipt lookup, and admin PIN reset is `demos/server/v1_product/server.py` and modules listed in [`architecture/server.md`](../architecture/server.md).
3. **`combined` remains** for demo parity (e.g. h18 web twin workflows in [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md)) but is **not** the v1 ship target. Do not assume route parity between combined and v1_product ([`plans/product-00-baseline-gate.md`](../plans/product-00-baseline-gate.md)).
4. **Firmware x02** targets v1_product API shapes; flash shim `x02` → `firmware/v1/` ([`AGENTS.md`](../AGENTS.md)).

## Consequences

- New server features (archive, chunk resume, state SQLite) land in `v1_product` first.
- Docs and runbooks should link [`architecture/server.md`](../architecture/server.md) and v1_product README; update stray “combined-only” instructions when found.
- **Speculation:** A future single “production entrypoint” might wrap v1_product; no merge-back into combined is planned in this ADR.

## Evidence and references

- [`evidence/product-no-storage/03-server-recovery/README.md`](../evidence/product-no-storage/03-server-recovery/README.md)
- [`features/server-disk-archive.md`](../features/server-disk-archive.md)
- [`plans/product-no-storage-roadmap.md`](../plans/product-no-storage-roadmap.md)
- [`STORAGE.md`](../hardware/STORAGE.md) — notes combined-era data paths (historical); v1 store layout in [`SERVER-MESSAGE-STORAGE.md`](../SERVER-MESSAGE-STORAGE.md)
