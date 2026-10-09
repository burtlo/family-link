# Architecture and product decisions

Durable decisions for Family Link v1 and hardware qualification. Each record is an **ADR-style** note: context, decision, consequences, and links to evidence.

**Unresolved items** live in [`open-questions.md`](open-questions.md) (migrated from [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md)).

**Normative product contract** remains [`REQUIREMENTS.md`](../REQUIREMENTS.md) and [`plans/v1-product-spec.md`](../plans/v1-product-spec.md). Decisions here do not replace those documents; they index what was locked from requirements, specs, and qualification runs.

## Decision index

| ID | Title | Status | Date |
|----|-------|--------|------|
| [ADR-001](async-audio-v1-scope.md) | v1 merge is async audio only | Accepted | 2026-09-04 |
| [ADR-002](no-wake-word-mic-only-while-recording.md) | No wake word; mic only while recording | Accepted | 2026-09-04 |
| [ADR-003](storage-qualification-status.md) | Storage qualification status (H32, H38, product SD) | Accepted | 2026-10-08 |
| [ADR-004](v1-host-is-v1-product-not-combined.md) | v1 host is `v1_product`, not `combined` | Accepted | 2026-10-08 |
| [ADR-005](retired-64gb-sd-geometry.md) | Retired 64 GB SDXC geometry and epochs | Accepted | 2026-10-07 |

## Related indexes

| Topic | Location |
|-------|----------|
| Open questions | [`open-questions.md`](open-questions.md) |
| Device incidents (INT-001–015) | [`device-incidents.md`](device-incidents.md) |
| Agent operating notes | [`AGENTS.md`](../AGENTS.md) |
| Hardware limits | [`hardware/limitations.md`](../hardware/limitations.md) |
| Attached storage operator guide | [`ATTACHED-STORAGE.md`](../hardware/ATTACHED-STORAGE.md) |

## How to add a decision

1. Copy the section headings from an existing ADR in this folder.
2. Assign the next `ADR-NNN` id in this README table.
3. Label claims: **Observed** (device/evidence), **Specification** (approved contract), **Inferred** (reasonable from code/docs), **Speculation** (not yet evidenced).
4. Link sanitized evidence under [`evidence/`](../evidence/) — never commit private captures or credentials.
