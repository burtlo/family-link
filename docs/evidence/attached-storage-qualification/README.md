# Attached-storage qualification evidence

**Status:** H35 attempt stopped with `host_capture_dependency_failure`; controller restoration proof and full readback are verified, with independent post-run review verified. No media discovery verdict is available.

This directory records the isolated H35 SDMMC discovery attempt. The attempt reached application readback and verified its flash bounds, but the host controller failed before serial capture was created or reported ready. This is a host capture dependency failure, not an inconclusive media result and not evidence that a card is absent. Independent closeout review verified all 16,777,216 restored bytes against the original and confirmed partition, NVS, application-descriptor, and device-proof bindings. The 8,397-byte boot capture matches the original project/version/ELF-prefix and shows healthy startup; the word “panic” appears only in the reset legend. The privacy and public-mapping review passed.

## Current evidence

- [H35 epoch summary](h35-20261005/summary.md) records the build, bounded flash/readback checks, capture failure, and independently verified restoration.
- The application is an isolated discovery fixture built with ESP-IDF 5.4.2. The validated app image is 278,816 bytes; the factory partition is 1,536,000 bytes. The app-only flash interval is `[0x10000, 0x55000)`.
- Independent comparison confirms the app readback matched the validated build. The 16 MiB pre-flash and post-flash images differed only within the app interval. The controller restore proof and independent review verify all 16,777,216 restored bytes against the original, with partition, NVS, application-descriptor, and device-proof bindings in agreement. The original boot capture is 8,397 bytes with SHA-256 `65e2e4c459b4ca90035e4e39425599733cbe210b505bf32fab7b836333573388`.
- The serial capture was not created. There is no H35 card initialization result or discovery verdict.

Private raw data, device identity, complete-image/NVS hashes, and run locations remain outside the repository. No H35 serial capture exists. The controller exit code is unavailable because the detached launcher did not retain it; the known uncaught exception is `ModuleNotFoundError: No module named serial`. Host runtime observation shows system Python 3.14.7 lacked the module while workspace `.venv` Python 3.14.7 had pyserial. A managed execution session must retain the real controller exit code on retry. The full acceptance and safety criteria are in [the attached-storage qualification plan](../../plans/attached-storage-qualification.md).

## Qualification boundary

H35 is a read-only SDMMC discovery fixture. Even a successful card initialization would establish only that the fixture detected and identified an SD card at the reported level. It would not establish filesystem type or contents, ownership, backup coverage, mount safety, write behavior, durability, recording cadence, fault recovery, near-full behavior, or backend qualification. Those gates remain separate and require the preservation and authority process in the plan.
