# H37 read-only build review

The isolated H37 firmware built and validated successfully with ESP-IDF 5.4.2. Prepare, build, and validate each returned exit code 0. The immutable build record and independent preflash review are summarized in [build-review.json](build-review.json).

Independent review verified source, SDK, partition-table, and device bindings; the 4 KiB DMA buffer alignment; and the linked image's lack of SD write/erase, filesystem, NVS, Wi-Fi initialization, flash-write, or eFuse-programming paths. Startup performs an S3 eFuse read/check; review found no eFuse programming or mutation call. The app is 264,816 bytes in the 1,536,000-byte factory partition, leaving 1,271,184 bytes (82.76%) headroom. The reviewed app erase interval is `[0x10000, 0x51000)`.

This is a preflash review, not a device result. The subsequent flash/capture operation stopped before an application write and before SD access; see the [failure summary](failure-summary.md). The device remains held in the downloadloader; the original application was not launched, and boot has not been verified.

A fresh read-only current BOX baseline was captured with actual exit code 0 and independently reviewed GO. The 16 MiB image exactly matches the failed preflash snapshot; separate NVS and partition-table dumps cover their exact configured regions. Application regions, descriptors, and partition table match the prior original baseline; every byte outside NVS is identical, and the previously observed 5,978-byte NVS difference remains unexplained. See [baseline-refresh.json](baseline-refresh.json). No card access occurred, so no card backup was required for this operation. Prior backups and failed-run evidence remain immutable; no old NVS was restored.

No new H37 epoch has started. The next gate is the source private live-device-proof patch and H37 controller tests, then a new immutable epoch. Full per-run BOX/NVS backup, complete restoration/readback, and healthy-boot proof remain mandatory. The storage backend remains unqualified.
