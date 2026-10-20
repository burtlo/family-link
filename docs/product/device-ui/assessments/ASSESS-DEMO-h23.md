# LVGL assessment — demo h23 (record idle stop)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
**Pattern ledger:** **SKIP** (LVGL); **ADAPT** (audio policy)

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-h23` |
| **Scope** | Island demo `h23` (toggle record + idle auto-stop upload) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Related specs** | [`recording-send`](../../../v1-assessments/recording-send.md) |

## 1. User-visible scope

Red circle tap start/stop; **30 s** silence ends take and uploads; no sketch UI.

## 2. LVGL scope

**None** — no `lv_*` in demo; status via `board_status` / hardware chrome only.

## 3. Ledger B (critical for v2)

| Resource | Size | Risk |
|---|---|---|
| `PCM_CAP` | `SAMPLE_RATE × 2 × 300 s` ≈ **9.6 MiB** | **AVOID** copying reservation model |
| Upload | Multipart WAV | Stream or chunk per resource envelope |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | **ADOPT** idle-stop **policy** for child UX; implement with streaming buffer |
| P0 | **AVOID** holding max-length PCM in RAM |
| — | Skip for LVGL heap lab |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
