# Plan: Measure X02 Opus partition fit

| Field                          | Value |
|--------------------------------|-------|
| **Doc kind**                   | `research/exploration` |
| **Owners / areas**             | X02 firmware, build system, flash partitioning, device storage |
| **Status**                     | `shipped` (research result; Phase 4 geometry **corrected** 2026-10-04 — see [phase4-corrections.md](../evidence/x02-opus-partition/phase4-corrections.md)) |
| **Targets**                    | Link-only X02 + Opus size probe; one-device boot proof if a partition change is selected |
| **Last updated**               | 2026-10-04 |
| **Supersedes / superseded by** | Feeds Phase 8 of [`long-message-experiments.md`](long-message-experiments.md) |
| **As-built**                   | **2026-10-04** · commit `9788f0130fdc9826503f48f39eba032db6be6593` · evidence [`docs/evidence/x02-opus-partition/`](../evidence/x02-opus-partition/) |

## At a glance

Determine whether full X02 plus the proven Opus encoder/decoder fits the current application partition with enough room for durable outbox, streaming playback, and sketch integration. Produce exact binary, component, partition, and remaining-flash measurements before changing X02 behavior.

| Phase | Outcome | Status |
|---|---|---|
| [Phase 1 — Capture reproducible baselines](#phase-1--capture-reproducible-baselines) | Current X02, h30, and h31 sizes and partition bytes are recorded from clean builds | `done` |
| [Phase 2 — Link the combined size probe](#phase-2--link-the-combined-size-probe) | A non-product build contains full X02 and live Opus encode/decode symbols | `done` |
| [Phase 3 — Attribute growth and reserve](#phase-3--attribute-growth-and-reserve) | Flash growth, component costs, and planned-feature reserve are quantified | `done` |
| [Phase 4 — Compare partition strategies](#phase-4--compare-partition-strategies) | Current, single-app, OTA, and outbox capacity tradeoffs are explicit | `done` |
| [Phase 5 — Record the integration decision](#phase-5--record-the-integration-decision) | Future agents know the approved partition direction and remaining risks | `done` |

---

## Background

The completed h30/h31 proof found Opus viable and measured roughly 180 KiB of codec-related application growth. h31 also reported little application-partition headroom. X02 already links the complete product UI, Wi-Fi, HTTP/WebSocket, audio, and sketch code. The repository currently selects ESP-IDF's `SINGLE_APP_LARGE` partition table through `firmware/sdkconfig.defaults`; no project-owned partition CSV defines how future durable local storage and application growth share the BOX-3's 16 MiB flash.

Adding `opus` to `PRIV_REQUIRES` is not a valid combined-size measurement by itself. The linker may discard the codec library when no reachable X02 code references its symbols. The experiment therefore needs a dedicated build variant that links the complete X02 sources and makes real references to both Opus encode and decode paths without exposing Opus behavior in the product UI.

This plan does not authorize Opus behavior in X02, a production partition migration, erasing attached storage, or flashing every device.

**Related docs:** [`opus-demo.md`](opus-demo.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md), [`STORAGE.md`](../STORAGE.md), [`durable-outbox-demo.md`](durable-outbox-demo.md).

## Questions this experiment must answer

1. What is the exact current application partition size?
2. What are the exact clean-build image sizes for X02, h30, and h31 under the same toolchain and configuration?
3. What is the image size of full X02 when both Opus encoder and decoder are retained by the linker?
4. Which components and object files account for the increase?
5. How much margin remains after reserving expected long-message integration growth?
6. Can a larger application partition coexist with a useful durable outbox on 16 MiB flash?
7. Does the product require one factory application slot or future dual-slot OTA, and what outbox capacity does each choice leave?
8. Does changing the partition layout require destructive migration or reformatting on devices with existing local data?

## Measurement record

Every result must include:

- Git commit plus a concise list of relevant uncommitted changes
- ESP-IDF version, target, compiler version, and BOX-3 flash size
- `sdkconfig` hash and selected partition-table filename
- Exact partition offsets and sizes decoded from the generated partition table
- Exact `.bin` byte count, not only rounded console output
- ESP-IDF size summary, component report, file/object report, DRAM, IRAM, flash text, and flash rodata
- Linker map path and build log path
- Whether device identity patching changes the `.who.bin` size
- Build variant and compile definitions used

Use one small results table as the canonical comparison:

| Build | App bytes | App partition bytes | Free bytes | Free % | Flash text | Flash rodata | DRAM | IRAM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| X02 baseline | 1,484,272 | 1,536,000 | 51,728 | 3.4% | 1,101,922 | 253,684 | 232,431 | 16,383 |
| h30 Opus local | 888,864 | 1,536,000 | 647,136 | 42.1% | 662,922 | 133,376 | 147,475 | 16,383 |
| h31 Opus chunks | 1,519,440 | 1,536,000 | 16,560 | 1.1% | 1,180,374 | 210,428 | 196,799 | 16,383 |
| X02 + Opus size probe | 1,665,808 | 1,536,000 | −129,808 | −8.5% | 1,260,786 | 276,356 | 232,431 | 16,383 |

Negative control **x02-opus-dep** (Opus dependency without live references) matches X02 baseline at **1,484,272** bytes — see [phase2-summary.md](../evidence/x02-opus-partition/phase2-summary.md). Full provenance: [phase1-summary.md](../evidence/x02-opus-partition/phase1-summary.md), [toolchain.txt](../evidence/x02-opus-partition/toolchain.txt).

## Phase 1 — Capture reproducible baselines

**Goal.** Replace stale or differently configured size notes with comparable clean-build evidence.

**Deliverables**

- Preserve the current dirty worktree; do not reset or discard another agent's Opus work.
- Record toolchain and configuration identities before building.
- Clean and rebuild `x02`, `h30`, and `h31` using the same ESP-IDF installation and `sdkconfig` inputs.
- Record exact application binary sizes and generated partition boundaries.
- Save `idf.py size`, `idf.py size-components`, and `idf.py size-files` output for each build, or the equivalent commands supported by the installed ESP-IDF release.
- Record current baseline build behavior for LAN and any production-shaped TLS configuration that materially changes linked code. Do not compare binaries built with different feature flags without labeling them.
- Note discrepancies between fresh output and the figures currently recorded in [`opus-demo.md`](opus-demo.md); retain both with provenance rather than silently replacing history.

**Suggested commands**

```bash
make build-firmware DEMO=x02
make build-firmware DEMO=h30
make build-firmware DEMO=h31
```

Run size-report commands against each demo's own build directory. Do not reuse a map file from an earlier timestamp as fresh evidence.

**Acceptance**

- All three builds succeed from a reproducible configuration.
- Exact app partition bytes and image bytes are recorded.
- Size reports and maps are retained at documented paths.
- Any current image already violating the selected partition is treated as a blocker rather than rounded away.

**Status:** `done`

## Phase 2 — Link the combined size probe

**Goal.** Measure full X02 and live Opus code in one binary without implementing product behavior.

**Deliverables**

- A dedicated build-only variant, separate from `x02_product_shell`, that links every current X02 source.
- The variant adds the proven Opus component and `fl_opus.c` wrapper.
- A probe translation unit references initialization, one encode call, one decode call, profile selection, and teardown so link-time garbage collection retains the required paths.
- Probe code is unreachable from normal X02 user interaction and does not alter the product build.
- The variant uses the same Wi-Fi, TLS, LVGL, assets, and build flags as the baseline X02 comparison.
- A map-file check naming the retained Opus encoder, decoder, wrapper, and relevant component archive symbols.
- A negative-control build that adds the dependency without live references, demonstrating whether dead stripping would have hidden part of the cost.

**Acceptance**

- The combined probe links successfully or fails with an exact partition-overflow report.
- Map evidence proves both encoder and decoder paths are present.
- The binary remains a measurement artifact; X02 product behavior and source routing are unchanged.
- The result table contains exact combined size and current-partition margin.

**Status:** `done`

## Phase 3 — Attribute growth and reserve

**Goal.** Distinguish Opus cost from build noise and decide how much product-growth margin is needed.

**Deliverables**

- Component and object-file delta from X02 baseline to X02 + Opus probe.
- Separate accounting for the Opus library, wrapper, new task code, Ogg/page parsing intended for the device, seek-index client, checksums, and any accidentally duplicated helpers.
- Identification of large optional tables, diagnostics, or demo-only dependencies that should not enter X02.
- Estimated flash reserve for work that is planned but absent from the size probe:
  - Durable outbox and selected filesystem driver
  - Chunk upload/recovery state machine
  - Bounded Ogg streaming and index retrieval
  - FLSK2 timeline
  - Administrator-independent protocol compatibility code
  - Logging and recovery diagnostics
- Explicit separation of flash size from runtime RAM, PSRAM, and task-stack risk. The three-minute soak supplies runtime evidence; a successful link does not prove runtime safety.

**Headroom policy for the experiment**

- **Hard failure:** image exceeds the application partition.
- **Marginal:** image fits but leaves less than 256 KiB or less than 15% of the partition, whichever reserve is larger.
- **Candidate fit:** image leaves at least that reserve before planned missing features are counted.
- **Product-ready fit:** image leaves the reserve after the documented estimate for all missing long-message components.

The decision record may choose a stricter policy. It must not silently choose a weaker one.

**Acceptance**

- At least 90% of material image growth is attributable to named components or object groups.
- The result states whether the current partition is hard-fail, marginal, candidate-fit, or product-ready-fit.
- Planned but unlinked work has an explicit reserve instead of being described as “small.”

**Status:** `done`

## Phase 4 — Compare partition strategies

**Goal.** Find a flash layout that supports the application and preserves useful durable outbox capacity.

**Deliverables**

- Decode and document the current `SINGLE_APP_LARGE` layout.
- Model at least these project-owned partition strategies against the BOX-3's 16 MiB flash:
  1. One larger factory application plus NVS and a wear-levelled outbox filesystem.
  2. Two OTA application slots plus NVS, OTA metadata, and a smaller outbox filesystem.
  3. Current application layout with removable storage as the only durable outbox.
- For each strategy, record application bytes, required alignment/overhead, usable outbox bytes, approximate number of three-minute 16 kbps Opus messages, OTA capability, recovery implications, and migration cost.
- Include a reserve for filesystem metadata and a free-space safety floor; do not present the raw partition size as fully usable message capacity.
- Record whether the currently attached storage reduces the need for a large on-chip outbox without making the product dependent on a removable accessory.
- Document that changing a partition table can invalidate or erase existing on-chip filesystem data and requires a migration/backup procedure.

**Comparison table**

| Strategy | App slots | App bytes/slot | Usable outbox estimate | 3-min Opus messages | OTA | Migration risk |
|---|---:|---:|---:|---:|---|---|
| Current `SINGLE_APP_LARGE` | 1 | 1,536,000 | **0** (tail unpartitioned) | **0** on-chip | No | **Low** (baseline) |
| Larger factory + on-chip FAT | 1 | **2,228,224** (2.125 MiB) | **12,258,797 B modeled** | **~31.5** | No | **High** |
| Dual OTA + on-chip FAT | 2 | **2,228,224** each | **10,317,358 B modeled** | **~26.5** | Geometry only | **High** |
| Current app + removable outbox | 1 | 1,536,000 | **External** (not on-chip) | **Many** (media-limited) | No | **Low–med** |

Decoded layouts, usable-outbox formula, and migration notes: [phase4-partition-strategies.md](../evidence/x02-opus-partition/phase4-partition-strategies.md). **Corrections:** [phase4-corrections.md](../evidence/x02-opus-partition/phase4-corrections.md).

**Acceptance**

- At least one layout provides product-ready application headroom and a useful outbox.
- If no on-chip layout does both, the decision explicitly chooses removable storage or reduces requirements.
- OTA capability is an explicit product tradeoff, not an accidental consequence of copied ESP-IDF defaults.
- No partition CSV is adopted merely because the 16 MiB module has unused space; offsets, alignment, boot compatibility, and data migration are accounted for.

**Status:** `done`

## Phase 5 — Record the integration decision

**Goal.** Leave a reviewable conclusion that later agents can implement without repeating the spike.

**Deliverables**

- Dated result section or feature record with all raw reports linked.
- Decision among:
  - Keep current partition unchanged
  - Adopt a named project-owned single-app layout
  - Adopt a named project-owned OTA layout
  - Depend on removable storage for the outbox
  - Reduce/trim linked features and repeat the probe
- Exact remaining application headroom and planned-feature reserve.
- Exact outbox capacity and storage assumption.
- Required changes to `sdkconfig.defaults`, partition CSV, build variant, flashing process, and recovery instructions.
- Identification of probe-only files to retain as a reproducible size target or remove after results are captured.
- Updates to [`HARDWARE.md`](../HARDWARE.md), [`STORAGE.md`](../STORAGE.md), [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md), and [`long-message-experiments.md`](long-message-experiments.md) when evidence changes their estimates or status.

**Optional one-device boot proof**

If the decision selects a new partition table, validate it on one explicitly identified development device before broader use:

- Save any needed local storage first.
- Flash the complete partition table and application using the normal supported tool.
- Confirm boot, Wi-Fi, PIN, carousel, record entry, and reported flash/partition sizes.
- Confirm the outbox partition mounts only if that filesystem is part of this spike; otherwise leave storage behavior to the durable-outbox experiment.
- Record whether restoring the prior partition layout requires full erase/reflash.

**Acceptance**

- One partition strategy is recommended with quantified reasons, or the spike reports a precise blocker and next measurement.
- Future agents can reproduce the combined build and size reports.
- Product X02 remains behaviorally unchanged by this experiment.
- A partition migration is not rolled out beyond the one-device proof without separate authorization.

**Status:** `done`

## Results / decision (2026-10-04, corrected)

**Provenance:** Phase 1–3 **measured** sizes unchanged. Phase 4/5 **modeled** capacity, OTA geometry, and headroom policy corrected per [phase4-corrections.md](../evidence/x02-opus-partition/phase4-corrections.md). Layout CSVs are **tool-validated**, not **device-proven**.

### Decision status

**The production layout remains open.** Both corrected candidates use **2.125 MiB** application slots and have tool-validated geometry:

- **(a) Single factory + FAT:** **~31.5** modeled three-minute messages; requires explicit acceptance of physical USB-serial recovery and no rollback.
- **(b) Dual OTA + FAT:** **~26.5** modeled three-minute messages; requires OTA signing, boot-slot policy, and operating procedures before remote update and rollback exist.

The repository establishes remote deployment, but it does not establish whether each endpoint can be physically recovered or whether remote firmware update and rollback are required. The **~5.0-message** modeled difference does not resolve that product choice. This spike does **not** select either layout in `sdkconfig` or a production partition CSV, and neither candidate is device-proven.

**(c) Removable-only** does not fix the application partition blocker; use as complement after on-chip outbox exists.

### Hard fail on current partition (X02 + Opus probe)

| Metric | Value |
|--------|------:|
| Factory app partition (`SINGLE_APP_LARGE`) | 1,536,000 B |
| X02 baseline app image | 1,484,272 B (+51,728 free → **marginal** vs **262,144 B** margin @ current slot) |
| X02 + Opus probe app image | 1,665,808 B (**−129,808** B vs partition → **hard failure**) |
| Probe growth vs X02 | **+181,536 B** (~177 KiB); ~99% attributable to Opus lib + wrapper + probe TU |
| Planned long-message flash (not in probe) | **80–178 KiB** (evaluate low / mid / high separately) |
| Policy margin @ **2.125 MiB** slot | **334,234 B** (`max(256 KiB, ceil(15%×2,228,224))`) |

On a **2.125 MiB** app slot with **125 KiB** reserve: projected **1,793,808 B**, **434,416 B** free, **100,182 B** beyond policy — **product-ready (modeled)**. **High (178 KiB)** reserve: **380,144 B** free, only **45,910 B** beyond policy. Exact low/mid/high self-consistent minima are **2,056,151 / 2,110,363 / 2,174,212 B**; rounded up to ESP-IDF's 4 KiB app-size alignment they are **2,056,192 / 2,113,536 / 2,174,976 B**. App offsets separately require 64 KiB alignment. Modeled on-chip outbox **(a):** **12,258,797 B** usable → **~31.5** three-minute Opus messages @ **389,120 B** planning size.

### Probe artifacts to retain

Keep as reproducible size-regression targets (not product firmware):

| Flash id | Role |
|----------|------|
| `x02-opus-probe` | Full X02 + forced Opus encode/decode link — canonical combined size |
| `x02-opus-dep` | Negative control (dependency only; proves dead-strip would hide cost) |

Sources: `firmware/demos/x02_opus_size_probe.c`, `firmware/demos/x02_opus_dep_only.c`, `firmware/common/fl_opus_size_probe.{c,h}`; flash map in `scripts/flash.py`.

### Required future changes (implementation — not done here)

1. **Resolve and record the product choice** — decide whether physical serial recovery is acceptable and whether remote update with rollback is required. Then promote either [partitions_single_factory.csv](../evidence/x02-opus-partition/fixtures/partitions_single_factory.csv) or [partitions_dual_ota.csv](../evidence/x02-opus-partition/fixtures/partitions_dual_ota.csv) into a project-owned production table; keep **nvs** / **phy_init** offsets unless the migration runbook says otherwise.
2. **`firmware/sdkconfig.defaults`** — switch from `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` to custom partition table filename; rebuild all demos against new geometry.
3. **Flash / manufacturing** — document full reflash when table changes; first outbox mount may format empty FAT; NVS may survive if chip is not erased and NVS region unchanged.
4. **Recovery / migration runbook** — backup server-side inbox; treat first custom-table flash on a dev device as wiping experimental on-chip data; document revert to `SINGLE_APP_LARGE`.
5. **Long-message integration** — still requires durable-outbox, streaming, and protocol work from [`long-message-experiments.md`](long-message-experiments.md); partition change alone does not add Opus product behavior to **x02**.

### Remaining risks (unchanged by this spike)

- IRAM **~100%** on measured builds; runtime RAM, codec soak, and WL/FAT mount proof are **out of scope** here.
- **No** fleet repartition or one-device boot proof was performed in Phases 1–5.

### Evidence index

All raw reports: [`docs/evidence/x02-opus-partition/`](../evidence/x02-opus-partition/) — `phase1-summary.md` through `phase4-partition-strategies.md`, **`phase4-corrections.md`**, `toolchain.txt`, `partition-tables.txt`, `*-size*.txt`, `fixtures/partitions_*.csv`.

## Non-goals

- Implementing Opus recording or playback in X02
- Implementing the durable outbox
- Selecting the final attached-storage backend
- Running the three-minute codec soak
- Changing the server protocol
- Shipping OTA updates
- Erasing or repartitioning every device

## Evidence checklist

- [x] Toolchain and `sdkconfig` identity
- [x] Decoded current partition table
- [x] Fresh X02, h30, and h31 exact sizes
- [x] Combined X02 + Opus probe size
- [x] Encoder and decoder retention proven in the map
- [x] Component and object-file deltas
- [x] Planned-feature reserve
- [x] Partition/outbox/OTA comparison
- [x] Migration and erase implications
- [x] Dated open-decision record and implementation handoff

## References

- Current build selection: [`firmware/main/CMakeLists.txt`](../../firmware/main/CMakeLists.txt)
- Current partition defaults: [`firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults)
- X02 sources: [`firmware/v1/`](../../firmware/v1)
- Opus wrapper: [`firmware/common/fl_opus.c`](../../firmware/common/fl_opus.c)
- Opus result: [`opus-demo.md`](opus-demo.md)
- Device storage constraints: [`STORAGE.md`](../STORAGE.md)
