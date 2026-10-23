# X02 2.125 MiB device boot — summary

| Exercise | Result | Date | Notes |
|----------|--------|------|-------|
| E1 host build matrix | **PASS** | 2026-10-22 | See [`build-sizes.txt`](build-sizes.txt) |
| E2 device boot (x02) | **PASS** | 2026-10-23 | 2125 full flash + readback; boot rubric after server IP fix; mandatory restore verified — [`restore-proof.txt`](restore-proof.txt) |
| E3 device boot (x02-opus-probe) | **PASS** | 2026-10-23 | Probe **1,677,568 B** @ 2125; readback OK; boot rubric + restore verified — [`restore-proof.txt`](restore-proof.txt) |
| E4 smoke | **PASS** | 2026-10-23 | 2125 **x02** + v1-server @ `192.168.8.236`; checklist below; restore verified — [`restore-proof.txt`](restore-proof.txt) |

## Server / secrets (2026-10-23)

- v1 product host on owner laptop: `http://192.168.8.236:8080` (`make v1-server` → `0.0.0.0:8080`).
- `firmware/secrets.sunset.h` / `secrets.h`: `DEMO_SERVER_HOST` **192.168.8.236**, port **8080**.
- Reflash command used: `make flash DEMO=x02 PARTITION_PROFILE=x02_2125 PROFILE=sunset WHO=mazi PORT=/dev/cu.usbmodem101` (WHO stamps `change-me-mazi` for v1 hangout auth).

## E2 boot highlights

- Factory partition **0x220000** on device; post-flash readback matched built `partition-table.bin` and stamped app (see [`boot-capture-metadata.json`](boot-capture-metadata.json)).
- **Pre-fix:** wrong host **192.168.8.143** + placeholder Wi-Fi → hangout HTTP failures and Guru loop.
- **Post-fix (2125 image):** Wi-Fi OK; `http://192.168.8.236:8080`; hangout roster loaded (private log `boot-capture-after-ip-fix.txt`).
- **Restore:** Full 16 MiB write + readback hash match to pre-experiment backup; post-restore serial shows legacy **factory 0x300000** table, `Calling app_main`, Wi-Fi connected, no panic in capture — display recovery from aborted partial restore.

## E3 boot highlights

- Backup epoch: `~/family-link-private/x02-2125-e3-20261022` (same pre-experiment full-flash hash as E2).
- Flash: `make flash DEMO=x02-opus-probe PARTITION_PROFILE=x02_2125 PROFILE=sunset WHO=mazi PORT=/dev/cu.usbmodem101`.
- Readback: `partition-table.bin` (first **3072 B** of 4 KiB @ `0x8000`) + full stamped `family_link_demo.who.bin` (**1,677,568 B**) match build tree.
- Boot (private `boot-capture-2125-probe-full.txt`, 70 s): `Calling app_main`, ELF `529247194…` (probe image), `x02 v1 shell` @ `192.168.8.236:8080`, Wi-Fi **192.168.8.106**; no Guru / boot loop. Hangout HTTP **401** (auth) — product idle loop, display active.
- **Restore:** `h32_storage_qual.py restore` → JSON `status=verified`; post-restore `lab09_session_isolation` @ factory **0x300000** (45 s capture).

## E4 smoke checklist (2125 + full x02, `PROFILE=sunset` / `WHO=mazi`)

| Check | Result | Evidence (private serial `boot-capture-2125-smoke.txt`) |
|-------|--------|--------------------------------------------------------|
| Wi-Fi association | **pass** | `wifi:connected`, STA `192.168.8.106` |
| PIN → expected screen | **pass** | `v1_auth: paint pin` → carousel (no stuck `checking...`) |
| Carousel ≥1 card | **pass** | `paint carousel mode=inbox items=1`; playback fetch `status=200` |
| Record entry opens | **pass** | `paint pick` → `record start` / `paint record` (no full capture required) |

Backup epoch: `~/family-link-private/x02-2125-e4-20261023`. ELF prefix **8952dc90a…** (x02 @ 2125, matches E2 post-fix image).

## Owner session (2026-10-23 evening)

After E1–E4, owner exercised **2125 + x02** (record/send) and **2125 + x02-opus-probe** (live hangout). Captured learnings: [`owner-session-2026-10-23.md`](owner-session-2026-10-23.md) — includes v1 upload timing, `V1_PRODUCT_DEMOS` / token fix for probe, and **what Opus-on-probe does *not* mean** (same UI, WAV send).

## Overall

**Plan complete (E1–E4).** **2.125 MiB single-factory layout is device-proven** for full **x02** (boot + shallow product smoke) and **x02-opus-probe** (same product shell, Opus linked in image) on **0x220000** geometry. Owner **confirmed working** probe session after auth fix. Production `sdkconfig` promotion remains an owner decision; **Opus on the wire** remains follow-on work.
