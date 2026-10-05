# H35 corrected capture-preflight retry

**Status:** SDMMC transport discovery detected a card; full restoration, strict allowlist parsing, and sanitized public-mapping/privacy review passed. This is discovery evidence only, not attached-storage qualification.

## Epoch and build

- Run epoch: `a1617be8cb2d2343e744c31cc7d1b933`.
- Build source commit: `aff5e72656ec80c743fd254db8cb9a50990e9c48`.
- Prepare, build, validate, capture, and controller operations returned actual exit code 0 in managed workspace virtual-environment sessions.
- ESP-IDF: 5.4.2.
- Application image: 278,816 bytes; factory partition: 1,536,000 bytes.

## Read-only discovery

The fixture detected an SDMMC card and reported 121,503,744 sectors of 512 bytes each (62,209,916,928 bytes), using a 4-bit bus at 20 MHz. Initialization, deinitialization, and power-off error codes were all zero.

The raw H35 serial capture is 2,977 bytes; SHA-256: `30e2053b0284d08d8c6fa6fcef7db4bf33c48043543b29ccb6a2335a8c0975ed`. The CID digest remains private. The strict allowlist parser exited 0; its sanitized [summary JSON](summary.json) reports `result: detected`, `scope: discovery_only`, `qualification: unqualified`, `filesystem: not_inspected`, `media_backup: not_obtained`, and `media_writes: none`. Independent public-mapping/privacy review passed.

These records establish transport detection only. No filesystem was mounted, and no user data sectors, filesystem contents, ownership, or backup coverage were inspected. Stage A preservation and ownership gates remain unpassed. A successful initialization does not qualify the medium.

## Restoration

The controller's full-image restore proof and 16,777,216-byte readback are verified. Independent review verified all original bytes and the NVS, partition table, application descriptors, and device-proof bindings. The restored original boot capture is 8,397 bytes, SHA-256 `f26bfbc26416440be3a85f8b1dccda53b5eb9c655ddc056d579112ea74f3cd30`; independent review confirmed expected identity and healthy startup.

## Next gate

Before any mount or write, perform read-only media preservation and resolve ownership. Ownership remains unknown; default to preservation-only handling. Do not infer dedicated or disposable media from detection, capacity, or an empty listing. The ownership clarification remains pending. Keep private identities and raw data outside the repository.
