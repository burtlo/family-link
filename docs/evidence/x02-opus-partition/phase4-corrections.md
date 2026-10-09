# Phase 4 corrections — partition model repair

**2026-10-04** · commit `9788f0130fdc9826503f48f39eba032db6be6593` · ESP-IDF **v5.4.2** · target **esp32s3** · flash **16 MiB** · partition table @ **0x8000**.

This record corrects **derived** Phase 4/5 partition geometry, capacity arithmetic, and headroom policy application. **Measured** Phase 1–3 build and linker evidence is unchanged.

## Preserved measurements (do not edit)

| Item | Value | Source |
|------|------:|--------|
| X02 baseline app | 1,484,272 B | [phase1-summary.md](phase1-summary.md) |
| h30 / h31 baselines | 888,864 / 1,519,440 B | phase1 |
| X02 + Opus probe | 1,665,808 B | [phase2-summary.md](phase2-summary.md) |
| Probe overflow vs 1.5 MiB slot | 129,808 B | phase2 |
| Opus growth vs X02 | +181,536 B | phase2 |
| Dep-only control | 1,484,272 B (strip) | phase2 map |
| Planned reserve range | 80–178 KiB | [phase3-planned-reserve.md](phase3-planned-reserve.md) |

## Correction ledger

| Document / section | Issue class | Action |
|--------------------|-------------|--------|
| [phase4-partition-strategies.md](phase4-partition-strategies.md) §1 tail | **Corrected arithmetic** | `0x1000000−0x187000` = **15,175,680 B** (was 15,171,584) |
| phase4 §2a raw outbox | **Corrected arithmetic** | `0xDD0000` = **14,483,456 B** (was 14,548,992) |
| phase4 §2b OTA layout | **Corrected ESP-IDF constraint** | Remove invalid app @ `0x12000` / `0x232000`; use [fixtures/partitions_dual_ota.csv](fixtures/partitions_dual_ota.csv) |
| phase4 usable capacity | **Planning model** | Use **0.92 × 0.92** on raw bytes (not `×0.85` misread) |
| phase4 message counts | **Corrected arithmetic** | (a) **~31.5**; (b) **~26.5** @ 389,120 B/msg |
| phase4 / feasibility headroom | **Corrected policy** | Margin for **2.125 MiB slot** = **334,234 B** = `max(256 KiB, ceil(15%×2,228,224))`, not frozen 262,144 B |
| phase4 product-ready row | **Corrected arithmetic** | High reserve: free **380,144 B**, only **45,910 B** beyond policy |
| feasibility Results / STORAGE tail | **Corrected arithmetic + open decision** | Align numbers; state tradeoff (≈**5** queued 3‑min messages vs OTA rollback) and leave layout open |
| [long-message-experiments.md](../../plans/long-message-experiments.md) Phase 1 | **Product decision** | Record both validated layouts; do not name a default before recovery/update requirements are decided |
| [HARDWARE.md](../../hardware/HARDWARE.md) / [MESSAGE-PROTOCOL.md](../../MESSAGE-PROTOCOL.md) | **Product decision** | Replace the inherited single-factory recommendation with both validated candidates and the open recovery/update decision |
| Phase 1–3 raw `*-size*.txt` | preserved | **No rewrite** |

## Reproducible calculation

```bash
python3 scripts/opus_partition_model.py | tee docs/evidence/x02-opus-partition/partition-model-output.txt
```

Canonical outputs (2026-10-04 run):

| Item | Exact result |
|------|-------------:|
| Current unpartitioned tail | **15,175,680 B** (`0xE79000`) |
| Single-app raw outbox | **14,483,456 B** (`0xDD0000`) |
| Modeled usable (a) | **12,258,797 B** |
| 3‑min messages (a) | **31.5** |
| Dual-OTA raw outbox | **12,189,696 B** (`0xBA0000`) |
| Modeled usable (b) | **10,317,358 B** |
| 3‑min messages (b) | **26.5** |
| On-chip delta (a−b) | **~5.0 messages** |
| Margin @ 0x220000 slot | **334,234 B** |
| High-reserve projected image | **1,848,080 B** (probe + 178 KiB) |
| Free after high reserve in slot | **380,144 B** |
| Beyond policy margin (high case) | **45,910 B** (~45.9 KiB) |

The capacity model floors fractional modeled bytes after applying both factors. This resolves the previous one-byte discrepancy: the exact dual-OTA modeled value is **10,317,358 B**.

Self-consistent minimum app slots under the slot-dependent policy:

| Reserve | Exact minimum | Binary MiB | Rounded up to ESP-IDF's 4 KiB app-size alignment |
|---------|--------------:|-----------:|-----------------------------------------:|
| Low (80 KiB) | **2,056,151 B** | **1.960898 MiB** | `0x1F6000` = **2,056,192 B** (1.960938 MiB) |
| Mid (125 KiB) | **2,110,363 B** | **2.012599 MiB** | `0x204000` = **2,113,536 B** (2.015625 MiB) |
| High (178 KiB) | **2,174,212 B** | **2.073490 MiB** | `0x213000` = **2,174,976 B** (2.074219 MiB) |

The installed ESP-IDF v5.4.2 generator requires 4 KiB app **size** alignment in this non secure boot model and 64 KiB app **offset** alignment. The candidate 2.125 MiB slots are 64 KiB multiples, so both sizes and consecutive OTA offsets validate without padding gaps.

**Headroom policy (per candidate slot):**

```text
required_margin(slot) = max(262_144, ceil(0.15 × slot))
product_ready when (slot − projected_image) >= required_margin(slot)
```

**2.125 MiB slot (`2,228,224 B`)** passes low / mid / high reserve cases; high reserve leaves limited growth beyond policy (**45,910 B**).

## Tool-validated layouts

| Layout | CSV fixture | ESP-IDF verify |
|--------|-------------|----------------|
| (a) Single factory + FAT outbox | [partitions_single_factory.csv](fixtures/partitions_single_factory.csv) | OK → [partition-tool-validation-single.csv](partition-tool-validation-single.csv) |
| (b) Dual OTA + FAT outbox | [partitions_dual_ota.csv](fixtures/partitions_dual_ota.csv) | OK → [partition-tool-validation-dual.csv](partition-tool-validation-dual.csv) |

**Alignment notes (IDF v5.4.2, non secure boot model):** app **sizes** use 4 KiB alignment; app **offsets** use 64 KiB (`0x10000`) alignment. Invalid prior OTA table placed `ota_0` at `0x12000`.

**OTA-only table:** No `factory` subtype; initial firmware must land in `ota_0` (or manufacturing flow documented). **Rollback** requires OTA stack + signing — **not implemented**. Parser acceptance ≠ device boot proof.

**Outbox subtype:** `data,fat` matches ESP-IDF FAT over wear-leveling mount APIs (planning assumption until mount/fill measured).

## Product layout decision (open)

Both tool-validated layouts remain candidates. The repository confirms that endpoints will be remotely deployed, but it does not establish whether operators can physically recover each endpoint by USB serial or whether remote firmware update and rollback are required.

- **(a) Single factory:** **~31.5** modeled three-minute messages; requires acceptance of physical USB-serial recovery and no rollback.
- **(b) Dual OTA:** **~26.5** modeled three-minute messages; requires future OTA signing, boot-slot policy, and operating procedures before remote update or rollback exists.

**(c) Removable-only** still does not fix app overflow; remains complement.

The product owner must decide the recovery and update requirements before selecting the production CSV. The **~5.0-message** modeled capacity difference does not decide that product requirement.

## Evidence levels

| Level | What |
|-------|------|
| **Measured** | Phase 1–3 `.bin` / linker / map |
| **Tool-validated** | CSV fixtures + `gen_esp32part.py` |
| **Modeled** | 0.92×0.92 usable bytes; 80–178 KiB planned code reserve |
| **Device-proven** | Boot, FAT mount, fill, reboot — **not done** |

## Implementation gates (unchanged)

1. Decide whether physical serial recovery is acceptable and whether remote update with rollback is required; then select production `partitions.csv` + `sdkconfig.defaults`
2. Clean build all demos against new geometry
3. One-device flash + boot (x02 behavior)
4. FAT/WL mount + **measured** usable capacity
5. Interrupted-write / reboot recovery
6. Migration + rollback runbook

## Artifact index

```text
docs/evidence/x02-opus-partition/phase4-corrections.md          (this file)
docs/evidence/x02-opus-partition/partition-model-output.txt
docs/evidence/x02-opus-partition/partition-tool-validation.txt
docs/evidence/x02-opus-partition/partition-tool-validation-*.csv
docs/evidence/x02-opus-partition/fixtures/partitions_*.csv
scripts/opus_partition_model.py
```

Updated narrative: [phase4-partition-strategies.md](phase4-partition-strategies.md) (body corrected; historical errors superseded by this ledger).
