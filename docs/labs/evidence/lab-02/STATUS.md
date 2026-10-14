# LAB-02 evidence status

Last updated: 2026-10-11 (Phase A implementation landed; hardware evidence pending).

## Phase A closure checklist

| Item | Status | Notes |
|------|--------|-------|
| Firmware builds (recycle) | Done | `build/labs/lab-02-bounded-inbox` |
| Firmware builds (eager) | Done | `build/labs/lab-02-bounded-inbox-eager` |
| L1 host tests | Done | `make test` → `scripts/tests/test_lab02_host.py` |
| L2 auto-scroll (`CONFIG_LAB_AUTO_HARNESS`) | Ready | `make lab.lab02.scripted`; capture serial |
| L3 on BOX-3 (`S-fix-*`, `S-trav-20`, `S-rev`) | **Open** | Owner hardware run |
| `S-delay` (`LAB02_SLOW_BIND_MS`) | Ready | Kconfig inject; verify UI stays responsive |
| Eager vs recycle comparison | **Open** | Two flashes; link `comparison_run_id` in manifests |
| `REPORT-LAB-02-v1.md` | **Open** | After L3 runs |
| Phase B server | Ready (host tests) | `make lab.lab02.server` |
| Managed HTTP firmware (`page_provider_http`) | Ready (build) | `make lab.lab02.build KIT=lynn` |

## Capture workflow

From repo root (kit bound in deployment YAML, or pass `--port`):

```powershell
make lab.lab02 KIT=lynn
make lab.lab02 BACKEND=local VARIANT=eager KIT=lynn
make lab.lab02.capture KIT=lynn FLASH=0 SECONDS=60
```

Auto-scroll: `make lab.lab02.scripted KIT=lynn` (or menuconfig **Family Link device lab → Auto-scroll inbox after boot**).

Copy parsed metrics into `runs/<run-id>/` per [evidence README](../README.md) and [run manifest schema](../../schemas/run-manifest.v1.md).

## Reports

| Report | Status |
|--------|--------|
| `REPORT-LAB-02-v1.md` | Not published |
