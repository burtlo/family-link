# ADR-005: Retired 64 GB SDXC geometry and epochs

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-10-07 |
| **Labels** | **Observed** (card detection, H37 read-only); **Specification** (retirement policy) |

## Context

Early attached-storage work used an overspec **64 GB** microSDXC card:

- **121,503,744** sectors × 512 bytes (62,209,916,928 bytes reported)
- H37 Stage A on that card: MBR partition to end of media, **exFAT** signature at metadata reads — epoch `43d7e2e5792ca6c1e494ff7cb06f3353` ([`h37-current-baseline-20261006`](../evidence/attached-storage-qualification/h37-current-baseline-20261006/summary.json))
- H38 attempts on that geometry produced incomplete or diagnostic failures ([`h38-stack-failure-20261007`](../evidence/attached-storage-qualification/h38-stack-failure-20261007/README.md), [`h38-framing-failure-20261007`](../evidence/attached-storage-qualification/h38-framing-failure-20261007/README.md))

Espressif’s published SENSOR accessory spec cites **up to 32 GB** SDHC. Operator procured supported **16/32 GB** cards; software was updated for **per-card geometry binding** ([`plans/h38-sdhc-geometry-qualification.md`](../plans/h38-sdhc-geometry-qualification.md)).

## Decision

1. **Retire** the 64 GB card lineage for all **new** prepare/build/run work. Do not reuse:
   - Hard-coded `card_sector_count = 121,503,744`
   - H37 epoch `43d7e2e5792ca6c1e494ff7cb06f3353` and dependent H38 intent constants from that era
   - Public baselines tied to exFAT full-card MBR on that media
2. **Archive only:** existing evidence folders remain for forensics; they are **not authority** for current 32 GB track ([`evidence/attached-storage-qualification/README.md`](../evidence/attached-storage-qualification/README.md)).
3. **Current authority:** 32 GB SDHC track — e.g. **61,071,360** sectors on documented H35 pass ([`h35-32gb-20261007`](../evidence/attached-storage-qualification/h35-32gb-20261007/summary.json)); H38 Stage B pass on [`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/README.md).
4. **Frozen H38 v1 test volume** unchanged across cards: `volume_start_lba=32768`, `volume_sector_count=1048576` (512 MiB window) — only **total card sector count** and identity binding are per-card.

## Consequences

- Scripts (`h37_*`, `h38_*`) must read measured geometry from the selected H35/H37 capture, not the retired constant ([`plans/h38-sdhc-geometry-qualification.md`](../plans/h38-sdhc-geometry-qualification.md)).
- [`hardware/limitations.md`](../hardware/limitations.md) and [`AGENTS.md`](../AGENTS.md) repeat this retirement for operators.
- **Speculation:** The physical 64 GB card may remain in a parts bin; it must not be inserted for qualification without treating it as a **new** unsupported geometry policy decision.

## Evidence and references

- [`plans/h38-bounded-sd-filesystem.md`](../plans/h38-bounded-sd-filesystem.md) — retirement notice and 32 GB pass
- [`plans/continuation-checkpoint-2026-10-05.md`](../plans/continuation-checkpoint-2026-10-05.md) — last 64 GB H35 detection record
- [`hardware/storage-summary.md`](../hardware/storage-summary.md) — retired vs current track table
