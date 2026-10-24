# Family Link V2 server lifecycle, session, and hangout reliability audit

**Date:** 2026-10-24 (America/Chicago)  
**Scope:** Product server at `server/device/main/server.py`, the product device's roster/session exchange, owning scripts, contracts, and host tests. The numbered findings are investigation evidence; the post-audit corrections are recorded separately in section 11.  
**Source snapshot:** `29981de6` on the inspected checkout. Findings describe this snapshot, not a deployed build.  
**Severity key:** P0 = data loss, security/isolation failure, or inability to recover from a common operation; P1 = serious availability/lifecycle/session defect; P2 = reliability or failure-handling weakness; P3 = diagnostics/maintainability. **Confidence** describes evidence for the stated mechanism, not attribution to either field incident.

Sections 1–10 preserve the pre-change investigation evidence. Section 11 records the narrowly scoped corrections and current verification status.

## 1. Executive summary

The product server is one synchronous `ThreadingHTTPServer` process. Deployment YAML is loaded once before the listener binds; JSON and media files in `server/device/runtime/` supply durable state; a process-local `Store` and one mutex supply runtime state. There is no database, asyncio loop, server background task, or server-owned child process. The device obtains the hangout with its **desk** Bearer, before PIN login. A missing user session therefore cannot by itself explain a failed `GET /v2/hangout`.

The highest-risk confirmed issue is operational: the documented host test class deletes **all files** in the product runtime directory during setup. Running that class against a populated checkout can destroy accepted-media files and authentication records (F01). An isolated actual-process probe also reproduced an immediate restart failing with `[Errno 48] Address already in use` after SIGKILL and active HTTP requests; the server deliberately disables address reuse (F02). This can leave the device in its intended continuing `connecting...` retry state. Neither result proves the cause of the reported field failure, whose process identity, startup stderr, HTTP response, and device trace were not supplied.

The observed lingering Python process remains unexplained. In three isolated starts with only a health request, the owned server PID exited within four seconds after SIGINT, SIGTERM, and SIGKILL, respectively. Source still lacks an explicit graceful shutdown path, socket close, and lifecycle diagnostics (F03), but that omission is not proof that the reported process was this server. The product device bounds each roster request at the model level and retries at 1/2/4/30 seconds; there is **no terminal overall connecting deadline** by the accepted product contract.

The route `/health` declares `ready` once the listener is running without checking that persisted deliveries or media were restored (F04). Slow clients and storage can occupy unbounded request threads, and several paths hold the global lock through disk I/O or HTTP writes (F05). Sessions are only restored from PIN operation records accepted within 60 seconds, while a live session can remain valid for 180 seconds after refresh; this is a limited restart guarantee permitted by the current contract, not a direct roster defect (F06).

## 2. System lifecycle map

| Phase | Actual path and owner | Failure/recovery behavior |
|---|---|---|
| Command | `make server` runs `scripts/make/python.sh host-run`, which `exec`s host Python; `main()` may `os.execve` into `.venv` if YAML dependencies are missing (`Makefile:141-143`; `scripts/make/python.sh:63-70`; `server/device/main/server.py:320-363,2558-2585`). `exec` replaces the PID; it does not create a surviving child. | The Make/shell parents can differ from the Python PID. A separate launcher or unrelated Python process requires a process-tree observation. |
| Import | Fixture manifest/hashes and media load at module import; `Store` loads PIN records, open uploads, last-viewed, accepted tombstones, and reconstructs delivery (`server.py:79-90,193-208,894-983,1280-1290`). | Malformed PIN/last-viewed JSON can abort before bind. Malformed tombstones or open-upload metadata can instead be silently skipped. No startup status event identifies this distinction. |
| Configuration | `create_server()` validates static deployment, optional TLS pair, then optionally reloads tombstones; constructs bounded log and binds socket; TLS wrapping occurs after bind (`server.py:220-317,2504-2526`). | Invalid YAML fails closed. A second listener on the same address fails bind. There is no interprocess lock for two instances on different ports sharing `runtime/`. TLS setup failure after bind does not explicitly close the partially constructed server object. |
| Serving | `LabServer` is `ThreadingHTTPServer`, `daemon_threads=True`, `allow_reuse_address=False`; each connection has a handler thread (`server.py:1303-1317`). No server worker/timer/pool is created. | There is no thread count or read deadline. A slow request can occupy a thread; rapid replacement can fail to bind. |
| Liveness/readiness | `/health` returns static manifest/run data and labels itself `ready`; `/v2/hangout` independently checks desk Bearer and serializes the already loaded `ROSTER` (`server.py:1717-1740`). | `/health` is evidence that an HTTP handler answered, not proof of current storage, delivery, session recovery, or device reachability. A valid empty roster is distinct from an error. |
| Shutdown | `main()` calls `serve_forever(0.1)` and only closes the optional log in `finally` (`server.py:2588-2593`). There is no signal handler, `shutdown()`, `server_close()`, or request drain in this entry path. | SIGINT raises `KeyboardInterrupt` in the main thread and can exit with a traceback; SIGTERM uses the OS default termination. Daemon request threads do not guarantee completed writes. The OS closes descriptors on actual process exit. |
| Restart | New process repeats import, deployment load, bind, and optional implicit state restoration. `main()` does not request `reload_tombstones=True`; the import-time `Store` supplies the restoration (`server.py:983,1290,2504-2519`). | PIN/login replay and accepted sends have partial durable recovery. Endpoint health clock, active handler threads, and some session state are process-local. A failed bind means no new process serves the device. |

## 3. Session and hangout request map

```text
Device boot / unmute / Wi-Fi return
  -> login model begins roster operation (1 in flight in model)
  -> device HTTP worker GET /v2/hangout with configured desk Bearer
  -> server matches that Bearer to the one deployment endpoint
  -> server returns static {schema, hangout_id, revision, users}
  -> device parses and validates body; ready roster or valid empty hangout
  -> on transport/non-200/invalid body, connecting + timed retry

Select user + PIN
  -> POST /v2/session/login with desk Bearer, selected user, PIN, UUID
  -> under Store.lock: replay check / PIN check / 32-byte random session
  -> persist PIN operation record, then return session Bearer
  -> GET /v2/inbox and other signed-in routes resolve session -> user
  -> POST /v2/session/refresh every 60 s while signed in
  -> mute sends best-effort invalidate; local late-result fence
```

The server's desk credential and endpoint identity come from a single deployment endpoint (`server.py:270-315,1617-1630`). `/v2/hangout` uses only that credential (`server.py:1733-1740`); it does **not** create or consult a user session. PIN login uses endpoint-binding plus operation UUID as the replay key, saves a result before responding, and issues a user/endpoint-bound session (`server.py:1919-2008`). User-scoped routes call `_session_user()`, which checks existence, binding, revocation, and 180-second age, returning 401 for missing/revoked and 403 for expired (`server.py:1632-1661,1741-1745`). Refresh updates the session and its persisted PIN record when present (`server.py:2067-2107`). The accepted contract explicitly says a restart may drop a session; the device remains signed in after failed refresh until mute and new PIN sign-in (`docs/product/communication-contracts/user-scoped-envelope.md:48-54`; `endpoint-health-and-login.md:54-60,74-85`).

The device worker uses `CONFIG_FAMILY_P1_BASE_URL` and a Kconfig/NVS desk token; if token loading fails, its service returns without dispatch (`client/device/main/product_login_http_worker.cpp:91-107,342-350,395-405`). It calls `esp_http_client_perform` with the model-selected 1-second or 3-second timeout, then classifies the response (`product_login_http_worker.cpp:161-190,200-211,284-288,445-452`; `client/shared/login_session/fl_product_login/p1_wire.cpp:185-280`). The model has one roster operation at a time, records timeout/failure, and retries while connecting at 1, 2, 4, then 30-second intervals (`client/shared/login_session/fl_product_login/session.cpp:261-280,502-545,702-774,825-856,874-925`). The UI shows `connecting...` for that state (`client/device/main/product_login_bridge.cpp:659-694`). Each **model operation** has a terminal result or timeout; the **screen state** has no overall deadline or separate permanent-failure screen. If the HTTP worker never runs (missing token, task failure, stalled UI loop), the model timer still needs to be observed on target before asserting that physical retries occur.

Multiple physical devices are not separately identified by this server's deployment: it requires exactly one endpoint (`server.py:270-282`). Two devices configured with its desk token share one endpoint identity and health clock; their user sessions have distinct tokens/operation IDs but no distinct physical-device identity. This matches the current single-endpoint pilot constraint, not a demonstrated cross-user leak.

## 4. Observed failure investigations

### A. Ctrl+C appears to stop the server, but a Python process remains

**What is established:** There is no custom SIGINT/SIGTERM handler. The main-thread server is a blocking `serve_forever`; its `finally` closes only the optional log (`server.py:2588-2593`). The request threads are daemon threads (`server.py:1303-1305`), and no server-owned subprocess or asyncio task is created. The command wrapper and dependency bootstrap use `exec` (`scripts/make/python.sh:63-70`; `server.py:344-363`), preserving rather than forking the process PID. A scratch copy of the complete product entrypoint, config, fixtures, and separate runtime bound to `127.0.0.1` on unique temporary ports. Its owned PID answered `/health`, then exited within four seconds after each of SIGINT (exit `-2`), SIGTERM (`-15`), and SIGKILL (`-9`).

**What remains unknown:** There is no PID/PPID/process-group/listening-port capture from the reported incident. The remaining Python may have been the same server still serving, a separately launched server, a launcher/test process, or unrelated. This environment denied `ps` (`operation not permitted`), so the active workstation process tree was not inspected. Lack of explicit `server_close()` is a real lifecycle defect, but the isolated signal result refutes treating it alone as a proven explanation for a surviving PID. A second Ctrl+C, interrupted cleanup, blocked log/terminal output, or a different launch command also needs incident evidence.

**Disambiguating capture:** Before touching the process, record PID, parent PID, process group, executable/arguments, start time, and owning PID of port 8777; send one Ctrl+C; record the same fields and exit status. On macOS, `ps -o pid,ppid,pgid,stat,lstart,command -p <recorded PID>` and `lsof -nP -iTCP:8777 -sTCP:LISTEN` are useful operator commands. Do not terminate by name or kill unrelated Python processes. Capture startup/shutdown stderr with credentials redacted.

### B. Hangout retrieval fails after force termination/restart; device keeps `connecting...`

**Supported path:** A genuine `/v2/hangout` response depends on successful process import/configuration, socket bind, network routing to the configured URL, matching desk Bearer, and a body the client accepts (`server.py:220-317,1303-1309,1733-1740`; device worker/model citations above). User-session persistence is not on this path. In one owned scratch-copy experiment, after login and inbox HTTP activity, SIGKILL completed and an immediate same-port replacement exited with `[Errno 48] Address already in use`. A separate five-cycle scratch probe with roster requests alone restarted successfully each time. The source sets `allow_reuse_address=False`, so a rapid restart/bind failure is a **reproduced timing-dependent** explanation, not attribution to the field incident.

Other unconfirmed explanations include a previous listener still owning the port, failed import or deployment load, wrong device URL or desk token after deployment/rebuild, unreachable server network, an HTTP worker not running, or an invalid/truncated roster response. A successful `/health` would not by itself settle these; compare a same-network authenticated `/v2/hangout` response and the device's `roster op=... http=... perform=... parse=... valid=... users=...` log (`product_login_http_worker.cpp:200-211`). Do not print the Bearer or PIN. The accepted retry policy permits `connecting...` to persist through a continuing outage; it is not evidence of one indefinitely blocked HTTP request.

## 5. Findings register

| ID | Priority | Finding and affected components | Confidence |
|---|---|---|---|
| F01 | **P0** | Product host test setup deletes durable runtime files | Confirmed source defect |
| F02 | **P1** | Rapid same-port restart can fail to bind | Confirmed in isolated process probe; timing dependent |
| F03 | **P1** | Entry shutdown does not explicitly close listener or drain/record work | Confirmed lifecycle omission; field cause unproven |
| F04 | **P1** | `/health` can report ready with silently missing accepted deliveries | Confirmed source mechanism |
| F05 | **P1** | Unbounded connection threads and blocking I/O under global lock | Strongly supported availability risk |
| F06 | **P2** | Session restoration window is shorter than live session lifetime | Confirmed behavior; permitted by contract |
| F07 | **P2** | Two instances on different ports can write the same runtime without coordination | Strongly supported race/data-integrity risk |
| F08 | **P2** | PIN replay records accumulate indefinitely | Confirmed source mechanism |
| F09 | **P2** | Lifecycle/recovery diagnostics are incomplete or can disappear | Confirmed source mechanism |

### F01 — Host tests can erase the product runtime (P0)

- **Finding / evidence:** `scripts/tests/test_lab_p1_host.py:18-22,111-124` binds `RUNTIME` to `server/device/runtime/`; every `P1HostTests.setUp()` unlinks every child except `.gitignore`, and teardown unlinks the PIN record. `Makefile:103-105` discovers this class through `make test`; `server/device/README.md:7` explicitly recommends it. The inspected runtime directory contained accepted-media, tombstone, last-viewed, and PIN files; their contents were not modified or disclosed.
- **Failure mechanism / impact:** A routine host test erases durable delivery and authentication files. Later restart can lose messages or session replay; `/health` may still say ready. Missing accepted audio prevents `_apply_accepted_delivery_locked()` from restoring an item (`server.py:1248-1277`).
- **Reproduction:** In a **temporary copied** server tree, place sentinel runtime files, run one `P1HostTests` method, and compare file hashes. Do not run this test against the live product runtime.
- **Confidence / correction / regression test:** Confirmed by source inspection. Give every host test a temporary `RUNTIME_DIR`/paths/`Store`, with teardown that owns only that temp directory. Add a test that seeds an unrelated sentinel in product runtime, runs the test suite under isolation, and verifies unchanged bytes. The P0 is a test-side operational hazard; it does not prove the reported SIGKILL incident caused data loss.

### F02 — Immediate restart can fail to bind (P1)

- **Finding / evidence:** `LabServer.allow_reuse_address = False` (`server.py:1303-1309`); `create_server()` binds before serving (`server.py:2504-2526`), and `main()` exits on bind error (`server.py:2579-2587`). A copied product process with a private runtime exited with `[Errno 48] Address already in use` on an immediate same-port restart after authenticated HTTP traffic and SIGKILL. Five other quick restarts after roster-only traffic succeeded.
- **Failure mechanism / impact:** The old PID may be gone while the OS still declines an immediate bind to the same address. There is no running replacement; devices time out and stay in connecting/retry. A port held by another process causes the same startup error through a different mechanism.
- **Reproduction:** Own a temporary copied server on a reserved localhost port; perform health, roster, login, and inbox requests; SIGKILL only that PID; start a new copy on the same port immediately. Capture bind stderr and both PIDs. Repeat a bounded number of times because the behavior is timing dependent.
- **Confidence / correction / regression test:** Confirmed occurrence and code path; field attribution is a hypothesis. Use an explicit, documented listener reuse policy appropriate to this host, close the listener on graceful exit, and fail loudly on an owner conflict. Process integration test: repeated same-port SIGINT/SIGTERM/SIGKILL restarts with active connections, proving the replacement becomes reachable and no prior owner remains.

### F03 — Shutdown has no owned cleanup protocol (P1)

- **Finding / evidence:** `main()` only closes `httpd.log` in `finally`, never calls `httpd.server_close()` or records initiation/completion (`server.py:2588-2593`); request handlers are daemon threads (`server.py:1303-1305`). There is no signal registration in the product server.
- **Failure mechanism / impact:** SIGTERM terminates without an application cleanup phase. SIGINT exits by exception; in-flight writes can be interrupted and there is no bounded drain/cancel/accounting. A normally returning embedded `main()` can retain the bound socket until object destruction. This does **not** establish why the reported Python PID remained alive: the isolated owned processes exited for all three signals.
- **Reproduction:** In a scratch runtime, send SIGINT/SIGTERM during a bounded active upload or slow request; check exit status, listener closure, durable state and request outcome. Record exact child PID, not process name.
- **Confidence / correction / regression test:** Confirmed omission. Define one owner for listener/log/active handlers, perform bounded shutdown and `server_close()` in `finally`, and record an outcome even when startup is partial. Test actual `main()` process exit and owned resource cleanup on repeated signals and active work.

### F04 — Health is not functional readiness (P1)

- **Finding / evidence:** `/health` always emits `ready` after bind without inspecting runtime (`server.py:1717-1729`). `_load_tombstones()` catches malformed/unreadable durable JSON and returns `[]` silently (`server.py:371-381`); invalid open-upload rows are skipped (`server.py:720-792`); accepted delivery with missing audio silently returns without publication (`server.py:1248-1263`). Conversely malformed PIN or last-viewed state aborts during import (`server.py:471-477,486-538,894-983`).
- **Failure mechanism / impact:** The listener can answer health and hangout while previously accepted messages are absent; operators may mistake this for full recovery. Other state errors stop startup entirely. There is no single readiness inventory showing which capability was restored.
- **Reproduction:** Only in a scratch runtime, corrupt tombstone JSON or remove accepted audio, restart, compare `/health`, `/v2/hangout`, and affected recipient inbox/media; separately corrupt PIN JSON and capture startup failure.
- **Confidence / correction / regression test:** Confirmed source mechanism. Keep liveness distinct from readiness; validate durable-state restoration before declaring relevant capabilities ready, and report bounded, non-sensitive degradation/fail-closed reasons. Test both healthy and damaged scratch persisted states through the production process entrypoint.

### F05 — Requests can wait without server-side bounds and block shared state (P1)

- **Finding / evidence:** Per-connection `ThreadingHTTPServer` has no admission cap (`server.py:1303-1317`). `_read_json()` calls `rfile.read(Content-Length)` without a socket or total request deadline (`server.py:1601-1615`). The single `Store.lock` covers fsync/replace during PIN, refresh, complete, and other mutations (`server.py:1936-2005,2080-2105,2203-2273`). Some replay/error branches send HTTP responses **while holding that lock** (`server.py:1936-1952,2121-2145,2203-2214`).
- **Failure mechanism / impact:** Slow/incomplete clients can occupy arbitrary daemon threads. Slow storage serializes session, inbox, and send work under one mutex; a client that stops reading a response from a lock-held replay branch can hold the mutex for its network stall. Hangout itself has no `Store.lock`, but process/thread/resource exhaustion can still make it unavailable. A hung call has no guaranteed server-side terminal outcome.
- **Reproduction:** Scratch process only: open bounded incomplete POSTs, add one stalled response reader, then time authenticated inbox/session calls and roster/health; inspect owned thread count and recovery after closing clients. Never target a live server.
- **Confidence / correction / regression test:** Strongly supported by code; no measured saturation threshold or field causal link. Add bounded connection/request deadlines and admission, and move network writes outside the state lock using an immutable response snapshot. Integration test injects slow reads/storage and asserts bounded request results plus responsive independent routes.

### F06 — Restart does not guarantee restoration of an otherwise valid session (P2)

- **Finding / evidence:** Live sessions expire 180 seconds after last refresh (`server.py:51-54,1649-1655`), but `Store._restore_persisted_pin_sessions_locked()` restores only records whose **original acceptance** is within 60 seconds (`server.py:920-951`). Refresh persists a later `session_last_refresh_at` but does not extend `accepted_at` (`server.py:2088-2104`). The contract explicitly permits a restart that drops a credential (`docs/product/communication-contracts/user-scoped-envelope.md:48-54`).
- **Failure mechanism / impact:** After more than a minute of signed-in use, a restart can reject a session still valid immediately before the crash. The device remains visibly signed in, its refresh fails, and signed-in inbox/send work waits or fails until mute and PIN sign-in. Roster retrieval remains independent.
- **Reproduction:** Scratch runtime: login, refresh, age only the persisted acceptance time past 60 seconds while keeping last refresh within 180, then restart the actual process; compare `/v2/hangout` with desk Bearer and `/v2/inbox` with the prior session.
- **Confidence / correction / regression test:** Confirmed behavior, not a contradiction of the current product contract. In an isolated three-process probe with one private runtime, a recent token worked after restart (`/v2/inbox` 200); after moving only its persisted acceptance time back 61 seconds, the next process still served `/v2/hangout` (200) but rejected that token (401). Choose and document whether to preserve all still-valid sessions or deliberately invalidate them at restart with an observable reauthentication path. A process-level regression must cover both sides of the 60/180-second boundary and the device's accepted failed-refresh behavior.

### F07 — Runtime files have no cross-process owner (P2)

- **Finding / evidence:** All instances use fixed `RUNTIME_DIR` paths (`server.py:206-208,366-368,622-643`), while synchronization is only `threading.Lock()` in each process (`server.py:894-903`). The port check prevents identical-address listeners, but `--port` permits a second instance on another port (`server.py:2561-2563`).
- **Failure mechanism / impact:** Two processes can both load an old state snapshot then replace the same PIN/tombstone/last-viewed file, losing one process's update or returning incompatible sessions. This requires accidental concurrent deployment; it has not been observed in the field.
- **Reproduction:** Two owned scratch processes with one scratch runtime and different ports, concurrent distinct logins/acceptances, then restart and compare both results. Bound the run and preserve evidence.
- **Confidence / correction / regression test:** Strongly supported race; outcome not yet reproduced. Enforce one runtime owner with a process lock or reject simultaneous use; test second-process refusal and preservation of the first process's state.

### F08 — PIN replay state grows without pruning (P2)

- **Finding / evidence:** Every login operation copies and persists the entire `pin_records` map (`server.py:1936-2005,541-556`); load restores all records (`server.py:486-538`). The 60-second age check gates replay/restoration but does not delete old entries (`server.py:920-951,1943-1945`). Runtime reset reloads the same persisted records (`server.py:953-963`).
- **Failure mechanism / impact:** Disk file, startup parse time, and lock-held rewrite time grow with historical PIN submissions; a health/roster response may remain green while login degrades. PIN result records include session credential material, increasing retained secret exposure.
- **Reproduction:** Scratch runtime with a bounded set of expired records, measure file size and one login write before/after restart; inspect whether old rows remain.
- **Confidence / correction / regression test:** Confirmed by code. Prune expired replay data only after preserving the intended `operation_expired` semantics for old keys; bound retained records and startup time. Test expiry/replay/invalidate across restart and a fixed storage budget.

### F09 — Operators cannot reconstruct full lifecycle/recovery from logs (P2)

- **Finding / evidence:** Handler telemetry is emitted only in `finish()` and suppresses unstructured access logs (`server.py:1334-1440`); startup/shutdown and restoration have no corresponding events (`server.py:983,1290,2558-2593`). `_emit_telemetry()` swallows its own exceptions, and `_write_telemetry_line()` suppresses `OSError` (`server.py:1363-1433`). Optional file logging is bounded to 64 KiB and truncates (`scripts/server_log.py:27-68`); no `--log` is supplied by `make server` (`Makefile:141-143`).
- **Failure mechanism / impact:** Failed bind, incomplete requests, crash, or missing persisted delivery may leave no durable correlated record. A status logged after `send_response()` does not prove the client received the full body (`server.py:1343-1345,1441-1456`). Request `attempt` is always `1` on the server (`server.py:1417`), so it cannot reconstruct client retry number.
- **Reproduction:** In scratch process, compare log events for successful readiness, bind failure, interrupted request, and damaged tombstones; check whether one trace links startup generation to request and shutdown.
- **Confidence / correction / regression test:** Confirmed logging gaps. Add concise, redacted lifecycle and restoration summaries (PID/run ID/start generation, bind result, count of outstanding handlers at stop), and reasoned request outcomes without PINs, Bearers, bodies, or media. Test correlation and a truncated/disconnected response path; keep log volume bounded.

## 6. Failure scenario matrix

| Scenario | Implemented outcome | Missing guarantee / investigation consequence |
|---|---|---|
| Valid start, no prior runtime | Fixture seed + deployment load; listener and roster should work (`server.py:193-208,371-381,983,2504-2526`). | No explicit readiness probe for all signed-in capabilities. |
| Missing/invalid deployment or missing dependencies | Startup fails before serving (`server.py:220-282,323-363,2579-2587`). | Device repeats connecting; startup error exists on stderr only. |
| Malformed PIN/last-viewed JSON | Import-time error can prevent bind (`server.py:471-538,894-983`). | No recovery state or structured startup reason. |
| Malformed tombstone/open-upload JSON | Skipped/empty state can still serve `/health` and roster (`server.py:371-381,720-792,1720-1740`). | Accepted delivery or resumable upload may disappear from responses without readiness failure. |
| SIGINT / Ctrl+C | Isolated owned PID exited `-2`; log `finally` executes (`server.py:2588-2593`). | No explicit listener close/drain or evidence that incident PID belonged to this server. |
| SIGTERM | Isolated owned PID exited `-15`; OS terminates it. | No application cleanup or in-flight completion. |
| SIGKILL / crash during HTTP request | Process exits immediately; new process reloads durable records. | Request's client-visible outcome may be unknown; retry identity is needed. |
| Rapid same-port restart | Sometimes binds; one authenticated-traffic probe failed with Errno 48 (F02). | No guaranteed immediate readiness; device keeps retrying. |
| Crash during login | PIN operation is atomically written before 200 (`server.py:1976-2005,384-400`). | Result before durable write is not replayable; persisted record is not an all-time session guarantee. |
| Crash during accepted send | Media files and tombstone use atomic replace/fsync; host tests inject write failures (`server.py:2252-2273`; `scripts/tests/test_product_server_hardening.py:292-428`). | Damage/corruption on later load may be silent; process kill at each commit boundary is not tested. |
| Device reboot, server remains active | Device boots in connecting and fetches roster with desk token (`client/shared/login_session/fl_product_login/session.cpp:313-332`). | Device URL/token/worker startup must be confirmed on target. |
| Temporary network loss | Model changes to Wi-Fi unavailable, then immediate roster on return; connecting backoff continues (`session.cpp:360-399,502-545`). | Overall connecting may persist indefinitely by design during continued failure. |
| Stale/missing session after restart | User-scoped routes return 401/403; refresh failure leaves device signed in (`server.py:1632-1661`; `credential_session.cpp:142-163`). | User must mute and sign in again; not causal for desk-authenticated hangout. |
| Duplicate/overlapping requests | PIN operation replay and send identity checks are serialized by mutex (`server.py:1936-2005,2121-2273`). | No interprocess serialization; slow lock-held work delays other routes. |
| Multiple devices using one pilot endpoint | Both share one desk identity and health clock (`server.py:270-315,894-903,1880-1905`). | Physical devices are not distinguishable under the current one-endpoint deployment. |
| Slow/incomplete request, slow persistence | No socket request deadline; global lock spans durable I/O (F05). | No server-side terminal guarantee or admission limit. |

## 7. Test coverage assessment and experiments

**Executed without touching product runtime:** `python3 -m unittest scripts.tests.test_product_server_startup -q` passed 3 tests. The isolated deployment and hardening suites passed 46 tests under localhost permission: `.venv/bin/python -m unittest scripts.tests.test_product_server_deployment scripts.tests.test_product_server_hardening -q`. They exercise deployment validation, HTTP roster/auth/send paths, store reconstruction, duplicate completes, storage-failure injection, and structured telemetry. Several restart tests rebuild `Store` in one interpreter and reauthenticate; that is useful state-unit evidence but does not cover process signals, bind reuse, real device reconnection, or a live session carried through an actual replacement PID (`scripts/tests/test_product_server_hardening.py:642-710`; `test_lab_p1_host.py:49-74`). The startup tests mock `os.execve`, not production process exit (`scripts/tests/test_product_server_startup.py:34-102`). Product session/journey host tests combine real localhost server threads with a compiled model, but do not run the device's ESP HTTP worker or display (`scripts/tests/test_product_session_host.py:42-62`; `test_product_journey_host.py:156-176`).

**Before the repair:** `scripts.tests.test_lab_p1_host.P1HostTests` and the full `make test` gate were not run against the populated checkout because F01 would delete product runtime files. No active service, device, or non-owned Python PID was signaled. `ps` was denied in this execution environment. A bounded copied-process probe confirmed SIGINT/SIGTERM/SIGKILL exits; another reproduced a timing-dependent post-SIGKILL bind error. A third used three actual copied processes and private persisted state to confirm 200/200 roster/session responses for a recent token, then 200/401 after aging the PIN acceptance record. The first copied restart probe failed to start because its interpreter symlink was resolved outside the venv; that harness mistake was corrected before interpreting process results.

**After the repair:** the isolated host, deployment, hardening, and lifecycle suites passed 68 tests. The lifecycle suite now exercises the real `server.py` entrypoint through three SIGINT/restart/authenticated-hangout cycles, a SIGKILL/immediate-rebind cycle, the SIGINT-inherited-from-launcher case, damaged and malformed persisted-state readiness, and an incomplete authenticated request that must terminate with a 400 while `/v2/hangout` remains usable. The full `make test` gate was not run because it includes broader lab/device tests outside this server change.

The acceptance workflow was also exercised directly: an owned process-group supervisor ran three `make server` → Ctrl+C → immediate `make server` cycles on port 8777 with a private runtime. Each cycle returned the authenticated `hangout-kitchen` roster after restart and released the listener before the next bind. GNU Make itself reported the interrupt as a signal exit; the Python child exited through its cleanup path and no listener remained.

**Recent evidence:** The checked repository's available `.log` files are lab/server fixture traces, not the two reported product-server incidents. A selective scan of recent agent session logs found prior product-server test discussion but no incident PID tree, port-owner capture, paired request log, or device serial trace. Historical client-startup review notes are not current runtime proof; current source was used to assess the roster path. No live product-server log was changed or searched for secrets.

**Missing integration gates:** (1) repeated production-entrypoint startup/shutdown with owned PID and listener release; (2) same-port restart after SIGKILL and active HTTP; (3) SIGINT process exit with an incomplete request and with a durable write in progress; (4) restart restoration/degradation from valid, absent, malformed, and stale state; (5) desk-roster retrieval and user-session rejection separately after restart; (6) stale token handling through real device worker/model/UI; (7) concurrent physical clients and bounded server thread/lock wait; (8) cleanup of owned threads, sockets, log handles, temporary files and any subprocesses. Use a copied server tree and private runtime for every process test; test the real `main()` wiring, not only a helper.

## 8. Architectural recommendations

1. **Make ownership explicit locally.** One process should own one runtime directory, listener, optional log, and active handlers. A small lifecycle wrapper around the existing HTTP server can supply bounded shutdown, socket close, bind diagnostics, and restoration status; no framework replacement is required (F02, F03, F07).
2. **Separate live, ready, and degraded.** Keep a cheap liveness probe. Determine readiness only after deployment and durable-state restoration; publish non-sensitive reasons for lost delivery/session capabilities. Do not equate a valid empty hangout, failed roster request, and damaged inbox (F04).
3. **Bound every server request.** Limit connection admission, request-body reads and output stalls. Commit shared state under the mutex, then send an immutable response outside it. Retain atomic file replacement for persisted operations (F05).
4. **State the restart session invariant.** The current contract permits credentials to disappear on restart. If kept, test the resulting signed-in-but-unusable state and make reauthentication practical without misleading roster diagnostics. If a stronger guarantee is chosen, persist all still-valid sessions and revocation with explicit expiry (F06).
5. **Preserve retry identities and diagnostic correlation.** Existing PIN/send operation IDs are useful. Add generation/route/outcome timing at lifecycle boundaries and record storage recovery decisions without logging credentials or personal/media contents (F08, F09). The device's continuing `connecting...` is intentional per contract; make the underlying failure visible to engineering.

## 9. Prioritized remediation plan

| Order | Priority | Work item and dependency | Acceptance criteria |
|---|---|---|---|
| 1 | **P0** | Isolate `P1HostTests` runtime and quarantine the destructive full-suite path until fixed (F01). | Running the documented host suite leaves pre-existing product runtime sentinels byte-identical; tests use only owned temporary directories. |
| 2 | **P1** | Capture one field incident with PID/port ownership and paired device/server traces; concurrently repair deterministic listener closure and same-port restart behavior (F02, F03). | No unexplained owned PID after Ctrl+C; repeated bounded SIGINT/SIGTERM/SIGKILL restarts accept authenticated `/v2/hangout` on the configured port or report a different live owner explicitly. |
| 3 | **P1** | Define readiness/degraded restoration outcomes and test damaged state (F04). | `/health`/readiness evidence distinguishes listener liveness from missing accepted deliveries; no silent success when required persisted state cannot be restored. |
| 4 | **P1** | Bound request admission/read/write and remove HTTP writes from `Store.lock` (F05). | Slow-client/storage integration test has finite outcomes and independent health/roster latency within an agreed budget. |
| 5 | **P2** | Enforce exclusive runtime ownership and choose documented session restart semantics (F06, F07). | Second instance refuses shared runtime; valid/stale/revoked session behavior across actual process restart matches the chosen contract and device stage. |
| 6 | **P2** | Prune PIN replay records while retaining key-expiry behavior (F08). | Fixed maximum durable size/age; old exact replay cannot reauthenticate; active replay/invalidation survive restart. |
| 7 | **P2/P3** | Add bounded lifecycle/request diagnostics and process-level failure gates (F09). | Redacted logs reconstruct startup generation, bind/readiness, request outcome, shutdown and outstanding work; no PIN/Bearer/media leakage. |

## 10. Open questions and decisive experiments

1. **Which PID actually remained after Ctrl+C?** Capture PID/PPID/process group/executable, start time, listening socket owner, and exit status before and after one signal. This distinguishes same process, another server, launcher, child, or unrelated Python.
2. **Did the replacement ever bind port 8777?** Preserve its first stderr lines and a port-owner snapshot. `Address already in use` could be TIME_WAIT/reuse policy or another listener; the scratch probe demonstrates possibility, not incident attribution.
3. **What did `connecting...` refer to on the device?** Capture screen/stage and `roster op` serial line with time and firmware hash. A signed-in inbox connecting state is different from the signed-out hangout connecting state.
4. **What exact hangout exchange failed?** From the device's network, capture endpoint URL/port (no credential), transport result, HTTP status, response byte count/schema/parse-valid flag, and the matching server route event. A 401 points to desk-token mismatch; no server event points to network/listener/startup; 200 plus invalid parse points to wire/response handling.
5. **Was durable state intact after forced termination?** Hash/count only the accepted tombstone, PIN, last-viewed, open-upload metadata, and media files before any tests or cleanup. Compare with a scratch-copy restoration. Do not run `P1HostTests` on the populated runtime.
6. **Can the device's worker or UI loop stop servicing roster jobs?** Run a target capture across server outage and recovery with model operation IDs, worker queue depth, task liveness, and stage updates. Host model timeouts alone do not establish physical task progress.
7. **How does the server behave under concurrent clients and slow I/O?** Run a bounded scratch load with two client connections, one incomplete request and one injected slow persistence operation; measure handler count, lock wait and roster/health latency before choosing limits.

The two reported failures should remain separate until these captures establish a shared causal chain.

## 11. Follow-up implementation status (2026-10-24)

This section records the narrowly scoped changes made after the investigation. The findings above remain the evidence base; this is not a claim that either field incident has been attributed.

### Repaired

- **F01 (P0):** `scripts/tests/test_lab_p1_host.py` now owns a module-scoped temporary runtime and resets only that directory. Process-level tests use `FAMILY_LINK_RUNTIME_DIR` to isolate persisted state. The host suite no longer deletes `server/device/runtime`.
- **F02 + F03 (P1):** `LabServer` enables address reuse. The production entrypoint restores Python's SIGINT handler when a launcher inherited `SIGINT=SIG_IGN`, routes SIGTERM through the same cleanup path as Ctrl+C, and closes the listener and optional log in `finally`. `scripts/tests/test_product_server_lifecycle.py` repeats three real-process Ctrl+C/restart cycles and authenticated hangout retrieval.
- **F04 (P1):** `/health` is explicitly liveness (`status: live`); `/ready` returns 200 only when persisted delivery restoration has no recorded recovery issue and 503 with non-sensitive issue codes when degraded. Missing accepted media, failed legacy-media migration, malformed tombstones, and invalid tombstone rows are surfaced.
- **F05 (targeted subset):** accepted HTTP connections have a five-second socket deadline, invalid content lengths are rejected safely, and an actual process test proves an incomplete authenticated request terminates while the hangout route remains available.

### Investigated but not fully repaired

- **F02/F03 incident attribution and full drain:** the source-level listener and signal cleanup gaps are repaired and the owned-process regression passes, but no field PID/port-owner evidence exists to identify the Python process from Failure A. Signals arriving before listener construction and graceful draining of active request writes remain outside this targeted correction.
- **F05 concurrency:** the bounded request correction addresses the demonstrated incomplete-body mechanism. Unbounded connection admission, storage work under `Store.lock`, and network writes under that lock remain in the source and were not broadened into a rewrite.

### Remain open by scope

F06 (restart session semantics), F07 (cross-process runtime ownership), F08 (PIN replay pruning), and F09 (lifecycle/recovery observability) were preserved as findings and recommendations only. The device's unbounded overall `connecting...` state remains a contract-level follow-up, not an implementation change in this work.
