# On-chip storage qualification evidence

## Execution status — 2026-10-05

**H32 remains unqualified.** Corrected retry epoch
`16a5354bbfd590933f18a093dd4f154b` reached the PCM cadence and halted with
`stage=pcm,reason=cadence`. The 16 kbps Opus-sized workload reported 90 chunks
and zero missed two-second deadlines. The 67,200-byte/s PCM stress workload
reported 90 chunks, 79 missed deadlines, and backlog high-water 1. The
`seconds=180` field is the configured recording workload; it does not establish
that PCM completed within 180 seconds.

A later no-yield comparison (epoch
`d5e92d2c813b3acd2125763829469869`) also halted at PCM cadence. Opus completed
90 chunks with no deadline misses; PCM produced 90 chunks with 44 deadline
misses (33 service misses), max start lateness 2.674574 s, and backlog high-water
1. The heartbeat's maximum observed gap was 57.684 ms and no watchdog event was
reported. See [no-yield failure evidence](no-yield-20261005-failure/summary.md).
Its original image was restored after the failed comparison; the complete
16 MiB readback matched the backup SHA-256
`7d6537305e73c36aa23353923536c8d805b61f12808bf47c9664b66a3213a112`.
The [sanitized no-yield records](no-yield-20261005-failure/h32-records.txt) and
[chunk timings](no-yield-20261005-failure/chunk-timings.csv) retain the measured
operation distribution without private serial/device information.

The retry observed a complete erased-region scan, first format, five distinct
probe reboot/remount cycles, checksum-valid I/O records, and the Opus cadence.
These partial observations cannot qualify the full experiment. Interruption
recovery and near-full admission were not reached. The original 16 MiB image was restored; complete readback, partition-table,
NVS, device, and application identity checks passed. See
[restore-proof.txt](restore-proof.txt).

A prior no-yield preflight (epoch
`17e69395cf2086a79ce7c15f533dfadb`) stopped at controller sentinel validation
because of eager Python global build-path evaluation. The sentinel capture
contained a valid runtime profile, and its historical validation passes after
the controller fix. No experiment erase or start occurred; the original image
was restored and its complete readback verified. See
[no-yield preflight evidence](preflight-20261005-no-yield.txt).

See [correction-summary.md](correction-summary.md),
[failed retry summary](retry-20261005-failure/summary.json), and
[sanitized H32 records](retry-20261005-failure/h32-records.txt).
Observed I/O and chunk rows are retained as partial failure evidence, not as
accepted full-run parser output. The earlier sandbox block was historical;
hardware access subsequently succeeded and the first mount attempt failed
safely before its original image was restored.

## Implemented artifacts and plans

- Demo: `firmware/demos/h32_onchip_storage.c`
- Fixture: `firmware/partitions/h32_onchip_storage.csv`
- Isolated configuration: `firmware/sdkconfig.h32.defaults`
- Controller: `scripts/h32_storage_qual.py`
- [Original qualification plan](../../plans/onchip-storage-qualification.md)
- [Mount retry corrections](../../plans/h32-onchip-mount-retry.md)
- [PCM cadence corrections](../../plans/h32-pcm-cadence-corrections.md)

Private capture and backup paths, identities, flash images, and NVS contents
remain outside the repository. No actual power-loss result is established.
The production partition choice remains open.
