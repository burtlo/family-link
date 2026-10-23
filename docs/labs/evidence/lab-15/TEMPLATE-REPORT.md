# LAB-15 — capture memory envelope

| Field | Value |
|-------|--------|
| Lab version | LAB-15/v1 = firmware _build-id_ + fixture lab15-envelope/v1 |
| Target | _kit name_ / BOX-3 / ESP32-S3 |
| Arm | _e.g. catalog_pcm_none_none_ |
| Workflow | _PASS / FAIL / INCOMPLETE_ |
| Experiment | _PASS / FAIL / UNMEASURED_ |

## Question

Can the desk reserve **5,760,000** bytes PCM plus sketch/canvas while **catalog fonts and montage UI** are mounted?

## Measured peak (hardware `provenance` only)

| Phase | Internal largest free | PSRAM largest free | LVGL largest free | PCM reserved | Result |
|-------|----------------------:|-------------------:|------------------:|-------------:|--------|
| After boot | | | | 0 | |
| After UI montage | | | | 0 | |
| After audio reserve | | | | 5760000 | |
| At duration_cap | | | | 5760000 | |

## Declared next allocation vs largest free

_From lab README sums; compare at **after audio reserve** row._

## Opus / SD exploratory (if run)

| Arm | App bytes | Partition free | Boot | Notes |
|-----|----------:|---------------:|------|-------|
| X-opus-link | | | | |
| X-sdmmc-spill | | | | |

## Scenarios

| ID | Level | Result |
|----|-------|--------|
| E-baseline-ui | | |
| E-catalog-ui | | |
| E-catalog-peak-only | | |
| E-chunk-fragmentation | | |

## Decision

_OPEN — lab evidence only._

Recommended follow-on: _PCM ring feasible / needs SD / needs codec / needs partition change / block P7 until …_

## Artifacts

- Run: `docs/labs/evidence/lab-15/runs/<run-id>/`
- Serial: `logs/device/labs/lab-15/<run-id>/serial.txt`
