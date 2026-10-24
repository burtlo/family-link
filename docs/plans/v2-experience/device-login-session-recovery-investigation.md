# Family Link V2 device login, session, and recovery investigation

**Date:** 2026-10-24 (America/Chicago)  
**Scope:** ESP product login bridge, HTTP worker, shared login/session model, P1 wire classifier, server login/roster contract, and host tests.  
**Investigation status:** historical client-side investigation; superseded for implementation by the server-only investigation in `device-login-server-pin-retry-investigation.md`. The client changes previously described here were reverted to preserve the mandatory server-only scope.

## Executive summary

The observed sequence is explained by a confirmed client-side state-machine defect when PIN authentication fails at the transport level. After four one-second PIN attempts, `SessionWriter::fail_pin_transport()` changed the stage to `connecting` and closed the PIN operation, but did not arm the existing connecting backoff. `advance_to()` starts a roster request from `connecting` only when that backoff is armed. Consequently, the device could render `connecting...` forever with no roster request in flight and no future retry event.

An ordinary rejected PIN is a different path. The server returns HTTP 200 with `outcome: wrong_pin`; the classifier produces `kPinOutcomeWrong`, and the model returns to the PIN surface (or cooldown after repeated credential failures). It does not enter the transport-exhaustion path. The physical incident therefore cannot be attributed to a wrong credential from the available evidence; it is consistent with a timeout, unavailable server, non-200/malformed response, or an authenticated response missing its session token.

The smallest correction now arms the existing 1/2/4/30-second connecting backoff at the transport-exhaustion transition. A model regression drives all four timeout attempts and proves that a roster request is scheduled after the first connecting delay. The ESP HTTP worker now records redacted PIN classification telemetry (operation ID, status, perform/parse flags, numeric outcome) without PINs, Bearer tokens, user IDs, or bodies.

No server code was changed. The server's hangout route uses the desk credential and does not require a user session, so server-session loss after restart is a separate failure path. The current evidence does not prove whether the physical device encountered this exact defect, a stalled ESP worker/UI timer, a restart/bind/readiness failure, or a combination. No physical device or ESP target run was available for this turn.

The prior server audit remains the established evidence for server findings F06–F09 (restart session semantics, cross-process runtime ownership, PIN replay retention, and lifecycle/recovery observability). Those items were not automatically implemented here; they remain dependencies for a stronger end-to-end restart guarantee.

## Reconstructed production flow

```text
boot / Wi-Fi return
  -> product_login_bridge::on_login_timer()
  -> SessionWriter::advance_to(now)
  -> product_login_http_service()
  -> GET /v2/hangout from the ESP HTTP worker
  -> classify roster body and deliver completion
  -> roster stage / selectable cards

select user + four digits
  -> SessionWriter::start_pin_submission()
  -> product_login_http_service() queues OP-PIN
  -> worker POST /v2/session/login
  -> classify response and deliver PinResult
  -> success: credential/session state and signed-in UI
  -> wrong_pin: wrong-PIN copy, then PIN entry/cooldown
  -> transport: 1/2/4-second retry delays, four attempts total
  -> exhausted transport: connecting stage, roster recovery backoff
```

The bridge runs the session clock before servicing the worker in `client/device/main/product_login_bridge.cpp:697-720`. This means the model's request deadlines continue to advance on the normal LVGL timer even when the worker has not delivered a result. The worker queues at most two jobs and performs blocking `esp_http_client_perform()` calls (`client/device/main/product_login_http_worker.cpp:161-190,266-337`). Results are drained by the same LVGL service path (`product_login_http_worker.cpp:395-443`).

The server contract distinguishes the operations:

- `OP-ROSTER` is `GET /v2/hangout` with the desk Bearer and is required for signed-out readiness.
- `OP-PIN` is `POST /v2/session/login`; retries reuse one operation ID.
- A valid `wrong_pin` result is a credential failure, not a transport failure.
- A PIN timeout has four total attempts; exhausted attempts intentionally show `connecting` and must recover through the roster retry policy.

Evidence: `docs/product/communication-contracts/endpoint-health-and-login.md:61-95, COM-003–COM-008`; `client/shared/login_session/fl_product_login/p1_wire.cpp:293-350`; `client/device/main/product_login_bridge.cpp:462-545,609-695`.

## State and request transitions

### Successful authentication

`start_pin_submission()` freezes the selected user/session generation, creates an operation ID, arms a one-second deadline, and exposes `checking...` (`pin_session.cpp:373-395`). A classified authenticated response with a valid session token signs in, issues the local credential, closes the operation as `success`, and causes the bridge to enter the signed-in experience (`pin_session.cpp:426-455`; `product_login_bridge.cpp:619-631`).

### Ordinary incorrect PIN

The server's HTTP 200 `wrong_pin` envelope is classified as `kPinOutcomeWrong` (`p1_wire.cpp:341-343`). The model wipes the PIN, records a credential failure, shows `wrong pin`, and later reopens entry; after the wrong-count limit it enters the bounded cooldown (`pin_session.cpp:462-480`). This path does not call `fail_pin_transport()`.

### Transport, timeout, malformed, and incomplete authentication

The classifier defaults to `kPinOutcomeTransport` for an unsuccessful perform, missing body, invalid schema/JSON, non-200 result envelopes, or an unknown outcome (`p1_wire.cpp:308-350`). Each result or model deadline invokes `fail_pin_transport()`. Attempts below the maximum schedule the next PIN retry (`pin_session.cpp:339-354`).

Before the correction, the final branch did this:

1. Wipe PIN state and set `pin_phase` to idle.
2. Set stage to `kStageConnecting` and clear selectable cards.
3. Close the PIN operation as `timed_out`/`transport`.
4. **Do not arm `connecting_retry_armed_`.**

`advance_to()` only starts a connecting roster request when `connecting_retry_armed_` is true (`session.cpp:874-925`). Therefore the old state had no in-flight operation, no pending timer, and no path to `begin_roster()`. The visible bridge renders `connecting...` for `kStageConnecting` (`product_login_bridge.cpp:659-694`), so the UI accurately reflected a state that could never recover.

The correction at `client/shared/login_session/fl_product_login/pin_session.cpp:366-371` calls `arm_connecting_backoff()` in the non-muted, non-Wi-Fi-unavailable branch. The existing backoff and roster ownership remain unchanged.

## Incident timeline and reproduction

### Reported physical timeline

1. Device booted and reached roster/PIN entry, proving that the initial worker, desk credential, server route, and accepted roster response were functional at that time.
2. PIN submission failed.
3. Device displayed `connecting...`.
4. Server was stopped and restarted.
5. Device remained in `connecting...`.

The missing field evidence is the PIN HTTP status, perform result, parse result, operation ID, worker task state, and server-side route event. The timeline alone cannot distinguish a normal wrong PIN from a transport failure, nor can it show whether the device sent any roster retry after restart.

### Deterministic host reproduction of the confirmed defect

The production shared model was booted with a valid roster, a user was selected, four digits were submitted, and all four PIN deadlines were allowed to expire. Before the correction, the final deadline left:

- `stage == kStageConnecting`;
- `pin_operation_id == 0` and no PIN in flight;
- `roster_inflight == false`;
- `connecting_backoff_index == 0` and no retry event.

After the correction, the same sequence leaves the first connecting backoff armed (`connecting_backoff_index == 1`); advancing one second creates a roster operation. This is covered by `test_pin_transport_exhaustion_backoff()` in `client/shared/login_session/tests/product_login_model_test.cpp:917-949`.

This reproduces the state-machine failure, not the entire ESP/network/server stack. No physical flash, reset, or live-device disruption was performed.

## Findings register

| ID | Finding | Evidence and mechanism | Confidence | Impact | Status |
|---|---|---|---|---|---|
| D01 | PIN transport exhaustion had no recovery event | `pin_session.cpp:356-371` transitioned to `connecting` without `arm_connecting_backoff()`; `session.cpp:885-925` requires the armed event to call `begin_roster()` | **Confirmed** | Device could remain at `connecting...` indefinitely after four timeout/transport attempts, even though no operation was permanently in flight | **Repaired** |
| D02 | Wrong-PIN and transport outcomes are distinct, but the incident had no outcome telemetry | `p1_wire.cpp:308-350` maps wrong PIN to a typed outcome and transport/malformed/non-200 to transport; old worker did not log PIN classification | **Confirmed gap; incident attribution unresolved** | A physical trace could not distinguish credential rejection from server/network failure | **Instrumented** |
| D03 | Worker result delivery can block on a full result queue | `product_login_http_worker.cpp:334-336` uses `xQueueSend(..., portMAX_DELAY)`; only the LVGL service drains results at `:395-443` | **Strongly supported hypothesis** | A stalled LVGL timer or accumulated results can strand the worker and completions; not reproduced or tied to this incident | Open; not changed |
| D04 | Worker stop deletes task/queues without a quiescence acknowledgement | `product_login_http_worker.cpp:372-393` calls `vTaskDeleteWithCaps()` and deletes queues; no stop handshake exists | **Confirmed lifecycle weakness; incident attribution unresolved** | A stop/start race could lose a completion or leave stale ownership; no reproduction tied to this incident | Open; not changed |
| D05 | Server roster retrieval is independent of a user session | `server/device/main/server.py` hangout route and the contract use the desk credential; user-scoped session routes are separate | **Confirmed** | A post-restart session loss cannot by itself explain signed-out hangout failure; server bind/readiness/network evidence is still required | No client change |

## Recovery and concurrency assessment

- **Server restart while signed out:** the device should keep its desk credential, issue a roster request, and use the connecting backoff when the server is unavailable. The repaired PIN exhaustion path now enters this same recovery mechanism.
- **Server restart after a successful PIN:** user-scoped server credentials may be lost according to the existing contract. A failed refresh leaves the device signed in; it is not a signed-out hangout dependency. This remains a server/session contract concern, not a proven cause here.
- **Server restart during a PIN request:** the worker may receive a timeout, non-200, malformed body, or no result. The model retries the same PIN operation ID four times. If all fail, the corrected backoff schedules roster recovery. If a late completion arrives after the operation is closed, identity/terminal fencing rejects it (`pin_session.cpp:397-425`).
- **Wi-Fi loss:** `set_wifi()` cancels roster work and starts a fresh roster request on return (`session.cpp:360-399`). This path already has a terminal cancellation/restart transition.
- **Worker/UI liveness:** the bridge's timer owns both model advancement and result draining. The queue-full and task-stop issues above remain candidates requiring target instrumentation, but neither is established as the physical root cause.

## Changes made

1. `client/shared/login_session/fl_product_login/pin_session.cpp:366-371`: arm the existing connecting backoff after final transport exhaustion.
2. `client/shared/login_session/tests/product_login_model_test.cpp:917-949,1318-1320,1383`: add and register the four-attempt recovery regression.
3. `scripts/tests/test_product_login_model.py:97-98`: invoke the regression through the production model compilation harness.
4. `client/device/main/product_login_http_worker.cpp:214-226`: log only operation ID, HTTP status, perform flag, parse flag, and numeric classification outcome for PIN completions. No PIN, Bearer, user ID, or response body is emitted.

No server code, session architecture, HTTP queue policy, or UI architecture was rewritten.

## Test coverage and execution

Passed in this checkout:

- `python3 -m unittest scripts.tests.test_product_login_model -q` — 24 tests.
- `python3 -m unittest scripts.tests.test_device_guardrails -q` — 21 tests.
- `python3 -m unittest scripts.tests.test_product_session_model -q` — 12 tests.
- `git diff --check` — clean.

The new test compiles the actual shared production session, wire parser, credential/session, poller, worker, and test sources; it is not a duplicate helper model. It does not compile ESP-IDF worker code or exercise an LVGL task.

The L2 host suites `test_product_login_host` and `test_product_journey_host` could not start because this checkout's interpreter lacks PyYAML (`ModuleNotFoundError: No module named 'yaml'` while loading the real server deployment). No dependency installation or live service was attempted.

Missing regression coverage remains for worker queue-full behavior, task stop/restart quiescence, real ESP timeout delivery, malformed/mismatched PIN identity fields, and a process-level server restart paired with an actual device worker. Those are follow-up tests, not evidence that D01 remains unfixed.

## Confirmed causes versus unresolved hypotheses

**Confirmed:** the pre-change shared client model could enter a terminal-looking `connecting` state with no scheduled roster recovery after final PIN transport exhaustion. The code change closes that dead end, and the host regression passes.

**Strongly supported but unproven:** a physical device can also become stuck if the LVGL service stops draining a full result queue or if a worker stop races with result delivery. These require target logs and a bounded fault-injection test.

**Unresolved:** whether the reported device saw transport exhaustion, a genuine wrong PIN, a server bind/readiness failure, a missing desk credential, an ESP worker stall, or multiple failures. The new log line is intended to make the next controlled reproduction decisive.

## Required physical verification

The final acceptance test remains open until a real device is exercised with a private/test account:

1. Capture firmware build identity and server restart generation.
2. Reach PIN entry, submit an intentionally incorrect PIN, and verify the device returns to the accepted wrong-PIN outcome rather than entering transport recovery.
3. During a valid PIN submission, interrupt the server or inject a bounded timeout; collect the new `pin op=... http=... perform=... parse=... outcome=...` line and the matching server event.
4. Allow all four transport attempts to exhaust; verify the device enters `connecting...`, then emits a roster request after the 1-second backoff.
5. Restart the server and verify an accepted roster response returns the device to the roster/PIN experience. Confirm no operation remains indefinitely in flight.

Until this target run is complete, the software root cause is repaired and regression-tested, but the physical-device incident is not declared fully resolved.

