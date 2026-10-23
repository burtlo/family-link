# Plan: Boot X02 on 2.125 MiB single-factory layout

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `research/exploration` |
| **Owners / areas**             | X02 firmware, BOX-3 flash recovery, partition migration evidence |
| **Status**                     | `done` (E1–E4, 2026-10-23) |
| **Targets**                    | One BOX-3 boot proof for **x02** and **x02-opus-probe** on tool-validated **2.125 MiB** single-factory geometry; safe restore to pre-experiment flash |
| **Last updated**               | 2026-10-22 |
| **Supersedes / superseded by** | Closes the **device boot** gap left by [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md) (Phases 1–5 host-only); does not supersede the open production layout choice |
| **As-built**                   | [`device-boot-2026/summary.md`](../evidence/x02-opus-partition/device-boot-2026/summary.md) |

## At a glance

Prove on **one explicitly identified BOX-3** that full v1 **X02** and the **X02 + Opus size probe** boot after flashing the complete bootloader, partition table, and application for the **single-factory 2.125 MiB** fixture (`factory` `0x220000` @ `0x10000`, `outbox` FAT @ `0x230000`). Host sizing and partition-parser validation are already done; this plan is **boot proof only**.

| Exercise | Outcome | Status |
|---|---|---|
| [E1 — Host build matrix](#e1--host-build-matrix) | Isolated sdkconfig + partition profile; `idf.py size` and partition fit vs `0x220000` for x02, probe, and dep control | `done` (2026-10-22 — [`build-sizes.txt`](../evidence/x02-opus-partition/device-boot-2026/build-sizes.txt)) |
| [E2 — Device boot (x02 only)](#e2--device-boot-x02-only) | Full flash of table + **x02** app; readback; boot markers; mandatory restore proof | `done` (2026-10-23) |
| [E3 — Device boot (x02-opus-probe)](#e3--device-boot-x02-opus-probe) | Same procedure for combined probe image on same geometry | `done` (2026-10-23) |
| [E4 — Smoke checklist (optional)](#e4--smoke-checklist-optional) | Short Wi-Fi / PIN / carousel / record-entry checks — no long capture | `done` (2026-10-23) |

**Stop rule:** If **E2** fails, do **not** proceed to **E3** until the failure is understood and E2 passes on a retry or the plan is revised.

---

## Background

The [`x02-opus-partition-feasibility`](x02-opus-partition-feasibility.md) spike (Phases 1–5, **2026-10-04**) measured clean-build application sizes under the legacy **`SINGLE_APP_LARGE`** table and modeled larger layouts. Preserved measurements:

| Build | App image bytes | Legacy 1.5 MiB slot (`1,536,000 B`) |
|---|---:|---|
| X02 baseline (`x02_product_shell`) | **1,484,272** | Fits (+51,728 B → **marginal** vs 262,144 B policy @ current slot) |
| X02 + Opus size probe (`x02-opus-probe`) | **1,665,808** | **Hard fail** (−129,808 B overflow) |
| Negative control (`x02-opus-dep`) | **1,484,272** | Matches baseline (dead-strip control) |

The corrected **single-factory** candidate enlarges the factory slot to **2,228,224 B** (**2.125 MiB**, `0x220000`) with on-chip **outbox** FAT at **`0x230000`**, size **`0xDD0000`**. The canonical CSV is [`partitions_single_factory.csv`](../evidence/x02-opus-partition/fixtures/partitions_single_factory.csv); an implementer copy may live at [`firmware/partitions/x02_single_factory_2125.csv`](../../firmware/partitions/x02_single_factory_2125.csv) (byte-identical to the fixture).

**Parser-valid ≠ boot proof.** ESP-IDF’s partition generator accepted the fixture in Phase 4; **H32** later booted a **small** island app on the **same geometry** ([`onchip-storage-qualification.md`](onchip-storage-qualification.md)), which shows the **table and offsets** can run on hardware but **does not** prove a **~1.48–1.67 MiB** X02-class image boots. Owners have flagged prior concern that **growing the application partition** can cause **silent boot failure** (invalid image bounds, second-stage loader rejection, or runtime faults) even when host tools report success.

**IRAM pressure:** measured X02-family builds reported **IRAM ~100%** (16,383 B used in Phase 1 table). A successful link and even a clean boot **do not** prove Opus runtime, WL/FAT mount, or long-message soak safety — only that the **chosen partition table + full app image** reach `app_main` and basic product markers.

**Related docs:** [`opus-demo.md`](opus-demo.md), [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md), [`phase4-corrections.md`](../evidence/x02-opus-partition/phase4-corrections.md), [`onchip-storage-qualification.md`](onchip-storage-qualification.md), [`long-message-experiments.md`](long-message-experiments.md).

## Product question

Can **full v1 X02** and the **X02 + Opus size probe** **boot** on the **tool-validated single-factory 2.125 MiB** partition table on **one BOX-3**, with a **verified 16 MiB backup** and **mandatory restore** to the pre-experiment flash image if anything goes wrong?

A **yes** for both images supports promoting that geometry for future long-message work. A **no** (boot loop, loader error, or unrecoverable brick without full restore) is a **hard blocker** for that layout regardless of modeled outbox capacity.

## Non-goals

- Fleet rollout or manufacturing partition migration
- Dual OTA layout proof (defer to a separate plan)
- Durable outbox fill, format/mount qualification, or H32-style I/O cadence
- Three-minute Opus codec soak or streaming playback proof
- v2 labs under `client/` or active product tree changes
- Changing `firmware/v1/` product behavior or exposing Opus in the X02 UI/record path
- Flipping global [`firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults) to the new table **before E2 passes** (isolated overlay only until then)

## Experiment boundaries

The experiment may add demo-only sdkconfig overlays, a named partition CSV under `firmware/partitions/`, host capture notes, and committed evidence. It must **not** change global production partition selection until E2 (and policy) say otherwise.

Use a **dedicated build directory** per demo (same pattern as H32: `SDKCONFIG` in the build tree, `SDKCONFIG_DEFAULTS` chain). Do not erase the entire 16 MiB flash unless the restore procedure and backup hashes are verified first. Do not commit full-flash backups, NVS blobs, Wi-Fi credentials, or household data.

---

## E1 — Host build matrix

**Goal.** Rebuild the three X02-related variants against the **2.125 MiB** table in an **isolated** configuration and confirm image size ≤ `0x220000` with documented margin vs policy.

**Deliverables**

- Demo-only sdkconfig fragment (suggested name: `firmware/sdkconfig.x02_2125.defaults`) that:
  - sets `CONFIG_PARTITION_TABLE_CUSTOM=y`
  - points `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` at `partitions/x02_single_factory_2125.csv` (or equivalent recorded path)
  - clears `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`
  - keeps `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- Clean builds (separate `-B` dirs) for:
  - **`x02`** → `x02_product_shell`
  - **`x02-opus-probe`** → `x02_opus_size_probe`
  - **`x02-opus-dep`** → `x02_opus_dep_only` (negative control; must still match baseline size)
- For each build: exact `.bin` byte count, `idf.py size`, `idf.py size-components` (or IDF-equivalent), decoded partition row for `factory`, and `python3 scripts/device.py device-partitions` **against the generated** `partition-table.bin` if helpful.
- Compare free bytes in the `0x220000` slot vs **policy margin** from [phase4-corrections.md](../evidence/x02-opus-partition/phase4-corrections.md): **`334,234 B`** = `max(262_144, ceil(0.15 × 2_228_224))`.
- Record git commit, ESP-IDF version, and sdkconfig hashes in `build-sizes.txt` under the evidence directory.

**Suggested commands** (adjust `SDKCONFIG_DEFAULTS` chain to match the recorded fragment name):

```bash
# Example pattern — use dedicated build dirs, not firmware/build reused from SINGLE_APP_LARGE
idf.py -D FAMILY_DEMO=x02_product_shell \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.v1;sdkconfig.x02_2125.defaults \
  -B build/x02_2125_product_shell -C firmware build
idf.py -B build/x02_2125_product_shell size

make build-firmware DEMO=x02-opus-probe   # only after flash.py/Makefile wired for 2125 overlay, or equivalent idf.py line
make build-firmware DEMO=x02-opus-dep
```

**Acceptance — PASS**

- All three variants **link** and produce `.bin` files.
- **x02** and **x02-opus-dep** sizes remain **1,484,272 B** (or document any intentional drift with commit provenance).
- **x02-opus-probe** ≤ **2,228,224 B** and recorded **free bytes** and **policy margin** are explicit.
- Generated partition table matches fixture offsets/sizes (especially `factory` @ `0x10000` / `0x220000`, `outbox` @ `0x230000` / `0xDD0000`).

**Acceptance — FAIL**

- Link failure, partition generator error, or image **>** `0x220000` for any variant intended to flash in E2/E3.

**Status:** `done` (2026-10-22)

---

## E2 — Device boot (x02 only)

**Goal.** On **one** named BOX-3, flash **bootloader + partition table + x02 application** for the 2.125 MiB layout, confirm boot, then **restore** the original 16 MiB image with readback proof.

**Prerequisites**

- E1 **PASS** for `x02`.
- Phase 1-style preservation from [`onchip-storage-qualification.md`](onchip-storage-qualification.md): full **16 MiB** flash backup and separate **NVS-region** backup in a **private** path; SHA-256 and byte counts recorded only in operator notes / `restore-proof.txt` (no blob in repo).
- Single unambiguous `/dev/cu.usbmodem*` (reject ambiguous multi-device).
- Restore command reviewed against backup size, chip flash size, and partition map **before** experimental flash.

**Procedure (implementer checklist)**

1. Inventory: `python3 scripts/device.py` storage / partition commands on the **backed-up** image or live device **before** change; record factory app descriptor at **`0x10000`**.
2. Flash **complete** images: **bootloader**, **partition-table.bin**, **application** at `0x10000` — **not** app-only after a partition table change ([warning](#buildflash-implementation-notes)).
3. Read back partition table @ `0x8000` and application header @ `0x10000`; hash compare to flashed artifacts.
4. Capture serial boot log (metadata only in repo): expect healthy second-stage load, **`Calling app_main`**, and product identity markers per [H38 boot validation pattern](h38-bounded-sd-filesystem.md) (project name, ESP-IDF version, factory segment, ELF SHA prefix — **no** Guru Meditation / watchdog loop).
5. Optional: `python3 scripts/device.py device-partitions` on live device to confirm `factory` / `outbox` rows.
6. **Mandatory restore:** write original full flash (or equivalent restore sequence validated in Phase 1), reboot, read back **full 16 MiB** or minimum partition+NVS+app regions; hashes must match pre-experiment backup. Record in `restore-proof.txt`.

**Acceptance — PASS**

- Device reaches stable boot with **x02** on new table; capture documents pass criteria above.
- Restore verified; post-restore partition map and app identity match pre-experiment backup.

**Acceptance — FAIL**

- Boot loop, loader panic, blank display with serial fault, or restore/hash mismatch.
- **Halt plan** — do not run E3.

**Status:** `done` (2026-10-23 — see `device-boot-2026/summary.md`)

---

## E3 — Device boot (x02-opus-probe)

**Goal.** Repeat E2 for **`x02-opus-probe`** on the **same** 2.125 MiB table (fresh backup epoch recommended).

**Procedure**

- Same preservation, combined flash, readback, boot capture, and **mandatory restore** as E2.
- Boot markers may reference the probe demo id / ELF identity; still require `Calling app_main` and absence of loader/runtime panic evidence.

**Acceptance — PASS**

- **x02-opus-probe** boots to the same class of healthy markers as E2.
- Restore proof recorded.

**Acceptance — FAIL**

- Any E2 failure class, or probe-specific crash before stable idle loop.

**Status:** `done` (2026-10-23 — see `device-boot-2026/summary.md`)

---

## E4 — Smoke checklist (optional)

**Goal.** If E2/E3 pass and owner authorizes a few minutes of interaction **before restore**, record shallow product health — **not** a stability or soak sign-off.

**Suggested checks** (stop on first hard failure; still restore afterward)

- Wi-Fi association or expected offline behavior (no credential logging)
- PIN entry reaches expected screen (no long-lived `checking...` without repaint)
- Carousel loads at least one card
- Record **entry** screen opens (do **not** require a full message capture or upload)

**Acceptance**

- Checklist recorded in `summary.md` as pass/fail per row; failures do not erase E2/E3 boot pass but must be noted for integration risk.

**Status:** `done` (2026-10-23) — see [`device-boot-2026/summary.md`](../evidence/x02-opus-partition/device-boot-2026/summary.md) § E4 checklist

---

## Build/flash implementation notes

**Partition fixture**

- Source of truth: [`docs/evidence/x02-opus-partition/fixtures/partitions_single_factory.csv`](../evidence/x02-opus-partition/fixtures/partitions_single_factory.csv)
- Suggested project copy: [`firmware/partitions/x02_single_factory_2125.csv`](../../firmware/partitions/x02_single_factory_2125.csv)
- Validate with the installed ESP-IDF partition tool; decode and compare to H32’s [`h32_onchip_storage.csv`](../../firmware/partitions/h32_onchip_storage.csv) (same geometry, different experiment).

**Per-demo sdkconfig**

- Follow [`firmware/sdkconfig.h32.defaults`](../../firmware/sdkconfig.h32.defaults): overlay only, semicolon-merged **after** `sdkconfig.defaults` (and `sdkconfig.defaults.v1` for x02 family per [`firmware/CMakeLists.txt`](../../firmware/CMakeLists.txt)).
- Suggested: `firmware/sdkconfig.x02_2125.defaults` with `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/x02_single_factory_2125.csv"`.
- **Do not** set `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` in the overlay.

**Flash entrypoints**

- Product: `make flash DEMO=x02` → [`scripts/flash.py`](../../scripts/flash.py) demo id **`x02`** → `x02_product_shell`
- Probe: **`x02-opus-probe`** → `x02_opus_size_probe`; control: **`x02-opus-dep`** → `x02_opus_dep_only`
- Implementers must ensure `flash.py` (or manual `esptool.py`) flashes **bootloader**, **partition table**, and **app** when the custom table is new to the chip.

**Critical warning — app-only flash after table change**

Flashing **only** the application binary while the chip still has **`SINGLE_APP_LARGE`** (or any mismatched table) leaves the second-stage loader and runtime with **inconsistent** partition bounds → **boot failure** or wrong segment mapping. After adopting `0x220000` factory size, always flash **partition table + app** (and bootloader if the IDF build requires it) as one coordinated step.

**Host tooling patterns**

- Partition decode / storage inventory: [`scripts/device.py`](../../scripts/device.py) (`device-partitions`, storage sections)
- H32 runbook reference: [`scripts/h32_storage_qual.py`](../../scripts/h32_storage_qual.py) (backup binding, readback, restore-while-held) — adapt for x02 images, not H32 sentinel logic
- On-chip qualification plan: [`onchip-storage-qualification.md`](onchip-storage-qualification.md) Phase 1 and Phase 3 boot language

---

## Evidence contract

Store committed, privacy-safe results under:

```text
docs/evidence/x02-opus-partition/device-boot-2026/
  summary.md              # PASS/FAIL per exercise, device id (sanitized), dates, commit
  build-sizes.txt         # E1 exact bytes, free space, policy margin
  boot-capture-metadata.json  # serial capture SHA, duration, pass/fail rubric — no secrets
  restore-proof.txt       # backup paths (private), hashes, readback verification outcome
```

Use [`device-boot/TEMPLATE.md`](../evidence/x02-opus-partition/device-boot/TEMPLATE.md) as the stub. Raw serial logs may live in private storage; link filename and hash only.

**Full-flash backup location policy:** keep 16 MiB and NVS backups **outside the repository** in a restricted local path; record **path, byte size, and SHA-256** in private operator notes and summarize verification outcome in `restore-proof.txt`. Never commit backup blobs.

---

## Acceptance / stop conditions (summary)

| Exercise | PASS | FAIL (halt) |
|---|---|---|
| E1 | All builds fit `0x220000`; table matches fixture | Overflow, wrong table, or missing dep control |
| E2 | x02 boots; restore verified | Boot failure or restore mismatch → **stop before E3** |
| E3 | Probe boots; restore verified | Probe boot or restore failure |
| E4 | Optional checklist recorded | N/A (informational) |

Overall plan **ships** when E2 and E3 **PASS** and restore proofs exist for each device epoch. E1 failure blocks all device work. E4 never overrides a failed restore requirement.

---

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| **Partial flash** (app only, old table) | Flash bootloader + partition + app together; document exact `esptool.py` offsets in evidence |
| **NVS survival / stale state** | Backup NVS separately; record whether NVS region unchanged; expect PIN/server pairing to survive if NVS offset unchanged — if boot fails after table change, restore full image before debugging app logic |
| **Outbox region touched at boot** | X02 product path may not mount outbox; still record whether any format/mount occurred (unexpected writes to `0x230000` region). H32 showed mount/format risk — **out of scope** to fill outbox, but **note** blank vs touched in `summary.md` |
| **IRAM / runtime vs boot-only** | Treat pass as **geometry + link + loader** proof only; IRAM saturation and Opus runtime remain tracked in feasibility / long-message roadmaps |
| **Brick without backup** | No experimental flash until 16 MiB backup hash verified; stop if port or flash size ambiguous |
| **Owner concern: partition grow → boot failure** | E2 is the explicit test; negative result blocks layout promotion regardless of modeled capacity |

---

## Links

- Opus island proof: [`opus-demo.md`](opus-demo.md)
- Long-message partition context: [`long-message-experiments.md`](long-message-experiments.md) Phase 8 / Phase 9
- Same geometry, small app: [`onchip-storage-qualification.md`](onchip-storage-qualification.md) (H32)
- Corrected margins and capacity: [`phase4-corrections.md`](../evidence/x02-opus-partition/phase4-corrections.md)
- Prior host-only spike: [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md)

## Evidence checklist

- [x] E1 isolated sdkconfig + partition CSV recorded (2026-10-22)
- [x] E1 build-sizes.txt with policy margin @ 2.125 MiB
- [x] E2 full flash + readback + boot capture + restore proof (2026-10-23)
- [x] E3 probe flash + boot capture + restore proof (2026-10-23)
- [x] Optional E4 smoke rows in summary.md (2026-10-23)
- [x] No secrets or full-flash blobs in repo (private epochs only)
