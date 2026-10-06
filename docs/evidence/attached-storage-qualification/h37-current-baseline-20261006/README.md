# H37 current-baseline build review

The current BOX/NVS baseline was captured and audited with independent GO before this immutable H37 epoch. The ESP-IDF 5.4.2 prepare, build, and validation steps each exited 0. The app is 264,816 bytes in the 1,536,000-byte factory partition, leaving 1,271,184 bytes (82.76%) headroom; its reviewed erase interval is `[0x10000, 0x51000)`. See [build-review.json](build-review.json) for the epoch and artifact bindings.

Independent root and second-review checks passed for the current-baseline/source binding, SDK, partition table, device bindings, DMA alignment, and linked-image absence of SD write/erase, filesystem, NVS, Wi-Fi initialization, and eFuse-programming paths. Startup includes an S3 eFuse read/check; no programming or mutation path was found.

The actual host-check output is preserved in [host-checks.txt](host-checks.txt): exit code 0, 42 synthetic checks, and 16 controller preflight cases. It includes the `used_current-device-proof-private.json` marker and bounded command-write checks. These are host-only synthetic checks.

Stage A completed for this epoch and was independently verified. The sanitized [classification summary](summary.json) records an MBR partition from LBA 32,768 through 121,503,743 and an exFAT signature. The two metadata reads total 1,024 bytes; [timing-summary.json](timing-summary.json) reports 3,601 μs for LBA 0 and 1,501 μs for LBA 32,768. These timings cover only the two synchronous metadata reads; they do not establish a cadence, upload, or filesystem I/O rate. No filesystem contents or names were inspected, and no SD writes occurred. Card-content backup was waived by the owner; BOX/NVS preservation and restoration were not waived.

The current operation completed with actual process exit code 0 and independently verified full 16 MiB BOX restore/readback and private device/source binding. Startup-health evidence is limited: the original application was launched and matched the expected project, SDK version, and ELF prefix; the 8,507-byte capture lasted 15.02 seconds. No actual panic, watchdog event, or USB reset code 11 was observed. A composite boot helper exited 1 because it required H32-only records; an earlier one-shot attempt failed in system Python before port open, RTS, or capture. Neither is rewritten as a successful exit. Startup observation does not establish PIN/UI/network health.

Stage A establishes bounded metadata classification only. A separately reviewed H38 filesystem/mount/I/O plan has not started; the storage backend remains unqualified.
