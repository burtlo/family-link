# H32 no-yield comparison — failed at PCM cadence

**Result:** failed; H32 remains unqualified. The run stopped at
`stage=pcm,reason=cadence`. It did not reach interruption recovery, near-full
admission, or safe-floor qualification. This result does not change any
acceptance gate or advance the storage phase.

## Capture identity

- Epoch: `d5e92d2c813b3acd2125763829469869`
- Profile: `no_yield` (erase yield disabled; 0 yield ticks)
- Raw capture: 193,031 bytes; SHA-256
  `a6ba63f6b6e764ef5c4fada89b58b51b08ded54b11e67454df6dbd0ad9a18a85`
- Source aggregate SHA-256: `c4bfc53628ad1b4150353d57def1d791e85fe017d740e7fc112bb687957e0166`
- Effective SDK configuration SHA-256: `97f6c180de19702bf67b0ee89682899cf40eaa49945ea3809d6b21cbc83547b8`
- Experiment application: 331,680 bytes, SHA-256 `4565a36d9c9ba4ef98d7fafcdcb7ae5eea45f63e6210266689e351e1aee43794`
- Sentinel application: 249,328 bytes, SHA-256 `48a017e3c62fd776ea3123969d3385ea4a2afc9cd82811e0b2f32770517eb020`
- Entire erased-region readback: 14,483,456 bytes, every byte `0xff`, SHA-256
  `6ef7367f20657ba1d9292cb6636c3be874c7ddc28e85fbf02302b88b2b291d15`
- The raw capture hash and byte count were checked against the supplied
  metadata. The raw capture and metadata remain in private storage; neither is
  copied into this repository.

## Observations

| Workload | Chunks | Deadline misses | Other result |
|---|---:|---:|---|
| Opus-sized, 2,000 B/s | 90 | 0 | backlog high-water 0 |
| PCM, 67,200 B/s | 90 | 44 (33 service misses) | backlog high-water 1; cadence gate failed |

For PCM, maximum chunk-start lateness was 2,674,574 µs. The heartbeat maximum
gap was 57,684 µs; watchdog events were 0. The 180-second field is the
configured workload duration and does not establish uninterrupted real-time
cadence. Reaching 90 generated chunks likewise does not turn the failed cadence
gate into a pass.
The H32 fixture kept the task watchdog disabled throughout this run to
accommodate the initial format; the interrupt watchdog remained enabled with a
300 ms timeout.
Zero reported watchdog events and the heartbeat measurement do not establish
responsiveness of the full product workload.

The measured PCM wall interval was 180.001379 seconds for 12,096,000 payload
bytes. Summed transaction service time was 170.447368 seconds, yielding about
70,966 payload bytes per second of transaction time. That average exceeds the
64,000-byte/s throughput floor, but the deadline gate still fails because 44
chunks finished after their scheduled two-second windows.

PCM operation latency, median / p95, in microseconds:

| Operation | Median | p95 |
|---|---:|---:|
| Full transaction | 1,894,707 | 2,142,590 |
| Write | 1,085,658.5 | 1,416,637 |
| Manifest | 379,495.5 | 455,486 |
| Flush | 206,764 | 273,422 |
| Read/verify | 66,852.5 | 68,301 |

These measurements describe this failed comparison only; they do not establish
a qualified throughput or safe operating limit.

## Method and limits

The H32 no-yield firmware profile was run against the isolated storage fixture.
The retained raw serial capture was inspected with its metadata, which records
the epoch, no-yield profile, payload cadence records, terminal failure, raw
capture size, and raw SHA-256. The summary above retains only aggregate
measurements and omits private device identity, operator paths, and NVS data.
The operation table uses all 90 PCM `CHUNK` records; p95 is the nearest-rank
value at sorted index `ceil(0.95 × 90) - 1`. The sanitized [H32 records](h32-records.txt)
(411 rows; SHA-256 `6a9d535300bd3df0a3a43b4f7871744117ef39d5f0ee0c19e436a1a7ae4443ba`)
and [chunk timing CSV](chunk-timings.csv) (180 rows; SHA-256
`86e3b8ccd30f8a86de0857323458dd3ab35904b3cb4374ed2c953a7739a6a8a8`)
allow recalculation without the private serial preamble.

The firmware halted after PCM cadence failed. No interruption/recovery matrix,
near-full test, or safe-floor determination was completed. The device restore
subsequently completed. The complete 16 MiB readback matched the original
image SHA-256
`7d6537305e73c36aa23353923536c8d805b61f12808bf47c9664b66a3213a112`;
partition table, NVS, application descriptors, and device identity checks passed.
Reset-based observations are not switched-power-loss evidence.
See the [restore proof](restore-proof.txt) for this epoch.

See the [evidence index](../README.md), [qualification plan](../../../plans/onchip-storage-qualification.md),
and [PCM cadence correction plan](../../../plans/h32-pcm-cadence-corrections.md).
