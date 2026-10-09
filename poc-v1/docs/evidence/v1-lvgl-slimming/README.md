# v1 LVGL slimming — evidence

Plan: [`docs/plans/v1-lvgl-slimming.md`](../../plans/v1-lvgl-slimming.md) — **status `partial` shipped** (2026-10-08). Decision: [phase3-decision/summary.md](phase3-decision/summary.md).

**Montserrat in x02 image (linked):** Phase 0 / Phase 1 → **1** size (14). **Shipped (Phase 2)** → **4** typical (14, 16, 24, 28). See plan [Findings](../../plans/v1-lvgl-slimming.md#findings--fonts-flash-and-desk-ux-2026-10-08).

| Phase | App bytes | Free bytes | LVGL lib flash | Operator sign-off |
|-------|----------:|-----------:|---------------:|-------------------|
| [0 baseline](phase0-baseline/summary.md) | 1,484,272 | 51,728 (3.4%) | 312,310 | n/a (desk-only) |
| [1 flags](phase1-flags/summary.md) | 1,414,464 | 121,536 (7.9%) | 245,075 | **2026-10-08** |
| [2 fonts](phase2-fonts/summary.md) | 1,496,384 | 39,616 (2.6%) | 326,933 | pending (optional) |
| [3 decision](phase3-decision/summary.md) | — | — | — | desk closed **2026-10-08** |

**Canonical x02 flash config:** `firmware/sdkconfig.defaults.v1` (Phase 1 flags + Phase 2 fonts).
