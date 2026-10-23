# X02 2.125 MiB device boot — orchestration log

**Plan:** [`docs/plans/x02-opus-2125-device-boot.md`](../../../plans/x02-opus-2125-device-boot.md)  
**Orchestrator:** parent agent (2026-10-22)  
**Policy:** Up to **3 implementor attempts per approach**; then next approach. Findings committed under `device-boot-2026/`.

## Approaches

| ID | Description | Max attempts | Status |
|----|-------------|--------------|--------|
| A1 | E1 host build matrix (2125 sdkconfig overlay + flash.py wiring) | 3 | `done` |
| A2 | E2 device boot x02 (full table flash + serial + restore) | 3 | `done` |
| A3 | E3 device boot x02-opus-probe | 3 | `done` |
| A4 | E4 smoke with v1-server | 3 | `done` |

## Implementor runs

| Run | Approach | Attempt | Agent | Outcome | Summary |
|-----|----------|---------|-------|---------|---------|
| R1 | A1 | 1 | [E1 host build matrix A1](c04ad7c1-4cb4-4380-86b9-ce9de5badb1b) | **PASS** | x02 1,496,384 B (+397,606 vs policy); probe 1,677,568 B (+216,422); `build-sizes.txt` @ `e1676644` |
| R2 | A2 | 1 | [A2 E2 flash and boot](522c7506-779b-4953-abaf-edccf8bd42ee) | **PASS** | 2125 full flash + readback; boot past connecting @ `192.168.8.236`; restore verified (readback hash match); kit on pre-experiment image after closure |
| R3 | A3 | 1 | [A3 E3 opus-probe boot](da776178-376e-45be-b1a5-a21e892f9843) | **PASS** | Backup `x02-2125-e3-20261022`; probe flash/readback; ELF `529247194…`; restore `status=verified`; kit on pre-experiment image |
| R4 | A4 | 1 | [A4 E4 smoke checklist](868350e7-47d9-4a0c-a7a3-1f225ef1e5e2) | **PASS** | Backup `x02-2125-e4-20261023`; x02 @ 2125 + v1-server; `boot-capture-2125-smoke.txt` (all checklist rows); restore `status=verified` on attempt 2 (USB stall @ 100% write on attempt 1; serial port contention during overlap) |

### Orchestrator notes (2026-10-22)

- **Kit detected:** `/dev/cu.usbmodem101`
- **A1 blocker found:** `firmware/CMakeLists.txt` resets `SDKCONFIG_DEFAULTS`; passing only `-D SDKCONFIG_DEFAULTS=...` from flash.py was insufficient — fixed by appending `sdkconfig.x02_2125.defaults` when `FAMILY_PARTITION_PROFILE=x02_2125`.
- **Build hygiene:** Only one implementor may run `idf.py` / `make build-firmware` on `firmware/build/x02_2125_*` at a time (parallel builds corrupt trees).
- **Server:** `cd poc-v1 && make v1-server` → binds **`0.0.0.0:8080`** (all interfaces).
- **Client:** `DEMO_SERVER_HOST` **`192.168.8.236`**, port **8080** (`firmware/secrets.h` / `secrets.sunset.h`, 2026-10-22 laptop session).

## Server monitor

v1 product host for device smoke: from `poc-v1/`, `make v1-server` (0.0.0.0:8080). Implementors E2+ start in background and log PID; orchestrator records server health in `summary.md`.

## Notes

- Owner authorized poc-v1 changes for this exploration.
- Owner authorized **autonomous phase progression** (2026-10-23): continue E4+ without pause; up to 3 implementor attempts per approach, then next approach.
- No full-flash backup blobs in git.
