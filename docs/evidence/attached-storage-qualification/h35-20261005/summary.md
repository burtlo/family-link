# H35 SDMMC discovery attempt

**Status:** `host_capture_dependency_failure`; controller restoration proof and full readback are verified, with independent post-run review verified. No discovery verdict is available.

## Build and boundary

- Source commit: `5170a92` (H35 isolated SDMMC discovery implementation).
- ESP-IDF: 5.4.2.
- Validated application image: 278,816 bytes.
- Factory partition: 1,536,000 bytes.
- Application-only flash interval: `[0x10000, 0x55000)`.
- Run epoch: `213b811d4944ebedfddf32ca4b800e07`.
- Independent preflash GO discovery passed for the final build and bounded procedure.

## Attempt result

The H35 operation passed the app readback and flash-bound checks. Independent comparison confirms that the app readback matched the validated build and that the 16 MiB pre-flash and post-flash images differed only within the authorized app interval `[0x10000, 0x55000)`.

The host controller then raised `ModuleNotFoundError: No module named serial` before serial capture creation/readiness and before application launch. Therefore no H35 serial capture was created and no SDMMC/card initialization result exists. The correct verdict is `host_capture_dependency_failure`; this says nothing about whether a card is present. The detached launcher failed to retain the actual controller exit code; it is unavailable, and no numeric exit code is asserted. Runtime observations show system Python 3.14.7 lacked `serial`, while workspace `.venv` Python 3.14.7 had pyserial. Retry must use a managed execution session that retains the controller exit code.

## Restoration and independent review

The controller restoration proof is verified, including a full 16,777,216-byte readback. Independent closeout review confirmed all restored bytes match the original and that partition, NVS, application-descriptor, and device-proof bindings agree. The original boot capture is 8,397 bytes; its SHA-256 is `65e2e4c459b4ca90035e4e39425599733cbe210b505bf32fab7b836333573388`. Review confirmed this matches the original project/version/ELF-prefix and shows healthy startup; the word “panic” appears only in the reset legend. The app readback matched the validated build, and the pre/post 16 MiB comparison showed no differences outside `[0x10000, 0x55000)`. Privacy and public-mapping review passed.

## Evidence limits

No discovery verdict is available. H35 does not mount a filesystem, read user data sectors, write the medium, initialize USB host, or change eFuses. No filesystem or removable-media content has been inspected in this epoch.

No H35 serial capture exists, so there is no H35 capture digest or byte count. Device identity, complete-image/NVS hashes, raw logs, and private run paths remain private and are intentionally omitted here.
