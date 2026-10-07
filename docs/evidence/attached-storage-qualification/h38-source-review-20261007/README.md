# H38 source and object compilation evidence

H38's host capture contract is committed in `2893946` and `32294ae`. The firmware source is committed in `98f5d5c`. Independent source review cleared the firmware for compilation and subsequent linked-image review. This is source/host evidence only; no H38 linked image, flash, format, or card I/O run has occurred.

The orchestrator independently reran the host contract checks (67 synthetic checks, exit 0), the shared C disk guard checks (14 fixture groups, exit 0), and the actual pinned FatFs formatter mock (compile and harness exit 0). The mock observed 2,062 formatted sectors / 1,055,744 bytes, 130,811 data clusters, 1,025 sectors per FAT, an eight-sector maximum driver call, and one intercepted trim request with no erase backend.

The firmware author configured ESP-IDF 5.4.2 for ESP32-S3 in an external temporary build directory using the H38 defaults and a header containing only dummy hashes. Configuration exited 0. Explicit compilation of the H38 demo, disk guard, FatFs adapter, and filesystem I/O objects exited 0; all four objects were present. No ELF was linked. The compilation preceded the firmware source commit, so its configuration version string was `32294ae-dirty`; it is not an immutable device build identifier.

To reproduce the object check, create an external temporary intent header with quoted `H38_PRIVATE_CID_SHA256`, `H38_OLD_MBR_SHA256`, `H38_WRITE_MANIFEST_SHA256`, and `H38_INTENT_SHA256` values of 64 zero hex digits, and `H38_SOURCE_REVISION` of 40 zero hex digits. Configure `FAMILY_DEMO=h38_sdmmc_filesystem`, a 32-zero `H38_RUN_EPOCH`, `H35_REFERENCE_EPOCH=a1617be8cb2d2343e744c31cc7d1b933`, that header, and `sdkconfig.defaults;sdkconfig.h38.defaults` in a separate build directory. Request only these Ninja targets:

```
esp-idf/main/CMakeFiles/__idf_main.dir/__/demos/h38_sdmmc_filesystem.c.obj
esp-idf/main/CMakeFiles/__idf_main.dir/__/common/h38_disk_guard.c.obj
esp-idf/main/CMakeFiles/__idf_main.dir/__/common/h38_fatfs_adapter.c.obj
esp-idf/main/CMakeFiles/__idf_main.dir/__/common/h38_filesystem_io.c.obj
```

The source review checked the fixed LBA-0 exception, mapped-volume bounds and callback precharge, eight-sector DMA splitting, phase limits, private identity handling, intercepted trim, absence of stock SDMMC disk registration/autoformat/physical erase, the fixed generated-file fixtures, and cleanup after synchronous operations. Returned operation failures have matching result records. An adapter-deinitialization failure cannot be represented by the frozen cleanup fields; it intentionally leaves no `COMPLETE` and must be classified as incomplete and recovered by the host.

## Controller review closeout

The final host contract is committed as `0a4140e` and passes 82 synthetic checks (independently rerun with the workspace interpreter, exit 0). Failed transcripts must preserve the first operation error, match the frozen transport/identity prefix, and report cleanup attempts consistent with acquired resources. A cleanup-stage terminal requires the complete operation schedule and four phase commands. Inconsistent cleanup evidence remains incomplete.

The full controller and host harness are committed as `fb32afb`. Independent source review returned GO for build; the orchestrator independently reran the controller harness with exit 0. It exercises the actual capture loop with a valid synthetic transcript and exact BIND/LAYOUT/FORMAT/IO/FINISH dispatch, safe draining and authoritative timeout/reset ordering after transport/evidence/storage errors, pre-capture recovery-reserve limits, exact fresh-backup approval, runtime ELF and H37-sector provenance binding, original-app boot checks, actual restore operations with injected full/NVS/partition/application/device/exit-code failures, and restoration continuation. Child outputs and detailed errors remain private. A recovered BOX after an initial restore failure does not qualify the experiment.

These tests do not exercise a device or card. Open gates are the immutable linked build and its independent review, review of the exact fresh held BOX/NVS backup, and the real H38 run with mandatory full restoration/readback and original-app startup. The card and attached backend remain unqualified.
