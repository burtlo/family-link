# H32 correction and retry evidence

## Historical attempts

The first implementation attempt was blocked by sandbox USB access and an
ESP-IDF component-manager process query. That restriction did not persist.
Later hardware execution preserved the original image, flashed the storage
fixture, and reached a controlled first-mount failure on 2026-10-04.

The failed privacy-safe capture is `logs/h32-experiment-probe.txt` with SHA-256
`793a8b6162969d9013adc3df4fd0e757c96b22b19e0ee35f917c709a16fde71f`.
It reported an existing preservation sentinel, an erased **4,096-byte sample**
of a 14,483,456-byte outbox, disabled first-mount formatting, and halted with
`stage=mount,reason=unsafe_or_io`. That attempt did not qualify the filesystem.

The first-mount policy trusted stale NVS progress over a separately erased
outbox. The correction requires host and firmware verification of every outbox
byte, separate sentinel and progress namespaces, and an immutable requested run
epoch. Nonblank flash can resume only with a matching validated filesystem
marker; it never authorizes formatting through NVS or a mount error.

The original full flash was subsequently restored and verified by the
orchestrator. The corrected retry requires its own independent restore proof;
the prior restore cannot satisfy the current run's acceptance gate.

## Corrected retry — failed at PCM cadence

Requested epoch: `16a5354bbfd590933f18a093dd4f154b`. Isolated experiment and
sentinel application sizes were 328,144 and 235,728 bytes. The preservation
sentinel was created by the sentinel-only build, which reported
`storage_access=no`; experiment boots reported the existing sentinel.

The initial firmware scan covered all 14,483,456 bytes and reported all erased
with SHA-256
`6ef7367f20657ba1d9292cb6636c3be874c7ddc28e85fbf02302b88b2b291d15`.
The observed format time was 741,694 µs, first mount 5,486 µs, and filesystem
capacity 14,323,712 bytes. Those are measurements from an unsuccessful run,
not a qualified capacity or safe-floor recommendation.

The log contains five distinct successful probe reboot/remount cycles and
40 successful I/O rows (20 each at 65,536 and 196,608 bytes). It also contains
later repeated cycle-5 status rows; retain their ordering and let the parser
apply the schema rather than silently deleting duplicates.

Opus-sized cadence: 90 chunks at 2,000 payload bytes/s, zero missed deadlines,
backlog high-water 0. PCM stress cadence: 90 chunks at 67,200 payload bytes/s,
79 missed deadlines, backlog high-water 1. Maximum PCM write time was
2,175,479 µs and maximum manifest time 952,832 µs. These maxima may refer to
different chunks and must not be summed into a claimed measured worst chunk.
The existing backlog field measures per-chunk service time divided by the
two-second period; accumulated schedule lateness needs additional instrumentation.

Firmware halted with `stage=pcm,reason=cadence`. Phase 5 interruption recovery
and Phase 6 near-full behavior were not reached. The current retry's original-image restore completed
with controller exit code 0 and an independently checked complete 16 MiB
readback matching the original image. Partition table, original pre-sentinel
NVS, device fingerprint, and application descriptors were verified before
launch. See [restore-proof.txt](restore-proof.txt). See [failure summary](retry-20261005-failure/summary.json)
for raw/sanitized capture hashes and [PCM correction plan](../../plans/h32-pcm-cadence-corrections.md)
for the next experiment. Retained partial evidence must never be promoted to a
passing full qualification.
