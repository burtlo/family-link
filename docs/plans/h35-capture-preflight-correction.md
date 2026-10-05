# H35 host capture preflight correction

**Status:** Ready for host correction implementation after the reviewed failed-attempt evidence and this plan are committed.

## Failure being addressed

The first bounded H35 attempt passed application readback and flash-bound checks, then failed in the host controller with `ModuleNotFoundError: No module named serial` before capture creation/readiness and before application launch. It produced no device discovery verdict. The controller exit code is unavailable because the detached launcher did not retain it; do not infer or assert a numeric code. System Python 3.14.7 lacked `serial`, while workspace `.venv` Python 3.14.7 had pyserial. Controller restore proof and independent closeout review verified all 16,777,216 restored bytes against the original; partition, NVS, application-descriptor, and device-proof bindings agree. The 8,397-byte original boot capture matches the expected project/version/ELF-prefix and healthy startup; “panic” appears only in the reset legend. Privacy/public-mapping review passed. Preserve the failed run epoch and its private artifacts unchanged; do not reuse or overwrite that epoch.

## Required correction

Before any device reset, full-image read, flash, or other device mutation, the actual interpreter that will run the controller must prove that `import serial` and the controller's required pyserial API calls work. Record the interpreter identity and package metadata privately. Use the workspace `.venv` runner, where the existing requirements already specify `pyserial>=3.5`; do not weaken or remove that dependency.

## Ordered recovery

1. Complete-device restoration and independent boot/image review have passed. Commit the reviewed failed-attempt evidence and this correction plan before implementing any correction.
2. Add the controller host preflight only to the `flash_capture`/capture path, before any `verify_device` or esptool callbacks. Do not add a global `serial` import; keep emergency restore usable without pyserial. Validate the actual interpreter, `serial` import, and required pyserial API before any device reset/full read/flash. Host-only negative checks must simulate both missing `serial` and a missing required API, replace all device-tool callbacks with sentinels, and require fail-closed behavior with zero callback invocations. Keep logs and interpreter/package paths private.
3. Obtain independent review of the correction and preflight evidence, then commit the code correction and its host-only verification.
4. Prepare a new immutable run epoch from the corrected committed source. Build and independently review its source/configuration binding, app bounds, partition table, and exact mutation interval.
5. Only after those gates pass, execute a bounded hardware attempt in a managed execution session that retains the real controller exit code. Require fresh full-image comparison, app-only flashing, capture readiness before application launch, serial capture, complete-image restoration, independent full-image readback, and restored-boot confirmation.
6. Review and sanitize the result into the evidence directory, preserving private identities and backups. Commit the reviewed evidence and resulting plan status before advancing to preservation or any later stage.

This correction is limited to host preflight and execution readiness. It does not change product firmware, device behavior, partition selection, or storage qualification criteria.
