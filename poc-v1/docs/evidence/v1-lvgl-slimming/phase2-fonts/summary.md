# Phase 2 — v1 Montserrat font diet (x02)

Captured **2026-10-08** after updating `firmware/sdkconfig.defaults.v1` with the locked v1 ladder (minus 32 — see below). Commit: see [`git-rev.txt`](git-rev.txt).

## Delta vs [Phase 0 baseline](../phase0-baseline/summary.md) (original x02 image)

App partition **1,536,000** bytes.

| Metric | Phase 0 | Phase 2 | Δ |
|--------|--------:|--------:|--:|
| App bytes | 1,484,272 | **1,496,384** | **+12,112** |
| Free bytes | 51,728 (3.4%) | **39,616** (2.6%) | −12,112 |
| Flash `.text` | 1,101,922 | 1,036,698 | −65,224 |
| Flash `.rodata` | 253,684 | **331,092** | **+77,408** |
| IRAM used | 16,383 | 16,383 | 0 |
| `liblvgl__lvgl.a` flash total | 312,310 | **326,933** | +14,623 |

Phase 0’s checked-in defaults list 14–48, but the **built** x02 image at Phase 0 effectively linked **Montserrat 14 only** (same as Phase 1). Phase 2 adds explicit **14, 16, 22, 24, 28** in Kconfig; the linker typically retains **14, 16, 24, 28** (four tables). Further font stripping yields only **~15–46 KiB** per size — low ROI vs Phase 1’s **~70 KiB** widget win (see plan Findings).

## Delta vs [Phase 1 flags](../phase1-flags/summary.md)

| Metric | Phase 1 | Phase 2 | Δ |
|--------|--------:|--------:|--:|
| App bytes | 1,414,464 | **1,496,384** | **+81,920** |
| Free bytes | 121,536 (7.9%) | **39,616** (2.6%) | −81,920 |
| `liblvgl__lvgl.a` flash total | 245,075 | **326,933** | +81,858 |

Typography matches v1 source (no `LV_FONT_DEFAULT` fallback for 16/22/24/28). Trade-off: **~80 KiB** flash vs Phase 1’s 14-only ladder.

## Factory slot limit — Montserrat 32

Enabling **all six** locked sizes (including 32) with Phase 1 flags **overflows** the factory app partition by **0x18b0** (~6.3 KiB). Shipped fragment disables **32**; `v1_auth.c` already falls back to **28** for PIN entry.

## Operator validation

**Pending** after re-flash with this image. Phase 1 sign-off (messages, core journeys) recorded in the plan on **2026-10-08** for the prior image.

## Island smoke

`p13_ui_showcase` builds in `build/p13_ui_showcase/` (full shared font ladder unchanged).

## Artifacts

- [`sdkconfig.defaults.v1`](sdkconfig.defaults.v1)
- [`x02-size.txt`](x02-size.txt)
- [`x02-size-components.txt`](x02-size-components.txt)
- [`x02-archives.txt`](x02-archives.txt)
- [`x02-bin-wc.txt`](x02-bin-wc.txt)

Guard: `make check-v1-fonts` / `scripts/check_v1_fonts.py`.
