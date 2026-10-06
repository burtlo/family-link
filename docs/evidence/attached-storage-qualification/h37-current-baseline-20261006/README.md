# H37 current-baseline build review

The current BOX/NVS baseline was captured and audited with independent GO before this immutable H37 epoch. The ESP-IDF 5.4.2 prepare, build, and validation steps each exited 0. The app is 264,816 bytes in the 1,536,000-byte factory partition, leaving 1,271,184 bytes (82.76%) headroom; its reviewed erase interval is `[0x10000, 0x51000)`. See [build-review.json](build-review.json) for the epoch and artifact bindings.

Independent root and second-review checks passed for the current-baseline/source binding, SDK, partition table, device bindings, DMA alignment, and linked-image absence of SD write/erase, filesystem, NVS, Wi-Fi initialization, and eFuse-programming paths. Startup includes an S3 eFuse read/check; no programming or mutation path was found.

The actual host-check output is preserved in [host-checks.txt](host-checks.txt): exit code 0, 42 synthetic checks, and 16 controller preflight cases. It includes the `used_current-device-proof-private.json` marker and bounded command-write checks. These are host-only synthetic checks.

The operator is authorized to conduct the bounded read-only Stage A classification. Hardware work for this epoch is in progress, but this evidence claims no device or filesystem result. Full current BOX backup, complete restore/readback, and healthy-boot proof remain mandatory. The card-content backup waiver does not cover BOX/NVS. The backend remains unqualified.
