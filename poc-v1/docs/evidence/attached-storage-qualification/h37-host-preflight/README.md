# H37 host preflight — synthetic verification

**Result:** host-only synthetic checks passed; the documented firmware build attempt failed before producing a usable manifest. No hardware operation occurred.

The workspace `.venv` host preflight exited 0. The metadata/protocol suite reported **42 synthetic checks passed**. The controller emitted **15 mock/preflight cases**, all with the expected status: ten used-capture markers were refused unchanged before imports or device callbacks; missing serial and missing write capability failed closed before callbacks; mocked restore failure propagated; a mocked flash failure invoked restoration; and bounded command-write cases rejected short and oversized writes and propagated a write exception. The actual host-check output, including exact case names and process result, is preserved in [checks.txt](checks.txt).

The preflight uses the workspace Python environment and PySerial 3.5, constructs an unconnected `Serial(port=None)` object to inspect the API, and uses mocked device callbacks. It does not open a device port, flash, read the BOX, or access the microSD.

## Reproduction

Run from the repository root:

```sh
./.venv/bin/python scripts/h37_sd_metadata_checks.py
./.venv/bin/python scripts/h37_attached_classification.py host-checks
git diff --check
```

The recorded combined controller command exited 0 and reported 42 metadata checks and 15 controller cases. Source review against frozen plan commit `e9636f1` approved proceeding to a build; this preflight is not build evidence.

## Firmware build attempt

A build was attempted in ESP-IDF 5.4.2 using a fresh immutable epoch whose identifier is retained in private evidence. Preparation exited 0; build exited 1; validation exited 1 because the expected manifest was not produced. The compiler treated `-Werror=misleading-indentation` as an error at the READ_RESULT hash-encoding loop: the trailing NUL assignment appeared visually guarded by the loop. The source was corrected by bracing the loop and placing the assignment on its own line; behavior is unchanged. The failed epoch remains immutable. Any retry must use a new immutable epoch. No flash, BOX read, or microSD access occurred. This correction has not been rebuilt or validated yet.

After a successful build, review the generated ELF, link, effective configuration, app-slot headroom, and source/build bindings before any flash. No hardware operation is authorized by this result; the BOX remains on restored original firmware and storage remains unqualified.

## Source and parser limits

The SDMMC API calls are synchronous. Setting `command_timeout_ms=1000` bounds an individual command, not a complete card initialization or multi-sector API call. Firmware checks elapsed ceilings after synchronous calls return. If a call remains blocked, the host timeout/reset is authoritative; the firmware may produce no terminal or cleanup record. An incomplete capture cannot claim successful cleanup.

The `POWER` record contains the setup error but does not distinguish failure to configure the power GPIO from failure to set its initial level after configuration. Therefore, on a failed `POWER` event only, the parser permits either `power_off_attempted=0` or `1`. This ambiguity cannot produce a successful terminal or a filesystem verdict. A successful run requires successful `POWER` and `HOST` records and applicable cleanup attempts with zero errors. The source-reviewed `power_configured` guard determines whether power-off cleanup is attempted; the failed `POWER` record alone is not cleanup proof.

The warning fix is ready for a fresh build attempt; no successful build, flash, or hardware exercise is evidenced here. Stage A classification and attached-backend qualification remain incomplete.
