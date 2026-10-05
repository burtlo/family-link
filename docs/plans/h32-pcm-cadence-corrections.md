# Plan: Correct H32 PCM cadence and repeated probe records

| Field | Value |
|---|---|
| Doc kind | Experiment correction and comparison plan |
| Status | Defined before implementation; hardware runs pending |
| Date | 2026-10-05 |
| Scope | Isolated H32 firmware, controller, evidence; existing qualification gates |
| Prerequisite | Verified original-image restore after the failed retry |
| Product partition choice | Open |

## Observed failure

The corrected mount retry, epoch `16a5354bbfd590933f18a093dd4f154b`, reached
PCM cadence and halted safely. The original raw capture has 117,699 bytes and
SHA-256 `6fd66476a548d03ff6f160cb98bf44fb2aa6e23f0566b2d1211ccf1dab30d2de`.
[Partial evidence](../evidence/onchip-storage-qualification/retry-20261005-failure/summary.json)
is retained independently of future runs.

Opus-sized cadence produced 90 chunks with zero missed deadlines. PCM produced
90 chunks at a configured 67,200 payload bytes/s with 79 missed deadlines.
PCM transaction median was 2,197,771 µs and maximum 3,157,734 µs; payload bytes
divided by accumulated transaction time yielded 61,012.294749 bytes/s. This
falls below the original more-than-64,000-byte/s durable PCM gate. Opus
transaction median was 919,462.5 µs and maximum 1,219,464 µs. These timings
include the workload's verification and manifest work.

The existing backlog counter divides each transaction's service time by the
two-second period. It does not measure accumulated schedule lateness. The
configured `seconds=180` field represents the recording payload duration;
record actual elapsed wall time independently in the next run.

A second independent acceptance failure exists: seven `REMOUNT` rows appear
for five cycles because later boots repeat cycle 5. The strict parser requires
exact identities. A faster PCM run would still fail acceptance until this
record contract is corrected.

The failed run did not reach interruption recovery or near-full admission.
Do not accept the backend, advance to durable outbox, or infer a safe reserve
from this partial run.

## Read-only diagnosis and hypothesis

In pinned ESP-IDF v5.4.2,
`components/fatfs/diskio/diskio_wl.c` calls `wl_erase_range` before `wl_write`
for block writes. `components/spi_flash/Kconfig` enables
`SPI_FLASH_YIELD_DURING_ERASE` by default, with
`SPI_FLASH_ERASE_YIELD_DURATION_MS=20` and `SPI_FLASH_ERASE_YIELD_TICKS=1`.
With the H32 100 Hz scheduler, one tick is 10 ms. Confirm these exact values
in the generated baseline SDK configuration and record them in each manifest;
defaults alone do not prove a build's effective settings.

Frequent erase yields plausibly account for part of the latency. This is a
hypothesis requiring a controlled comparison. FAT metadata, manifest updates,
read verification, erase characteristics, and scheduling also contribute.
Removing yields can improve throughput while increasing starvation and
watchdog risk. The next experiment must measure both outcomes.

The prerequisite restore after this failed retry completed on 2026-10-05.
The complete 16 MiB readback matched the original image; partition table,
pre-sentinel NVS, device fingerprint, and application descriptors were verified.
See [restore proof](../evidence/onchip-storage-qualification/restore-proof.txt).
Future profile runs still require independent per-epoch restore verification.

## Implementation before flashing

1. Preserve the failed epoch and hashes. Complete and record its original full
   image restore before preparing another run. Private backups, paths, device
   identities, and NVS contents stay outside committed evidence.
2. Add an H32-only build profile selector. Record effective yield enable,
   duration, tick count, tick rate, watchdog settings, filesystem options,
   source identity, and generated configuration hash in the build manifest and
   runtime records. Reject missing or inconsistent profiles. Keep product
   defaults and production firmware untouched.
3. Add separate cadence timings for open, write, flush/fsync, close, rename,
   read/hash verification, manifest transaction, and full transaction. Record
   per-chunk scheduled start, actual start, finish, cumulative lateness, missed
   periods, and final actual elapsed time using monotonic timestamps. Keep the
   existing fields readable with an explicit schema version or compatibility
   strategy; update the parser in the same change.
4. Add a lightweight independent scheduler heartbeat and measure its maximum
   service gap through each workload, along with watchdog events and stack/
   heap watermarks. Document task priority, core placement, requested interval,
   timer resolution, and measurement limits. A heartbeat only establishes
   scheduling for this storage island; product audio/network/LVGL/sketch
   scheduling requires later integration measurements.
5. Emit each initial probe `REMOUNT` cycle 1–5 exactly once. Later retained-probe
   validations must use a distinct record kind with phase identity, or an
   explicitly gated path. Preserve every probe checksum verification; suppress
   only the duplicate identity, not the underlying integrity check.
6. Add focused parser-shape verification: exactly five initial `REMOUNT` rows
   plus valid later probe-verification rows must pass the relevant structural
   gate; repeated initial cycle 5, missing cycles, wrong epoch, and incorrect
   ordering must fail. Keep the full qualification parser strict. Verify this
   before hardware execution.
7. Run independent source/config review and isolated build validation. Confirm
   slot headroom, manifest/source binding, full blank authority, sentinel
   isolation, epoch enforcement, format policy, durability operations, and all
   unchanged interruption/recovery checks before mutation.

The capture controller must recognize a complete `H32,FAIL` record immediately,
flush and retain its log/sidecar, and report the firmware stopping stage rather
than waiting for the byte-stall watchdog. A partial record must remain buffered
until its line is complete. Preserve the original firmware failure alongside
any later host error so the diagnostic cannot be replaced by a generic timeout.

## Controlled profile order

Use generated patterns and exactly the original workload: same payload sizes,
90 two-second chunks per stream, 67,200-byte/s PCM target, per-chunk durable
payload and manifest, hash verification, retained state, and phase order. Do
not reduce the rate, skip fsync, batch manifests differently, remove readback,
increase chunk duration, or weaken deadlines to obtain a pass.

1. **A — Baseline reference.** Retain the failed default-yield run as historical
   throughput evidence. If new instrumentation materially affects transaction
   timing, run an instrumented default-yield control with a fresh epoch to
   obtain a comparable reference. Label an expected cadence stop as a failed
   diagnostic run and restore fully afterward.
2. **B — No erase yield.** Build H32 with
   `CONFIG_SPI_FLASH_YIELD_DURING_ERASE=n`. Use a fresh bound epoch, fresh
   capture, complete backup/preflight/readback gates, and original runtime
   workload. Measure latency, actual sustained throughput, cumulative
   lateness, heartbeat gap, watchdog behavior, checksums, and memory. Any
   watchdog or safety failure stops the run. Do not disable watchdogs to
   rescue throughput.
3. **C — Bounded yield comparison.** After a successful B run, compare a fresh
   H32 profile enabling erase yield with
   `CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS=100` and
   `CONFIG_SPI_FLASH_ERASE_YIELD_TICKS=1`, preserving the same 100 Hz tick rate.
   This is an experiment setting, not a product selection. Execute the same
   workload and full qualification with another epoch and independently
   verified restore. Measure whether occasional yield improves scheduler
   service while retaining durable write margin.

Never reuse an erased-region proof, capture, or epoch across profile runs.
Complete restore and readback after every run, including failures, before
advancing. If B fails, preserve its exact stopping evidence, restore, and
analyze the dominant measured operation before defining another change.

## Acceptance and decisions

A candidate can qualify only by passing all original H32 and mount-correction
gates: full-region blank proof; sentinel and marker authority; exactly five
probe cycles; both 20-iteration I/O cases; 180-second Opus and PCM recording
workloads with no missed two-second deadlines or growing backlog; durable PCM
transaction throughput above 64,000 bytes/s; all 90 ordered interruption/
recovery pairs; near-full refusal, retained-file remount and reclamation;
parser acceptance; and full original-image restore/readback.

Record actual elapsed time and transaction throughput distinctly. Report
maximum heartbeat gap and compare profiles, without inventing a product
scheduler budget that the storage island cannot validate. A storage throughput
pass accompanied by heartbeat starvation is evidence for further architecture
work before production integration.

Publish a comparison table for each profile: exact effective settings, epoch,
build/config hashes, full raw-capture hash, actual wall time, per-operation
min/median/p95/max, durable transaction throughput, cumulative lateness,
missed deadlines, heartbeat maximum gap, watchdog events, memory, checksum/
recovery results, parser verdict, stopping phase, and restore verdict.
Label absent phases as not reached. Keep synthetic parser checks distinct from
hardware measurements and software restart distinct from power removal.

Choose a storage-island setting only after the comparisons establish write
margin and scheduler behavior. Production capture buffering, concurrent
microphone/network/sketch work, X02 integration, and actual power removal
remain subsequent experiments. Keep the on-chip backend contract neutral so
removable storage can be qualified separately.

## Completion and next dependency

Commit implementation, privacy-safe failure/success evidence, measured
comparison, independent verification, and per-epoch restore proof together.
Update the original qualification plan and evidence index truthfully. Advance
to the durable outbox experiment only after storage qualification passes and
its completed work is committed.
