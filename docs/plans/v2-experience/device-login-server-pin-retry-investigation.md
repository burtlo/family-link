# Server-side PIN rejection and retry investigation

**Date:** 2026-10-24 (America/Chicago)  
**Scope:** `server/device/` production server, isolated server HTTP tests, and read-only comparison with the device login contract/client interpretation.  
**Production code changes:** none.  
**Test change:** one isolated server regression in `scripts/tests/test_product_server_hardening.py`.

## Conclusion

The server is not responsible for the reported inability to try another PIN. The implemented `POST /v2/session/login` path returns the accepted typed response for an incorrect PIN, does not create a user session, and remains usable for later independent login operations. A bounded real-HTTP test against the server demonstrated five independent wrong-PIN requests followed by a correct PIN request and authenticated inbox access.

The server also correctly rejects changing the PIN under an already-used operation ID with HTTP 409 `identity_conflict`. That is accepted idempotency semantics, not a server lockout. A client that submits a corrected PIN under the old operation ID would be expected to remain in its transport/error handling path; the client must allocate a new operation ID for a new PIN submission. Client firmware/shared code was inspected only for this contract comparison and was not modified in this scope.

## Server request path

1. `Handler.do_POST()` routes `/v2/session/login` to the desk-credential login handler (`server/device/main/server.py:1988-2077`).
2. `_desk_token()` requires the configured endpoint Bearer and binds telemetry to a non-secret endpoint hash (`server.py:1672-1685`). Missing or wrong credentials return HTTP 401 `fl.error/2` `unauthenticated`.
3. `_read_json(4096)` parses a bounded JSON object (`server.py:1650-1670`). Missing/unsupported schema returns HTTP 400 `unsupported_schema`; invalid operation ID, user ID, or PIN returns HTTP 400 `invalid_request` (`server.py:1993-2001,1752-1765`).
4. The operation key is endpoint binding plus UUID. The request fingerprint includes schema, operation ID, user ID, and PIN (`server.py:1738-1750,2002-2004`).
5. Under `STORE.lock`, an existing exact request is replayed for the retention window; a different request under that operation ID returns HTTP 409 `identity_conflict`; an expired record returns HTTP 410 `operation_expired` (`server.py:2005-2021`).
6. For a new request, the server distinguishes unknown user, wrong PIN, and authenticated PIN (`server.py:2022-2055`):

   - unknown user: HTTP 200, `fl.session-login-result/2`, `outcome: unknown_user`;
   - wrong PIN: HTTP 200, `fl.session-login-result/2`, `outcome: wrong_pin`, no `session` field;
   - correct PIN: HTTP 200, `fl.session-login-result/2`, `outcome: authenticated`, selected `user_id`, and a new server session.

7. The result is persisted before the HTTP 200 response. If persistence fails, the server removes any newly issued session and returns HTTP 500 `storage_unavailable` (`server.py:2056-2074`).
8. The server records redacted telemetry after the response (`server.py:2074-2076`). Existing logs from the reproduction show `outcome: wrong_pin`, status 200, and no PIN value.

## Contract comparison

The accepted login contract requires:

- HTTP 200 for `authenticated`, `wrong_pin`, and `unknown_user`;
- typed `outcome` in `fl.session-login-result/2`;
- no session for a wrong PIN;
- HTTP 409 `identity_conflict` when an operation ID is reused with different request identity;
- a new operation ID for a new PIN submission;
- transport, malformed, 409, and 410 outcomes to be handled as connecting/error states, never as wrong PIN.

Evidence: `docs/product/communication-contracts/endpoint-health-and-login.md:185-211,274-291`; `docs/product/experiences/endpoint-login.md:168,233,333`.

The read-only client classifier agrees with these server responses: it recognizes HTTP 200 `wrong_pin` as a credential outcome and maps malformed/non-200/transport responses separately (`client/shared/login_session/fl_product_login/p1_wire.cpp:293-350`). The client’s operation-ID ownership and UI retry behavior remain outside this server-only repair scope.

## Reproduction evidence

The new test `ProductServerHardeningTests.test_wrong_pin_retries_and_new_operation_can_authenticate()` uses `_running_server()`, which binds a real localhost HTTP server and redirects all mutable runtime paths to a temporary directory (`scripts/tests/test_lab_p1_host.py:23-40,70-98`; `scripts/tests/test_product_server_hardening.py:17-33`). It performs independent HTTP requests as follows:

| Request | Expected and observed result |
|---|---|
| Five unique operation IDs, selected user, incorrect PIN | HTTP 200; schema `fl.session-login-result/2`; operation ID echoed; `outcome: wrong_pin`; no `session` field |
| Inspect server runtime state | `STORE.sessions == {}` after all rejected attempts |
| Reuse the last wrong-PIN operation ID with the correct PIN | HTTP 409; `fl.error/2`; `code: identity_conflict` |
| Correct PIN with a new operation ID | HTTP 200; `outcome: authenticated`; selected `user_id`; session token present |
| Authenticated `GET /v2/inbox` | HTTP 200; `fl.inbox-page/2` |

The isolated test passed against the real handler. The full isolated server suites also passed: 59 tests in `scripts.tests.test_product_server_hardening` and `scripts.tests.test_lab_p1_host`.

## Findings

### S01 — No confirmed server defect in incorrect-PIN retry behavior

- **Evidence:** `server.py:2038-2043` returns a typed wrong-PIN result; `server.py:2056-2074` persists it without creating a session; the new real-HTTP regression passes five wrong attempts and a later correct attempt.
- **Failure mechanism:** none found on the server for independent operation IDs.
- **Impact:** The server permits the expected retry sequence. It does not explain a device that cannot accept another PIN when the client sends a new operation ID.
- **Confidence:** Confirmed server behavior.
- **Correction:** none; changing response status or bypassing operation identity would violate the accepted contract.
- **Regression:** `test_wrong_pin_retries_and_new_operation_can_authenticate`.

### S02 — Reusing an operation ID with a corrected PIN is intentionally refused

- **Evidence:** fingerprint mismatch returns HTTP 409 `identity_conflict` (`server.py:2007-2011`); the regression demonstrates this after a wrong PIN.
- **Failure mechanism:** A client that reuses the old operation ID for a new PIN receives a refusal and must start a new operation.
- **Impact:** This could appear as “another PIN is not permitted” if the client incorrectly treats a new attempt as a retry of the old operation. It is not a server defect.
- **Confidence:** Confirmed contract behavior; client attribution is unresolved without a device trace containing operation IDs.
- **Correction:** none on the server.
- **Needed evidence:** device log of operation ID per PIN submission and HTTP status/body outcome, with PINs and credentials redacted.

### S03 — Server restart/session persistence is not on the signed-out hangout path

- **Evidence:** `GET /v2/hangout` uses the desk Bearer and does not consult `STORE.sessions`; user session state is used by user-scoped routes (`server.py` hangout route and `1687-1710`).
- **Impact:** A lost user session after restart cannot by itself prevent the initial hangout roster from being retrieved. A server bind/readiness/network failure remains a separate hypothesis.
- **Confidence:** Confirmed separation of server paths.
- **Correction:** none in this investigation.

## What remains unresolved

The server evidence cannot establish why the physical device refused another PIN. The decisive missing fields are:

- operation ID for each PIN submission;
- HTTP status and whether the response was received;
- parsed schema/outcome or error code;
- whether the second attempt used a new operation ID;
- client-side state immediately before and after the response;
- server restart generation and matching server telemetry.

If a device sends a new operation ID and receives HTTP 200 `wrong_pin`, the remaining blocker is client-side state/UI handling. If it reuses the old operation ID and receives HTTP 409, the client violates the operation-ID contract. If no server event exists, investigate transport, worker scheduling, or server reachability. No speculative server workaround is justified.

## Scope and verification statement

Production changes were confined to no files: `server/device/main/server.py` is unchanged. Client firmware, shared session models, device UI, client tests, and client build configuration are unchanged in this turn. The only code change is the isolated server regression test in `scripts/tests/test_product_server_hardening.py`.

