# Phase 1 — LVGL feature flags (x02)

Captured **2026-10-08** after implementing `sdkconfig.defaults.v1` + per-build-dir `sdkconfig`. Commit: see [`git-rev.txt`](git-rev.txt).

## Delta vs [Phase 0](../phase0-baseline/summary.md)

App partition **1,536,000** bytes.

| Metric | Phase 0 | Phase 1 | Δ |
|--------|--------:|--------:|--:|
| App bytes | 1,484,272 | **1,414,464** | **−69,808** |
| Free bytes | 51,728 (3.4%) | **121,536** (7.9%) | **+69,808** |
| Flash `.text` | 1,101,922 | 1,036,734 | −65,188 |
| Flash `.rodata` | 253,684 | 249,140 | −4,544 |
| IRAM used | 16,383 | 16,383 | 0 |
| `liblvgl__lvgl.a` flash | 312,310 | **245,075** | **−67,235** |

Partition check: **OK** (no overflow; ~8% headroom).

## Operator validation

**PASS (2026-10-08)** — desk BOX-3: PIN, carousel, record/send, and **messages** exercised on the Phase 1 image (`sdkconfig.defaults.v1` flags + Montserrat 14 only / fallbacks). Six-journey table in the plan; no glyph or layout regressions reported.

## Artifacts

- [`sdkconfig.defaults.v1`](sdkconfig.defaults.v1) (copy of firmware fragment)
- [`phase1-changelog.md`](phase1-changelog.md)
- [`x02-size.txt`](x02-size.txt)
- [`x02-size-components.txt`](x02-size-components.txt)
