# H37 read-only build review

The isolated H37 firmware built and validated successfully with ESP-IDF 5.4.2. Prepare, build, and validate each returned exit code 0. The immutable build record and independent preflash review are summarized in [build-review.json](build-review.json).

Independent review verified source, SDK, partition-table, and device bindings; the 4 KiB DMA buffer alignment; and the linked image's lack of SD write/erase, filesystem, NVS, Wi-Fi initialization, flash-write, or eFuse-programming paths. Startup performs an S3 eFuse read/check; review found no eFuse programming or mutation call. The app is 264,816 bytes in the 1,536,000-byte factory partition, leaving 1,271,184 bytes (82.76%) headroom. The reviewed app erase interval is `[0x10000, 0x51000)`.

This is a preflash review, not a device result. The operator is authorized to conduct the bounded read-only Stage A classification, which is in progress; no classification result is asserted here. Full BOX/NVS backup, complete restoration/readback, and healthy-boot proof remain mandatory for the device epoch. The card-content backup waiver does not apply to original BOX flash or NVS. The storage backend remains unqualified.
