# LAB-02 tests

**L1 (host):** run from repository root:

```text
make test
```

This executes `scripts/tests/test_lab02_host.py` (fixture JSON validation and snap math parity)
and `scripts/tests/test_lab02_model_native.py`. When a host C compiler and ESP-IDF cJSON
source are available, the latter compiles the actual C page parser and session reducer.
It exercises malformed pages, replacement after removal, result ownership, ordinary page application during held motion and mutation
deferral, revision reconciliation, failure retry, and queue overflow.

**L2/L3:** on-device scenarios — see [lab README](../README.md) and [evidence STATUS](../../../../docs/labs/evidence/lab-02/STATUS.md).

The older `test_inbox_model.c`, `test_page_merge.c`, and `test_snap_target.c`
files remain optional Unity/IDF sketches; the new `test_*_native.c` files are
run through the host suite when the toolchain is present.

C1a actual-C terminal admission cases (`test_terminal_native.c`) cover full normal
and initial capacity, duplicate completion, concurrent transport pressure,
old-response revision, superseded operation and session. Raw `fetch_terminal`
traces must have unique session/operation identities. Set
`LAB02_TERMINAL_ARTIFACT_DIR` when running the native Python wrapper to retain them.
These are L1 injected producer/owner concurrency checks using pthread adapters,
not FreeRTOS or target confirmation.

C1b allocation cases (`test_http_startup_native.c`) compile the production HTTP
provider and session with native task/HTTP adapters. Injecting either `xTaskCreate`
failure checks owner ERROR, exactly one startup/initial terminal, duplicate start
and report handling, retained valid page-zero neighbors, and closed admission with
no undrainable request queue or false `LAB02_HTTP_READY`. Background failure also
holds input to ensure allocation failure terminalizes despite ordinary page deferral.
The success boundary checks normal prefetch and exactly one readiness claim. Set
`LAB02_HTTP_STARTUP_ARTIFACT_DIR` to save compile command and raw scenario traces.
Existing session-only and headless adapters explicitly report their simulated
consumer availability; they do not claim production task allocation evidence.

C3a acceptance adds held input for >1 second, ordinary success ahead of later
timeouts/failure, mutation acceptance separate from model application, newest
pending revision supersession, session reset, and late old producer rejection.
Actual LVGL `held-page` retains viewport/root identity while Loading message…
becomes bound content; `page-during-snap` proves page acceptance during animation.
Fast traversal and reversal still characterize F002/F003, not C3a closure.

F008 initial retry runtime is included in `test_lab02_carousel_native.py`:
actual LVGL status click → actual session → lifecycle broker → actual HTTP
provider, with explicit RTOS allocation and HTTP adapters. Task creation is
asserted outside presentation context. The session race test freezes its policy
clock while retaining real pthread contention/waits; host nanosleep overshoot
cannot accidentally inject an unrelated five-second background refresh.
