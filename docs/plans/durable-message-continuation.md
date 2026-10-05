# Durable message experiments: continuation instructions

Updated 2026-10-05. This is an operator handoff, read alongside the [ordered experiments](long-message-experiments.md). The detailed current stopping point and resume sequence are in the [continuation checkpoint](continuation-checkpoint-2026-10-05.md). That checkpoint supersedes the older “Immediate work” instructions below where they conflict.

**Current checkpoint:** H34 is complete; H32 failed its PCM deadline gate and
remains unqualified. The H32 write-coalescing candidate stopped at the
[read-only feasibility gate](../evidence/onchip-storage-qualification/coalescing-preflight.md).
The H35 bounded attempt failed before application launch with
`host_capture_dependency_failure`; it has no serial capture or media discovery
verdict. Its complete restoration and boot were independently verified, and the
reviewed failed-attempt evidence was committed as `7f89a07`. The host capture
preflight correction and sanitized host-only checks are now committed; the
controller passed the actual workspace `.venv` pyserial preflight without
opening a port. The next step is a fresh corrected immutable hardware epoch.
Preserve the failed H35 epoch unchanged; it is not a retry candidate. Follow [the continuation
checkpoint](continuation-checkpoint-2026-10-05.md) for exact status and next
actions; do not follow the historical H32 retry instructions below as current
work.

## Working agreement

The user authorized unattended implementation, server control, device flashing, generated audio, subagents, verification, and local commits. Finish one experiment with reproducible evidence and a commit before advancing to its dependent experiment. Write or amend its plan before implementation. Use separate implementation and independent review agents. A build alone does not qualify firmware: retain device serial evidence and a verified restore. Model policy: use gpt-6-luna for small implementation, operations, and routine documentation; use gpt-6.1-sol for outlining and independent verification/review.

Read `docs/AGENTS.md`, [architecture](../LONG-MESSAGE-ARCHITECTURE.md), [protocol](../MESSAGE-PROTOCOL.md), [server storage](../SERVER-MESSAGE-STORAGE.md), [playback](../STREAMING-PLAYBACK.md), and [sketch timeline](../SKETCH-TIMELINE.md). These standards describe the intended system; they do not mean X02 already implements it.

## Completed H34 server proof

The implementation is committed as `e84c60e`, its configurable preview TTL as `81cf065`, the stabilized qualification harness as `d0f70c4`, and evidence/status updates as `f928c9e`.

The retained [H34 evidence](../evidence/h34-message-store/README.md) identifies tested source `d0f70c42781ecb433dd7d624ce016cf87b1db955`. All 21 functional checks and 50 process-crash boundaries passed. The deterministic fixture is 180 seconds of 16 kHz mono PCM: 90 two-second chunks, 5,760,000 PCM bytes, and 5,760,044 WAV bytes. Server allocation peaks remained below the declared 1 MiB threshold.

The proof covers host PCM/WAV storage, durable chunk acknowledgements, recoverable completion, inbox reconstruction, bounded Range responses, strict authorization, cull/restore, storage admission, and corruption isolation. Opus/Ogg, sketches, notifications, device outbox, device playback, and X02 integration remain later work. Process termination evidence does not establish switched-power-loss durability.

To reproduce when a concrete change requires it:

```bash
make install-server
set -o pipefail
make qualify-message-store 2>&1 | tee docs/evidence/h34-message-store/qualification.log
```

Do not rerun the complete suite merely to re-establish already retained results. After a code change, commit the tested source before the final evidence run so `source_revision` identifies it; commit the generated evidence afterward.

## Historical record: corrected H32 storage qualification (completed; failed PCM gate)

The instructions in this historical section describe the completed H32 attempt
and must not be treated as the current next step. See the pause checkpoint for
the H35 attached-storage discovery state and ordered remaining work.

The completed H32 attempt used [the H32 retry plan](h32-onchip-mount-retry.md) and [the original qualification plan](onchip-storage-qualification.md). Its PCM cadence failure is recorded in the evidence above. Do not treat older placeholder evidence as a successful device result or repeat this attempt as the current next step.

The first attempt halted safely at mount: stale experiment NVS progress was mistaken for filesystem generation authority after the host erased the outbox. It scanned only 4 KiB. The correction requires a host-generated 128-bit epoch, an entire-region erased proof, separate preservation/progress NVS namespaces, and a checked filesystem marker.

The original device was restored and its complete readback matched the backup. Expected original hashes are:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Full flash | 16,777,216 | `7d6537305e73c36aa23353923536c8d805b61f12808bf47c9664b66a3213a112` |
| Partition table | 4,096 | `06b412567cc48815931a3f67280450f31de7c3cc3e85747e71f3a4dc0219db88` |
| Original NVS | 24,576 | `67235fa2b245383d6bca8962f9be12523459871b475d9361e8a3ee340fd8387e` |

Locate the existing restricted backup directory from local operator context. Keep flash images, raw NVS, device fingerprints, tokens, and private operator paths outside Git. Revalidate byte counts and hashes before hardware mutation; never overwrite the original images. Detect the current USB port rather than assuming its suffix.

### Finish implementation and independent review

1. Finish `scripts/h32_storage_qual.py`: immutable `prepare-run` epoch, epoch-bound build manifests, full erased readback, exact device/backup/NVS binding, serial capture before first launch, strict evidence parser, and restore while held in the bootloader.
2. Finish `firmware/demos/h32_onchip_storage.c` and H32-only build inputs: complete partition scan/hash, outbox-only format on proven blank state, read-only preservation sentinel, separate `h32_run` progress, checked epoch marker and remount, actual runtime flash/partition facts, and epoch on every proof record.
3. Validate every retained committed file and its metadata on fault recovery, not just the first audio chunk. Cleanup must succeed before advancing the NVS cursor. Validate cadence manifests by content, epoch, size, seed, and hash.
4. Independently review all flash bounds, launch ordering, blank authority, marker rules, parser shapes, and restore sequencing. Resolve blockers before flashing.

Build input ABI: `-DH32_RUN_EPOCH=<32 lowercase hexadecimal characters>` for both sentinel and experiment builds. Runtime records must include `RUN_REQUEST`, full `ERASE_SCAN`, and `RUN` with `event`, `blank_authority`, `nvs_action`, and `status`; subsequent evidence carries the same epoch. Follow the current CLI help because the controller is being corrected.

### Execute a fresh, isolated retry

Use a new restricted run directory outside the repository. Generate its epoch once. Use dedicated sentinel and experiment build directories. Bind manifests to source hashes, ESP-IDF version, epoch, partition fixture, and image hashes. Require 15% application slot headroom.

The exact fixture is factory `0x10000 + 0x220000` and outbox `0x230000 + 0xDD0000` on the verified 16 MiB ESP32-S3. This is an experiment fixture; the production choice between single factory and dual OTA remains open.

Execute the following dependency sequence using the corrected controller:

1. Verify original backups and connected device identity; prepare the new run.
2. Build and validate both modes and exact decoded partition table.
3. Flash sentinel application within the original application bounds; capture and validate sentinel-only/no-storage-access proof; retain post-sentinel NVS hash.
4. Erase only the exact outbox region. Read back all 14,483,456 bytes, verify every byte is `0xff`, and compare the entire-region SHA with the expected erased hash.
5. Flash experiment bootloader/table/application without launching firmware. Verify each image readback and unchanged post-sentinel NVS.
6. Open serial capture before first launch. Capture the full scan, exactly one format/new-run, verified marker remount, five probe reboots, I/O matrix, both cadence runs, all 90 fault/recovery cycles, and near-full/reclaim phases.
7. Require one epoch and build set throughout. Parser must reject `FAIL`, invalid bounds, repeated formats, missing marker verification, duplicate proof identities, mismatched fault/recovery order, incomplete cadence, or watchdog failure. Derive evidence only into a fresh staging directory and publish atomically after acceptance.
8. Restore the original complete image while held in the bootloader. Read back the entire image before starting firmware, compare full hash, table, NVS, and application descriptors; retain private proof and publish a sanitized result.

If a gate fails, retain the failed attempt separately, diagnose it, restore the device, and write a failure-specific correction plan before another epoch. Never combine failed and successful logs. Label software restart as `esp_restart`; real power-cut behavior remains unproven without a controllable power device.

### Close H32

Update the H32 evidence index to replace stale statements about USB denial/no mutation. Record measured capacity, latency, cadence, recovery, safe floor, build identities, full erased proofs, and restore result. Update both H32 plans and Phase 4 roadmap only for gates supported by retained evidence. Commit the complete H32 implementation and evidence separately from H34. Do not commit raw backups, credentials, or household audio.

## Subsequent dependency order

After H32 succeeds, update [durable-outbox-demo.md](durable-outbox-demo.md) with the measured backend limits, then implement the durable outbox against the qualified server and storage. Prove a good recording survives server unavailability, retry, reset, and restart and arrives as exactly one completed message. Use generated fixtures because nobody is available to speak.

Follow the roadmap for bounded device playback and the end-to-end codec proof. Opus encoding has an existing [completed demo](opus-demo.md); server Ogg materialization and durable device lifecycle still need their own evidence. Keep the current X02 implementation separate until the required islands are qualified and the integration plan is explicit.

Extend sketch capture to the entire message lifetime and validate the timeline standard after storage/audio dependencies. Complete notification/position lifecycle and an X02 integration plan afterward. At each transition, record the outcome, remaining uncertainty, exact next plan, evidence links, and commit before advancing.
