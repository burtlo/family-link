# Plan: Correct X02 partition analysis

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `research/exploration` |
| **Owners / areas**             | X02 firmware, ESP-IDF partitioning, durable outbox, technical documentation |
| **Status**                     | `shipped` |
| **Targets**                    | Corrected X02 + Opus partition evidence and implementation handoff; no product firmware change |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Corrects the modeled results in [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md) and [`phase4-partition-strategies.md`](../evidence/x02-opus-partition/phase4-partition-strategies.md) |
| **As-built**                   | **2026-10-04** · [`phase4-corrections.md`](../evidence/x02-opus-partition/phase4-corrections.md), [`scripts/opus_partition_model.py`](../../scripts/opus_partition_model.py) |

## At a glance

Repair the arithmetic, headroom calculation, and invalid dual-OTA geometry in the completed X02 partition study without changing its valid binary-size evidence. Leave future agents with mechanically checked layouts, one consistent capacity model, and an explicit open product decision between the two valid layouts.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Establish the correction ledger](#phase-1--establish-the-correction-ledger) | Valid measurements are separated from statements that require correction | `done` |
| [Phase 2 — Recompute capacity and headroom](#phase-2--recompute-capacity-and-headroom) | Every byte count and policy result follows one reproducible calculation | `done` |
| [Phase 3 — Validate candidate partition layouts](#phase-3--validate-candidate-partition-layouts) | Single-app and dual-OTA layouts pass ESP-IDF partition validation | `done` |
| [Phase 4 — Revisit the product recommendation](#phase-4--revisit-the-product-recommendation) | The recovery and queue-capacity tradeoff is explicit and evidence based | `done` |
| [Phase 5 — Repair the evidence and handoff](#phase-5--repair-the-evidence-and-handoff) | All affected documents agree and distinguish modeled from device-proven behavior | `done` |

---

## Background

The completed experiment produced strong evidence for the application-size question. The full X02 shell with live Opus encode and decode references is **1,665,808 bytes**, which exceeds the current **1,536,000-byte** factory partition by **129,808 bytes**. The dependency-only control matches the X02 baseline, and the linker map attributes almost all growth to Opus. Preserve those results.

The subsequent partition model contains three kinds of errors: incorrect hexadecimal-to-decimal conversions, use of a fixed 256 KiB margin after the stated 15% margin became larger, and OTA application offsets that violate ESP-IDF alignment requirements. The final recommendation also needs to state whether gaining roughly five queued three-minute messages is worth giving up remote update rollback.

This correction is analysis work. It does not authorize a custom production partition table, a change to `sdkconfig.defaults`, device erasure, flashing, an OTA implementation, an outbox implementation, or changes to X02 behavior.

**Related docs:** [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md), [`phase4-partition-strategies.md`](../evidence/x02-opus-partition/phase4-partition-strategies.md), [`phase3-planned-reserve.md`](../evidence/x02-opus-partition/phase3-planned-reserve.md), [`long-message-experiments.md`](long-message-experiments.md), [`STORAGE.md`](../hardware/STORAGE.md), [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md).

## Known corrections to reproduce

These are review findings, not values to copy without recomputation. The executing agent must derive them from the hexadecimal boundaries and record the command or script used.

| Item | Correct calculation | Expected result |
|---|---:|---:|
| Current unpartitioned tail | `0x1000000 - 0x187000` | `0xE79000` = **15,175,680 B** |
| Single-app raw outbox | `0x1000000 - 0x230000` | `0xDD0000` = **14,483,456 B** |
| Corrected dual-OTA raw outbox, if apps end at `0x460000` | `0x1000000 - 0x460000` | `0xBA0000` = **12,189,696 B** |
| Usable single-app outbox under the existing model | `floor(14,483,456 × 0.92 × 0.92)` | **12,258,797 B** |
| Single-app three-minute capacity | usable bytes ÷ `389,120` | approximately **31.5 messages** |
| Usable corrected OTA outbox under the existing model | `floor(12,189,696 × 0.92 × 0.92)` | **10,317,358 B** |
| Corrected OTA three-minute capacity | usable bytes ÷ `389,120` | approximately **26.5 messages** |
| Margin for a `0x220000` app slot | `max(262,144, ceil(0.15 × 2,228,224))` | **334,234 B** |
| High-reserve projected image | `1,665,808 + 178 KiB` | **1,848,080 B** |
| Free space after high reserve | `2,228,224 - 1,848,080` | **380,144 B** |
| Space beyond the applicable policy margin | `380,144 - 334,234` | **45,910 B** |

The existing `0.92 × 0.92` filesystem model is an estimate. Keep it visibly labeled as a planning model until an actual FAT and wear-leveling volume is formatted and measured.

---

## Phase 1 — Establish the correction ledger

**Goal.** Preserve valid experimental evidence while identifying every derived statement that must change.

**Deliverables**

- Create `docs/evidence/x02-opus-partition/phase4-corrections.md` as the dated correction record.
- Record the source commit, current worktree state, ESP-IDF version, target, flash size, and partition-table offset used for validation.
- Inventory every affected statement in:
  - `docs/evidence/x02-opus-partition/phase4-partition-strategies.md`
  - `docs/plans/x02-opus-partition-feasibility.md`
  - `docs/plans/long-message-experiments.md`
  - Any architecture or storage document that repeats the chosen layout or capacity
- Classify each item as one of:
  - preserved measurement,
  - corrected arithmetic,
  - corrected ESP-IDF constraint,
  - planning assumption,
  - unverified runtime or device behavior,
  - product decision requiring an explicit rationale.
- Preserve Phase 1 through Phase 3 raw reports and the measured binary/linker values. Do not regenerate or rewrite them merely to make the correction record look uniform.

**Acceptance**

- The ledger names every document and line or section that needs an update.
- The X02 baseline, Opus probe, negative control, overflow, and growth-attribution measurements remain unchanged unless new reproducible build evidence disproves them.
- Derived estimates are not presented as measured facts.

**Status:** `done`

---

## Phase 2 — Recompute capacity and headroom

**Goal.** Replace hand-calculated values with one exact and reproducible model.

**Deliverables**

- Add a small calculation transcript or script output to the correction evidence. It must accept hexadecimal partition boundaries and emit exact bytes, binary MiB, usable bytes, and message counts.
- Recompute the current tail, both candidate outbox sizes, and every dependent capacity figure.
- Use one terminology convention:
  - **raw partition bytes** for flash geometry,
  - **modeled usable bytes** after filesystem and safety deductions,
  - **measured usable bytes** only after a real mount/fill experiment.
- Apply the declared margin policy to the candidate partition itself:

  ```text
  required_margin(slot) = max(256 KiB, ceil(15% × slot))
  product_ready when slot - projected_image >= required_margin(slot)
  ```

- Calculate low, midpoint, and high planned-feature reserves separately. Do not describe a range as a midpoint result.
- Solve the self-consistent minimum slot size for the 15% branch using `slot >= projected_image / 0.85`, then round up according to the verified ESP-IDF size-alignment rule.
- Report both:
  - free bytes remaining in the 2.125 MiB slot, and
  - free bytes beyond the applicable policy margin.
- State that the high-reserve case leaves about 45.9 KiB beyond policy, so the selected slot passes but has limited unallocated growth beyond the high estimate.

**Acceptance**

- Re-running the recorded calculation reproduces every number in the updated comparison tables.
- Hexadecimal boundaries, decimal bytes, and MiB values agree.
- The headroom policy is evaluated against each candidate slot rather than frozen at the current partition's 256 KiB threshold.
- The corrected conclusion still states whether 2.125 MiB passes low, midpoint, and high reserve cases.

**Status:** `done`

---

## Phase 3 — Validate candidate partition layouts

**Goal.** Ensure every modeled table is syntactically and geometrically valid for the repository's ESP32-S3 and ESP-IDF version.

**Deliverables**

- Confirm the applicable ESP-IDF rules from the version used by the experiment:
  - partition offsets use 4 KiB flash-sector alignment,
  - application offsets use 64 KiB (`0x10000`) alignment,
  - application sizes satisfy the configured secure-boot and sector-alignment requirements,
  - partitions do not overlap and remain inside 16 MiB flash.
- Express each candidate as a temporary CSV or evidence fixture and run ESP-IDF's partition generator/parser against it. Save the command and decoded output in `phase4-corrections.md`.
- Validate the single-factory candidate:
  - `nvs` at `0x9000`, size `0x6000`
  - `phy_init` at `0xF000`, size `0x1000`
  - `factory` at `0x10000`, size `0x220000`
  - `outbox` at `0x230000`, size `0xDD0000`
- Replace the invalid OTA candidate whose apps began at `0x12000` and `0x232000`. Validate a corrected candidate, expected to resemble:
  - existing `nvs` and `phy_init` unchanged,
  - `otadata` at `0x10000`, size `0x2000`,
  - intentional alignment gap through `0x20000`,
  - `ota_0` at `0x20000`, size `0x220000`,
  - `ota_1` at `0x240000`, size `0x220000`,
  - `outbox` at `0x460000`, size `0xBA0000`.
- Verify the intended boot behavior of an OTA-only table with no factory application. If the bootloader/configuration requires a factory slot or a different `otadata` arrangement, revise the candidate rather than assuming the geometry is sufficient.
- Confirm that `data,fat` is the correct subtype for the planned ESP-IDF FAT-over-wear-leveling mount API.
- Keep validated candidate CSVs as evidence or temporary fixtures. Do not select them in `sdkconfig.defaults` during this correction task.

**Acceptance**

- ESP-IDF tooling accepts each candidate table and prints the expected boundaries.
- Every application offset is a multiple of `0x10000`.
- The final partition ends exactly at or before `0x1000000` with no overlap.
- The corrected comparison uses parser output rather than only spreadsheet arithmetic.
- No device is erased or flashed.

**Status:** `done`

---

## Phase 4 — Revisit the product recommendation

**Goal.** Choose or defer the single-app versus dual-OTA direction using the corrected costs and the product's recovery needs.

**Deliverables**

- Rebuild the comparison from validated geometry and corrected capacity:
  - one factory app plus larger on-chip outbox,
  - two OTA apps plus smaller on-chip outbox,
  - removable storage as a complement rather than the application-size solution.
- Quantify the corrected difference between the two on-chip choices in raw bytes, modeled usable bytes, and three-minute messages.
- Evaluate these product concerns explicitly:
  - whether deployed endpoints can be physically recovered by serial flashing,
  - whether remote firmware updates are expected,
  - whether rollback after a failed update is required,
  - whether approximately five additional queued three-minute messages materially improve the product,
  - whether removable storage can provide overflow without becoming required for safe recording.
- Record one of two outcomes:
  1. select a default with a dated rationale and named recovery assumptions, or
  2. leave the product choice open while retaining both validated layouts.
- If single-factory remains the recommendation, state that it accepts serial recovery and lacks rollback. If dual OTA becomes the recommendation, state the corrected queue capacity and the future OTA/signing work it introduces.

**Acceptance**

- The recommendation does not claim that larger queue capacity alone makes single-factory preferable.
- Recovery and update assumptions are visible next to the capacity comparison.
- The result does not call OTA implemented merely because flash geometry exists.
- Any unresolved product choice is recorded as unresolved rather than silently inherited by downstream plans.

**Result.** The repository confirms remote deployment but does not say whether deployed endpoints can be physically recovered by USB serial or whether remote firmware update and rollback are required. Both layouts remain valid inputs, and the production choice remains open until those product requirements are decided. The additional ~5.0 modeled queued messages in the single-factory layout do not resolve that choice.

**Status:** `done`

---

## Phase 5 — Repair the evidence and handoff

**Goal.** Make the corrected analysis safe for future agents to use as the partition implementation input.

**Deliverables**

- Update `phase4-partition-strategies.md` with corrected geometry, arithmetic, capacity, headroom, and recommendation status.
- Update the Results / decision section of `x02-opus-partition-feasibility.md` so it points to the correction evidence and uses the same values.
- Update `long-message-experiments.md` and any repeated architecture/storage summaries so they no longer treat the original single-app recommendation as unqualified.
- Add a short correction note that preserves historical provenance: identify which Phase 1–3 results remain valid and which Phase 4/5 derived claims were corrected.
- Distinguish these evidence levels consistently:
  - **measured:** build and linker results,
  - **tool-validated:** candidate partition CSV geometry,
  - **modeled:** filesystem usable capacity and future code reserve,
  - **device-proven:** reserved for a later boot/mount/fill/reboot experiment.
- Mark the original research plan `shipped` only if its decision section clearly states that the geometry is tool-validated, not device-proven, and the production-layout choice remains open.
- Provide the future implementation agent with exact remaining gates:
  - project-owned production CSV selection,
  - clean build against the selected layout,
  - one-device flash and boot,
  - FAT/WL mount and measured capacity,
  - interrupted-write and reboot recovery,
  - migration and rollback procedure.

**Acceptance**

- Searching the documentation finds no remaining use of the invalid `0x12000` or `0x232000` app offsets as valid candidates.
- Repeated raw-capacity, usable-capacity, and headroom numbers agree across all affected documents.
- The evidence index includes `phase4-corrections.md` and the validated partition-tool output.
- Future agents can tell exactly what has been measured, modeled, tool-validated, and left for hardware proof.
- Product firmware, `sdkconfig.defaults`, and deployed devices remain unchanged.

**Status:** `done`

---

## Risks and review notes

- The planned-feature reserve remains an estimate until the durable outbox, streaming player, protocol compatibility, and sketch timeline are linked into X02.
- Static DRAM and IRAM reports from the link-only probe do not measure codec stacks, heap allocations, playback buffers, or concurrent runtime pressure.
- The existing **16,383 / 16,384 byte IRAM** result is shared by all measured variants. Keep it as a separate runtime/build constraint and do not attribute it to Opus.
- Filesystem overhead depends on the actual FAT and wear-leveling configuration. The 8% plus 8% deductions are capacity-planning assumptions.
- A parser-valid partition table is still not proof that the device boots, mounts storage, preserves NVS, or survives interrupted writes.

## References

- Original plan: [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md)
- Phase 2 measurement: [`phase2-summary.md`](../evidence/x02-opus-partition/phase2-summary.md)
- Phase 3 reserve: [`phase3-planned-reserve.md`](../evidence/x02-opus-partition/phase3-planned-reserve.md)
- Phase 4 model to correct: [`phase4-partition-strategies.md`](../evidence/x02-opus-partition/phase4-partition-strategies.md)
- Ordered experiments: [`long-message-experiments.md`](long-message-experiments.md)
- ESP-IDF partition rules: <https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-guides/partition-tables.html>
- ESP-IDF FAT and wear leveling: <https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/storage/fatfs.html>
