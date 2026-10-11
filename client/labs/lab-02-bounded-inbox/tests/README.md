# LAB-02 tests

**L1 (host):** run from repository root:

```text
make test
```

This executes `scripts/tests/test_lab02_host.py` (fixture JSON validation and snap math parity with `components/lab02/snap_math.c`).

**L2/L3:** on-device scenarios — see [lab README](../README.md) and [evidence STATUS](../../../../docs/labs/evidence/lab-02/STATUS.md).

The `test_*.c` files here are optional future Unity/IDF unit targets; they are not wired into CI yet.
