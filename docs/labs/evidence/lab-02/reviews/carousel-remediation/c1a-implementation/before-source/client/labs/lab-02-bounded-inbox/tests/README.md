# LAB-02 tests

**L1 (host):** run from repository root:

```text
make test
```

This executes `scripts/tests/test_lab02_host.py` (fixture JSON validation and snap math parity)
and `scripts/tests/test_lab02_model_native.py`. When a host C compiler and ESP-IDF cJSON
source are available, the latter compiles the actual C page parser and session reducer.
It exercises malformed pages, replacement after removal, result ownership, snap
deferral, revision reconciliation, failure retry, and queue overflow.

**L2/L3:** on-device scenarios — see [lab README](../README.md) and [evidence STATUS](../../../../docs/labs/evidence/lab-02/STATUS.md).

The older `test_inbox_model.c`, `test_page_merge.c`, and `test_snap_target.c`
files remain optional Unity/IDF sketches; the new `test_*_native.c` files are
run through the host suite when the toolchain is present.
