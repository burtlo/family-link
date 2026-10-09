# Phase 3 — LVGL slimming decision record

**Date:** 2026-10-08  
**Plan:** [`docs/plans/v1-lvgl-slimming.md`](../../plans/v1-lvgl-slimming.md)  
**Canonical x02 config:** [`firmware/sdkconfig.defaults.v1`](../../../firmware/sdkconfig.defaults.v1) merged via [`firmware/CMakeLists.txt`](../../../firmware/CMakeLists.txt) for `x02_product_shell`.

## Decision

**Ship Phase 1 + Phase 2** as the standard x02 product build. Close the LVGL slimming plan as **`partial`**: desk evidence and config are complete; **Phase 2 operator re-flash sign-off** remains optional follow-up, not a blocker for merging config.

| Choice | Rationale |
|--------|-----------|
| **Keep** `sdkconfig.defaults.v1` widget disables (`CONFIG_LV_USE_*`, `LV_BUILD_DEMOS/EXAMPLES`) | **~70 KiB** app savings vs Phase 0; validated on desk 2026-10-08 (messages, PIN, carousel). |
| **Keep** five-size Montserrat Kconfig (14, 16, 22, 24, 28; **32 off**) | Restores intended typography vs Phase 1’s single-font + fallback UX. |
| **Do not** pursue further font-only slimming in-tree | **~15–46 KiB** per removed size; **39,616** bytes factory free at Phase 2 — insufficient ROI vs UX. |
| **Do not** change partition table in this plan | Opus / larger codec headroom: [`x02-opus-partition` Phase 4](../../x02-opus-partition/phase4-partition-strategies.md). |
| **Do not** expect IRAM relief from LVGL work | **16,383 / 16,384** bytes IRAM unchanged across phases. |

## Size outcome (factory app 1,536,000 bytes)

| Phase | App bytes | Free bytes | Δ vs Phase 0 | Linked Montserrat (typical) |
|-------|----------:|-----------:|-------------:|----------------------------|
| 0 baseline | 1,484,272 | 51,728 | — | **1** (14) |
| 1 flags | 1,414,464 | 121,536 | **−69,808** | **1** (14) |
| 2 fonts (shipped) | 1,496,384 | 39,616 | **+12,112** | **4** (14, 16, 24, 28) |

**Net vs pre-slimming baseline (Phase 0):** slightly **larger** app (+12 KiB) with **much** smaller LVGL `.text` and **larger** font `.rodata`. **Net vs smallest experimental image (Phase 1):** +82 KiB for correct type.

## Configuration split (locked)

| Build | Font policy |
|-------|-------------|
| **x02** (`build/x02_product_shell`) | `sdkconfig.defaults` + **`sdkconfig.defaults.v1`** |
| **Islands / p13** (`build/p13_ui_showcase`, etc.) | `sdkconfig.defaults` only — full **14–48** ladder |

Guards: `make check-v1-fonts`, `make check-v1-parity` (timing unchanged).

## Operator sign-off

| Phase | Status |
|-------|--------|
| 1 | **PASS** 2026-10-08 (desk BOX-3; messages OK) |
| 2 | **Pending** — re-flash Phase 2 image and run six-journey table when convenient |

## Follow-ups (out of scope here)

1. **Opus / long audio on device** — partition and dependency budget, not more LVGL flags.
2. **Optional:** remove `CONFIG_LV_FONT_MONTSERRAT_22=y` if map never links 22 (~tens of KiB max).
3. **Optional:** enable Montserrat **32** only after factory slot grows or other flash is reclaimed (~6.3 KiB short today).

## Evidence index

- [Phase 0 baseline](../phase0-baseline/summary.md)
- [Phase 1 flags](../phase1-flags/summary.md)
- [Phase 2 fonts](../phase2-fonts/summary.md)
