# H35 capture runtime preflight — host verification

**Result:** pass — host-only verification, no device access.

The controller preflight ran under the repository `.venv` with Python 3.14.7 and PySerial 3.5. It validated the real `serial` import and required API using an unconnected `Serial(port=None)` object. It did not open a serial port or call device tools.

The checks also simulated a missing `serial` module, a missing required API, and all four used-epoch markers. Missing dependency/API cases failed with their expected errors and private metadata reasons, with one import attempt and zero device callbacks. Used epochs were refused before serial import; each directory/file snapshot remained unchanged. The unused-epoch positive case returned the resolved private run path and wrote a mode-0600 private preflight record without invoking device callbacks.

## Reproduction

Run from the repository root:

```sh
./.venv/bin/python -m py_compile scripts/h35_attached_discovery.py
./.venv/bin/python scripts/h35_attached_discovery.py host-checks
git diff --check
```

The sanitized output is in [`checks.json`](checks.json). Tested source commit: `b1e9882b5272b6a5a8d9a2b0bd0fa9da8f3252bd`. Controller SHA-256: `f9484ce0fd115792e1b47118c0b664f14a6de4d63f4037e180bebe55b959c435`.

This evidence covers host preflight behavior only. It is not a hardware discovery result and does not replace the next fresh immutable hardware epoch.
