# Plan: Test H32 cadence write coalescing

| Field | Value |
|---|---|
| Doc kind | Experiment correction plan, defined before implementation |
| Status | Closed at read-only feasibility gate; no candidate build or flash |
| Date | 2026-10-05 |
| Scope | One isolated H32 cadence write change; unchanged qualification contract |
| Prerequisite | Independently verified full restore of the failed no-yield epoch |
| Product partition choice | Open: single-factory versus dual OTA |

## Evidence and hypothesis

The historical default-yield run missed 79 of 90 PCM deadlines, with a transaction median of 2.198 seconds. The fresh no-yield epoch `d5e92d2c813b3acd2125763829469869` missed 44 of 90 scheduled deadlines, including 33 service misses. Its transaction median was 1.895 seconds and p95 was 2.143 seconds. Write median/p95 were 1.086/1.417 seconds; manifest median was .379 seconds and flush median was .207 seconds. Derived transaction throughput was approximately 70.97 kB/s, yet the strict two-second cadence failed. Opus had zero misses. Heartbeat maximum gap was 57.684 ms and watchdog events were zero. Interruption recovery and near-full admission were not reached.

See [no-yield evidence](../evidence/onchip-storage-qualification/no-yield-20261005-failure/summary.md) and [previous correction plan](h32-pcm-cadence-corrections.md). The bounded-yield 100 ms profile was never run. The previous plan conditions that comparison on a successful no-yield run; this failure does not authorize advancing to it.

The current `write_pattern()` generates and submits at most 4,096 bytes per `fwrite`. Larger cadence-only submissions may reduce lower-level transaction overhead, but this is an unproven hypothesis. Increasing an application buffer may merely move time from write to flush, or leave physical erase behavior unchanged. This plan promises no pass.

## Read-only diagnosis before choosing a size

Inspect the locally installed, pinned ESP-IDF v5.4.2 and its actual H32 build configuration. Trace the effective path from newlib stdio buffering through VFS, FatFs `f_write`, disk writes, `diskio_wl`, and wear-leveling erase/write calls. Record source paths, version/commit, relevant configuration values, sector and allocation sizes, stdio buffering behavior, contiguous multi-sector handling, alignment constraints, and any splitting or maximum transfer limits. Distinguish facts established from source from hypotheses requiring hardware measurements.

Local read-only inspection found that pinned FatFs `f_write` clips direct `disk_write` requests at a cluster boundary (`if (csect + cc > fs->csize) cc = fs->csize - csect`, around line 4112). `diskio_wl.c` `ff_wl_write` erases then writes the requested sectors. H32 requests a 4,096-byte allocation unit, apparently one wear-leveling sector. Confirm the effective mounted cluster size and generated sector settings: a 16 KiB application submission may still become four 4 KiB erase/write operations. If this boundary prevents reducing physical operations and inspection identifies no other measurable coalescing benefit, do not flash this candidate. Select a separately defined bounded hypothesis or attached-storage qualification. Changing allocation unit or filesystem geometry is a different experiment and is outside this one-variable plan.

Determine whether 4 KiB application submissions produce avoidable separate lower-level operations and whether larger submissions can reach the driver intact. Do not assume a larger `fwrite` produces a larger physical write. Choose exactly one bounded candidate size after this diagnosis; 16 KiB is a candidate, not a predetermined setting. If source inspection provides no defensible coalescing mechanism, stop before implementation and recommend attached-storage qualification instead.

**Gate result (2026-10-05): no go for the 16 KiB candidate.** The pinned
source and generated H32 configuration establish one 4 KiB sector per FAT
cluster. FatFs clips direct data writes at that cluster boundary, so a 16 KiB
submission still reaches wear leveling as four 4 KiB data requests. An
independent source review confirmed the inference and its limits. See the
[read-only preflight evidence](../evidence/onchip-storage-qualification/coalescing-preflight.md).
No source change, new build, or hardware run was made for this candidate.
Larger calls could save unmeasured stdio/VFS overhead; the proposal had no
defensible mechanism to reduce the dominant physical operations. Changing
cluster geometry would be a separate experiment. Proceed to an attached
storage qualification plan, preserving H32 as unqualified.

## Controlled implementation boundary

Keep the isolated single-factory H32 fixture, no-yield profile, filesystem settings, task placement/priorities, watchdogs, and workload unchanged. Product firmware, production defaults, X02, and server behavior remain outside scope. Record effective no-yield settings and generated configuration/source hashes.

Introduce one cadence-specific pattern writer using an explicit bounded buffer allocated before the timed cadence transactions. Generate the identical deterministic bytes with identical offsets and seeds. Keep buffer filling inside the write timing, matching the baseline's inclusion of pattern generation. Record allocation size, capability/location, allocation failure handling, available heap before/after allocation, minimum runtime heap/PSRAM, and stack watermark. Establish the required remaining heap budget from the existing H32 tasks and allocations before selecting the size; fail safely if allocation or the budget cannot be met. Release the buffer after the cadence phase. Avoid adding a large stack buffer.

Change only cadence payload submission granularity. Retain the existing writer and exact buffered-byte injection boundaries for probe, I/O, interruption, and floor paths. Preserve payload and manifest flush/fsync, close, rename, complete readback/SHA-256 verification, durable manifest replacement, retained files, and phase ordering. Do not batch manifests across chunks, remove checks, pre-generate an entire recording, increase chunk duration, lower the target rate, or relax deadlines.

## Device and run safety sequence

1. Preserve both failed epochs and their independent capture hashes. The committed no-yield [restore proof](../evidence/onchip-storage-qualification/no-yield-20261005-failure/restore-proof.txt) establishes the original full 16 MiB readback, partition table, pre-sentinel NVS, device identity, and application descriptors. Recheck the preserved backup and connected device before another mutation.
2. Apply all original preservation/preflight requirements: exactly identified target and port, detected 16 MiB flash, verified private full-image and NVS backups, reviewed restore offsets/command, and source/build/partition binding. Keep private paths, identities, backups, credentials, and NVS contents outside committed evidence.
3. Use a separate H32 build directory and fresh bound epoch, capture, and metadata. Independently establish full outbox-region blank authority before first formatting. Preserve sentinel/progress namespace isolation, marker authority, no-auto-format recovery, and all mount-retry requirements. Never reuse another epoch's blank proof or erased-region authorization.
4. Execute the original phase order with the candidate. Stop on safety, watchdog, identity, integrity, authority, or cadence failure; retain the exact complete terminal failure record and buffered partial-line handling.
5. Restore the complete original image after success or failure. Verify full 16 MiB readback and the original partition/NVS/application/device checks independently for this epoch. Do not start another profile or backend run until restoration is verified.

## Measurement and evidence contract

Preserve schema-2 cadence fields and strict epoch/profile identity. Record scheduled start, actual start, finish, start/finish lateness, missed periods, cumulative schedule behavior, service misses, scheduled deadline misses, backlog, actual elapsed wall time, and accumulated transaction time. Keep operation timings for open, pattern generation/write, flush/fsync, close, rename, full read/hash verification, manifest, and complete transaction. Report min/median/p95/max and write-plus-flush totals so deferred buffered work cannot masquerade as a gain.

Keep the independent heartbeat with its documented interval, timer resolution, priority, core placement, sample count, maximum gap, and measurement limits. Report watchdog events, memory watermarks, source/config/build hashes, exact buffer settings, payload and aggregate checksums, raw capture byte count/SHA-256, parser verdict, stopping phase, and restoration verdict. Heartbeat measurements establish storage-island behavior only; they do not establish product audio/network/UI concurrency budgets.

Use the existing strict qualification parser. Preserve exactly five initial `REMOUNT` identities and distinct later retained-probe validation records. Reject duplicate or missing identities, wrong epoch/profile, invalid ordering, missing records, checksum failures, weakened cadence, and incomplete campaigns. Any new buffer metadata must have a versioned, explicit parser contract and source/build binding. Candidate metadata must not turn a partial run into full acceptance. Label unavailable phases as not reached and synthetic parser validation separately from hardware evidence.

## Run, decision, and full acceptance

Run exactly 90 two-second Opus chunks at the original 2,000 B/s and 90 two-second PCM chunks at 67,200 B/s, with the original generated bytes, transaction durability, retained-state workload, and phase order. The existing no-yield run is the comparison reference. If instrumentation changes materially affect timing, obtain an independently restored fresh-epoch instrumented control before interpreting the comparison.

The cadence hypothesis succeeds only if the candidate has zero scheduled and service deadline misses, no growing backlog, intact checksums, and durable PCM transaction throughput above 64,000 B/s. Evaluate the full transaction distribution and write-plus-flush cost; a faster write field alone is insufficient. Any watchdog or safety failure is a stop. Report heartbeat changes without inventing an unmeasured product scheduler limit.

If both cadence streams pass, continue the same candidate through the unchanged full interruption and floor campaign. If either fails, stop, preserve its exact evidence, and restore; do not proceed to fault/floor or claim a qualified backend.

Full acceptance still requires all original H32 and mount-correction gates: full-region blank proof; sentinel and marker authority; exactly five initial probe cycles; both 20-iteration I/O cases; both 180-second recording workloads with zero missed two-second deadlines and no growing backlog; durable PCM throughput above 64,000 B/s; all 90 ordered interruption/recovery pairs; near-full refusal; retained-file remount/integrity and reclamation; measured safe free-space floor; strict complete parser acceptance; and independently verified full original-image restore/readback. Software restart evidence remains distinct from actual power removal.

## Alternative and dependency order

If this bounded candidate fails, prefer planning a separate attached-storage qualification over repeated unbounded tuning. First identify the user's actual BOX-3-accessible accessory and medium: a host-attached disk is not a BOX-3 backend. Dock USB storage requires MSC support and occupies the camera port; SENSOR microSD requires SDMMC support and replaces the dock. Neither nominal speed nor capacity proves durable latency.

Start the alternate backend with isolated mount, write/flush/fsync, checksum readback, remount, and the identical cadence diagnostic. Full qualification must retain equivalent integrity, interruption, measured free-space admission, and restoration requirements, and add removal/unavailable-media behavior. Keep that experiment separate from queue implementation.

H32 remains failed until the entire campaign passes and completed implementation/evidence are committed in a later authorized execution. The [on-chip qualification](onchip-storage-qualification.md) remains the prerequisite for the current [durable outbox plan](durable-outbox-demo.md); neither partial cadence results nor a removable-media demonstration advances it. Substituting an attached first backend requires an explicit dependency/plan decision before outbox implementation. The backend interface should remain neutral.

The single-factory partition is only the isolated fixture. Storage measurements do not decide the production choice between single-factory physical recovery and dual-OTA update/rollback. Production integration, real microphone/concurrent workloads, X02, and actual switched-power failure experiments remain subsequent work.
