# H37 host preflight — synthetic verification

**Result:** pass — host-only synthetic checks; no H37 build or hardware operation.

The workspace `.venv` host preflight exited 0. The metadata/protocol suite reported **42 synthetic checks passed**. The controller emitted **15 mock/preflight cases**, all with the expected status: ten used-capture markers were refused unchanged before imports or device callbacks; missing serial and missing write capability failed closed before callbacks; mocked restore failure propagated; a mocked flash failure invoked restoration; and bounded command-write cases rejected short and oversized writes and propagated a write exception. The actual host-check output, including exact case names and process result, is preserved in [checks.txt](checks.txt).

The preflight uses the workspace Python environment and PySerial 3.5, constructs an unconnected `Serial(port=None)` object to inspect the API, and uses mocked device callbacks. It does not open a device port, build firmware, flash, read the BOX, or access the microSD.

## Reproduction

Run from the repository root:

```sh
./.venv/bin/python scripts/h37_sd_metadata_checks.py
./.venv/bin/python scripts/h37_attached_classification.py host-checks
git diff --check
```

The recorded combined controller command exited 0 and reported 42 metadata checks and 15 controller cases. Tested firmware/source review was approved to proceed to a build against frozen plan commit `e9636f1`; this preflight is not build evidence. Review the generated ELF, link, effective configuration, app-slot headroom, and bindings after build and before any flash. No hardware operation is authorized by this result; the BOX remains on restored original firmware and storage remains unqualified.

## Source and parser limits

The SDMMC API calls are synchronous. Setting `command_timeout_ms=1000` bounds an individual command, not a complete card initialization or multi-sector API call. Firmware checks elapsed ceilings after synchronous calls return. If a call remains blocked, the host timeout/reset is authoritative; the firmware may produce no terminal or cleanup record. An incomplete capture cannot claim successful cleanup.

The `POWER` record contains the setup error but does not distinguish failure to configure the power GPIO from failure to set its initial level after configuration. Therefore, on a failed `POWER` event only, the parser permits either `power_off_attempted=0` or `1`. This ambiguity cannot produce a successful terminal or a filesystem verdict. A successful run requires successful `POWER` and `HOST` records and applicable cleanup attempts with zero errors. The source-reviewed `power_configured` guard determines whether power-off cleanup is attempted; the failed `POWER` record alone is not cleanup proof.

The H37 firmware is ready to build after source/configuration review. It has not been built, flashed, or exercised on hardware. Stage A classification and attached-backend qualification remain incomplete.
