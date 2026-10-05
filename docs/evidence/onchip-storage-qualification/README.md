# On-chip storage qualification evidence

## Execution status — 2026-10-05

**H32 remains unqualified.** Corrected retry epoch
`16a5354bbfd590933f18a093dd4f154b` reached the PCM cadence and halted with
`stage=pcm,reason=cadence`. The 16 kbps Opus-sized workload reported 90 chunks
and zero missed two-second deadlines. The 67,200-byte/s PCM stress workload
reported 90 chunks, 79 missed deadlines, and backlog high-water 1. The
`seconds=180` field is the configured recording workload; it does not establish
that PCM completed within 180 seconds.

The retry observed a complete erased-region scan, first format, five distinct
probe reboot/remount cycles, checksum-valid I/O records, and the Opus cadence.
These partial observations cannot qualify the full experiment. Interruption
recovery and near-full admission were not reached. The original 16 MiB image was restored; complete readback, partition-table,
NVS, device, and application identity checks passed. See
[restore-proof.txt](restore-proof.txt).

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
