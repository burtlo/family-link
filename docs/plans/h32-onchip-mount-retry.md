# Plan: Retry H32 after the first-mount authority failure

| Field | Value |
|---|---|
| **Doc kind** | `experiment correction / retry plan` |
| **Status** | `ready for implementation` |
| **Scope** | H32 controller, storage-only firmware, private operator artifacts, and H32 evidence |
| **Observed failure** | 2026-10-04 experiment probe stopped at the first outbox mount |
| **Production partition choice** | Open; the single-factory table remains an experiment fixture |

## Decision

Retry H32 only after making a verified all-`0xff` outbox authoritative for a new
experiment run. A stale experiment `run_id` in NVS must never prevent safe
initialization of a host-erased outbox. It must also never authorize formatting
an outbox that contains any data.

Use a host-generated run epoch, bind it into both build metadata and firmware,
and copy it into the on-filesystem run marker after formatting. Keep the
preservation sentinel and experiment progress in separate NVS namespaces. The
sentinel proves device preservation; it is not a filesystem generation marker.

## What the failed probe established

The privacy-safe log at `logs/h32-experiment-probe.txt` shows:

- The experiment reused the preservation sentinel rather than creating it.
- The ESP32-S3 reported 16 MiB flash and the running table matched the H32
  `factory` and `outbox` offsets and sizes.
- Firmware sampled the first 4 KiB of `outbox` as erased.
- NVS still contained a nonzero experiment `run_id`, so firmware classified the
  boot as a continuation and set `format_if_mount_failed=no`.
- FAT mount failed with result 13 and firmware halted with
  `stage=mount,reason=unsafe_or_io`.

The halt was the correct fail-safe response for the existing policy. The policy
was wrong because it treated NVS progress as the authority for whether the
separately erased outbox was new. The host erased `0x230000..0xffffff` while
preserving NVS at `0x9000`; those two stores therefore described different run
generations.

The 4 KiB sample is insufficient evidence that the complete 14,483,456-byte
partition is blank. The retry must not infer blank state from a sample, a mount
error, or an old NVS value.

## State and authority contract

### Three independent identities

1. **Preservation identity** lives in an NVS namespace dedicated to the H32
   sentinel. Sentinel-only firmware may create it. Experiment firmware may only
   read it and must halt if it is absent or invalid.
2. **Requested run identity** is a host-generated 128-bit epoch. The controller
   generates it once per retry, records it in private operator metadata, passes
   it to both H32 builds as an immutable build input, and records the exact value
   in each build manifest and capture sidecar.
3. **Active filesystem identity** is a versioned, checksummed run marker stored
   atomically in `outbox`. It contains the requested epoch, layout identifier,
   marker schema, and filesystem configuration.

Experiment progress belongs in a separate `h32_run` NVS namespace. Every key in
that namespace is scoped by the requested epoch. Do not store progress keys in
the sentinel namespace.

### Authoritative blank rule

The outbox is a new run only when all of the following are true:

- the host has erased exactly `OUT_OFF=0x230000`, `OUT_SIZE=0xDD0000`;
- a host readback of that entire region contains only `0xff`;
- byte count is exactly 14,483,456;
- the controller records the SHA-256 of the complete readback and compares it
  with the locally computed SHA-256 for 14,483,456 bytes of `0xff`;
- before mounting, firmware scans the complete runtime-discovered `outbox`
  partition and finds every byte equal to `0xff`;
- runtime partition offset and size equal the manifest and experiment fixture.

When the complete firmware scan passes, blank flash outranks stale `h32_run`
state. Firmware may format only `outbox`, erase/reset only the `h32_run`
namespace, create the requested-epoch marker atomically, and begin at phase
zero. It must leave the sentinel namespace unchanged.

When any byte is non-`0xff`, firmware must not format. It must mount without
automatic formatting and then require a valid marker whose epoch equals the
requested epoch. A successful mount with a missing, corrupt, or mismatched
marker is a fail-safe halt. A failed mount of a nonblank partition is also a
halt that preserves the partition for inspection.

NVS state alone never authorizes format. A mount error alone never authorizes
format. A partial erased-region sample never authorizes format.

## Implementation plan

### 1. Preserve the failed attempt

- Hash and retain the failed probe log and its metadata sidecar.
- Record it as an unsuccessful, nonqualifying attempt. Do not merge its records
  with the retry.
- Record the old experiment build hashes, source hash, requested epoch if one
  existed, and the exact mount failure.
- Keep private flash/NVS backups outside the repository. Revalidate their byte
  counts and hashes before another mutation.

### 2. Correct controller run setup

- Add a `prepare-run` operation that creates one cryptographically random
  128-bit epoch and writes it atomically to private operator metadata.
- Make `build`, `experiment-flash`, `capture`, and `parse` require that epoch.
  Reject missing, regenerated, or mismatched epochs.
- Include the epoch in source/build identity. A build for one epoch cannot be
  used for another.
- Preserve separate sentinel and experiment build directories. Sentinel mode
  must remain demonstrably unable to access `outbox`.
- After `erase_region`, read back the complete outbox to a private temporary
  file or verify it in bounded streaming blocks. Check exact byte count, every
  byte, and the whole-region erased SHA-256. Delete the temporary readback only
  after recording its hash and result in private metadata.
- Perform full-region blank verification before writing or starting the
  experiment application. A failed or interrupted verification stops the run.
- Bind capture metadata to the epoch, device fingerprint, original backup,
  post-sentinel NVS hash, partition-table hash, application hash, and complete
  erased-region proof.

### 3. Correct firmware initialization

- Compile the requested 128-bit epoch into experiment firmware and emit it in a
  structured `H32,RUN_REQUEST` record before storage initialization.
- Locate `outbox` at runtime and scan its full discovered length in bounded
  buffers. Emit byte count, elapsed time, all-erased result, and SHA-256.
- If fully erased, format only `outbox`, measure format and first mount
  separately, clear/reset only `h32_run`, create the marker by
  write/flush/close/rename, remount, and verify the marker before phase zero.
- If nonblank, mount with `format_if_mount_failed=false`, validate the marker
  checksum/schema/layout/epoch, and resume only when NVS progress carries the
  same epoch. Halt on every mismatch.
- Emit one `H32,RUN` record containing `epoch`, `event=new|resume`,
  `blank_authority=full_scan|marker`, `nvs_action=reset|preserve`, and status.
- Never call the unmount API after a failed mount unless a valid mount handle
  was actually registered. This avoids the misleading secondary error seen in
  the failed probe.
- Ensure every later proof record carries the same epoch. Store progress only
  after the preceding artifact has been recovered and validated.

### 4. Isolate the rerun

- Use a fresh private run directory, raw log filename, metadata sidecar, and
  evidence staging directory. Do not append to the failed capture.
- Require an empty evidence staging directory before parsing.
- Reject duplicate `(record kind, phase, test, iteration/cycle)` identities
  unless the schema explicitly allows reboot status records.
- The parser must accept evidence only from one epoch and one set of build
  hashes. It must reject any line lacking the epoch after run initialization.
- Publish evidence atomically only after the complete parser acceptance suite
  passes.

### 5. Run the corrected qualification

Execute the original H32 phase order after the gates below pass: five probe
reboot/remount cycles, full I/O matrix, Opus cadence, PCM cadence, interruption
matrix, near-full admission/reboot/reclaim, evidence aggregation, then restore.
The retry does not weaken any acceptance criterion in
`onchip-storage-qualification.md`.

Actual power removal remains unproven unless a named controllable power relay is
introduced. Software restart evidence must continue to say `esp_restart` and
must not be described as power-loss evidence.

### Interruption matrix clarification (2026-10-05)

Run ten cycles at each of nine boundaries, for 90 ordered fault/recovery pairs:
buffered 128 bytes, buffered half payload, buffered 65,535 bytes, after fsync
before close, after close before rename, after rename before metadata, after
metadata before delete, between payload and manifest deletion, and after both
deletions. The intermediate deletion boundary closes the original plan's
requirement to reset during delete. Recovery must validate all retained earlier
commits and exact manifest contents, complete cleanup successfully, and only
then advance the progress cursor. The parser must enforce the exact pair set
and order, including every reboot's epoch, marker, flash size, and partition
bounds. Missing or duplicate pairs cannot qualify the run.

## Exact preflight gates

The controller must stop before experiment mutation unless every gate passes:

1. Exactly one serial target is selected and its fingerprint matches the
   original private backup.
2. Chip is ESP32-S3, detected flash is 16 MiB, and the full original backup,
   partition backup, and original NVS backup pass size/hash/readback checks.
3. Sentinel log proves `mode=sentinel_only`, `storage_access=no`, and a halt;
   post-sentinel NVS readback matches its recorded private hash.
4. Sentinel and experiment builds are in distinct directories and encode the
   same requested epoch while differing in mode.
5. Both manifests match current source inputs, ESP-IDF version, fixture hash,
   image hashes, and device bounds. Application size retains the plan's 15%
   slot headroom.
6. Generated and decoded table has exactly `nvs`, `phy_init`, `factory`, and
   `outbox` at the planned offsets and sizes. The production partition choice
   remains explicitly undecided.
7. The erase command is exactly bounded to `0x230000 + 0xDD0000`; no whole-chip
   or NVS erase is present.
8. Complete post-erase outbox readback is exactly 14,483,456 bytes, all `0xff`,
   and matches the expected erased SHA-256.
9. NVS readback immediately after experiment image/table flashing still equals
   the post-sentinel NVS hash. No experiment boot has happened before this
   comparison.
10. Capture sidecar binds the device, epoch, builds, backups, erased-region
    proof, port, and reset mechanisms.

## Exact runtime acceptance gates

The retry is accepted only when all are true:

1. `SENTINEL` says experiment mode, existing valid sentinel, and `created=no`.
2. Runtime flash size and exact partition bounds match the manifest.
3. `ERASE_SCAN` covers 14,483,456 bytes, reports all erased, and matches the
   host's expected erased hash.
4. Exactly one new-run initialization occurs. It reports the requested epoch,
   full-scan authority, `h32_run` reset, outbox-only format, and successful
   marker verification after remount.
5. No subsequent boot formats. Every resume validates matching marker and NVS
   epoch before reading progress.
6. Five distinct reboot/remount cycles preserve the committed probe and its
   checksum.
7. All original Phase 4 I/O, cadence, checksum, latency, throughput, memory,
   backlog, and post-remount gates pass with the same epoch.
8. Every required interruption cycle recovers before its cursor advances;
   previously committed files remain valid; no partial file is accepted.
9. Near-full admission, retained-file validation after reboot, safe-floor hold,
   and reclamation all pass with the same epoch.
10. There are no `FAIL`, panic-loop, unexpected format, epoch mismatch, parser
    duplicate, missing record, or capture watchdog events.
11. Raw log SHA-256 and capture metadata are recorded before derived evidence is
    generated; the parser independently enforces all record counts and values.
12. Full original-flash restore is followed by a complete readback whose hash,
    partition table, NVS bytes, and application descriptors match the original
    backup. Restore proof is retained privately and a sanitized result is
    committed.

Failure of any runtime gate leaves the run `unproven`. Preserve the raw log and
on-flash state long enough to diagnose it, then use the verified full-image
restore path before starting a separately identified retry.

## Evidence required from the retry

Commit only privacy-safe evidence:

- correction summary linking the failed attempt and its log hash;
- run epoch in nonidentifying hexadecimal form;
- host and firmware full-region erased proofs with bytes, elapsed time, and
  SHA-256;
- build, application, table, and source hashes;
- first-format, cold mount, and warm remount measurements;
- parser-produced I/O, cadence, interruption, and floor results;
- explicit `power_loss=unproven` unless real controlled power-cut evidence exists;
- sanitized restore result.

Keep device MAC, tokens, original flash, raw NVS, household recordings, and
private backup paths out of committed evidence.

## Completion and next-step rule

Commit the H32 correction, successful retry evidence, and restore proof as one
reviewable experiment result only after every applicable gate passes. If H32
passes, update the durable-outbox plan with measured backend limits and proceed
to the durable-outbox demo. If it fails, write a new failure-specific correction
plan; do not reinterpret partial output as qualification.

This retry does not select the production partition layout. Single-factory and
dual-OTA remain open production choices until their separate product tradeoffs
are decided.
