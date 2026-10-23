# X02 2.125 MiB device boot — evidence stub

Copy this tree to a dated run directory, e.g. `docs/evidence/x02-opus-partition/device-boot-2026/` or `device-boot-2026-10-22/`. Fill after hardware execution. Do **not** commit full-flash backups, NVS dumps, Wi-Fi credentials, or raw serial with secrets.

**Plan:** [`x02-opus-2125-device-boot.md`](../../plans/x02-opus-2125-device-boot.md)

## Run identity

| Field | Value |
|---|---|
| Git commit | |
| ESP-IDF version | |
| Target / flash size | esp32s3 / 16 MiB |
| Partition CSV path | |
| Device label (sanitized) | |
| Serial port | |
| Operator date (UTC) | |

## E1 — Host build matrix

| Demo | App bytes | Factory slot (`0x220000`) | Free bytes | Beyond policy (`334,234 B`) | Link |
|---|---:|---:|---:|---:|---|
| x02 | | 2,228,224 | | | build-sizes.txt |
| x02-opus-probe | | 2,228,224 | | | |
| x02-opus-dep | | 2,228,224 | | | |

**E1 result:** PASS / FAIL

## E2 — Device boot (x02)

- Backup 16 MiB SHA-256 (private path only): 
- Backup NVS SHA-256 (private path only): 
- Flashed artifacts SHA-256 (bootloader, table, app): 
- Readback verification: PASS / FAIL
- Boot capture file (private) + metadata hash: 
- Boot rubric: PASS / FAIL (loader, `Calling app_main`, identity markers, no panic loop)
- Restore + full readback: PASS / FAIL

**E2 result:** PASS / FAIL

## E3 — Device boot (x02-opus-probe)

(Same fields as E2 for probe demo.)

**E3 result:** PASS / FAIL

## E4 — Smoke (optional)

| Check | Result | Notes |
|---|---|---|
| Wi-Fi | | |
| PIN | | |
| Carousel | | |
| Record entry | | |

## Outbox / NVS notes

- NVS region unchanged Y/N
- Outbox partition touched at boot Y/N (describe if yes)
- Unexpected format/mount Y/N

## Summary

One paragraph: overall PASS/FAIL, whether layout is **device-proven** for x02 and probe, and blockers for [`x02-opus-partition-feasibility.md`](../../plans/x02-opus-partition-feasibility.md) promotion.
