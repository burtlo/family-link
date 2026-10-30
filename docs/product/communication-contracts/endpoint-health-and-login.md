# Endpoint health and login communication contract

| Field | Value |
|---|---|
| ID | `COM-ENDPOINT-HEALTH-LOGIN` |
| Revision | 0.14 |
| Status | Interview draft; product meanings confirmed (login `DEC-072`); login clocks, session token, HTTP map, the five login routes, desk-credential installation, and invalidate spacing accepted as engineering wire 2026-10-22 |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | No approval claimed; no implementation authorization implied |
| Participants | Powered endpoint, server, and the endpoint's login session |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) for session credential shape after PIN success |

## 1. Scope and authority

This contract gathers the exchanges exposed by [endpoint login](../experiences/endpoint-login.md): endpoint-health heartbeat, signed-out readiness and roster refresh, PIN verification, and best-effort session invalidation after mute. It does not define signed-in message activity or user-presence policy. Its purpose is to make the two sides interoperable without treating a historical demo as a v2 protocol.

| Source | Authority here |
|---|---|
| [`EXP-LOGIN`](../experiences/endpoint-login.md) `DEC-029`–`DEC-032`, `DEC-038`–`DEC-039`, `DEC-054`–`DEC-057`, `DEC-066`–`DEC-071`, `EXP-004`, `EXP-010`–`EXP-011`, `EXP-014`, `EXP-022`–`EXP-024`, `EXP-029`, `EXP-035`, `EXP-040`–`EXP-043`, `EXP-050`–`EXP-055` | Owner-decided behavior and timing, including session issuance, the 3-minute credential bound, health-beat meaning, PIN-key expiry/mismatch, and PIN-transport exhaust recovery |
| [Client application coding standards](../../standards/client-application-coding-standards.md) §§3–7 | Normative client concurrency and operation constraints |
| [Operational-contract concept guide](../../v1-assessments/operational-contract-shape.md) | Supporting structure, not v2 wire authority |
| [Authentication and connectivity assessment](../../v1-assessments/authentication-connectivity.md) | V1 as-built login and hangout evidence, not v2 policy |
| [V1 communication inventory](../../v1-assessments/communication-contract-inventory.md) | V1 session/header authority evidence |

`MUST` below restates a decided experience requirement or applicable standard. `PROPOSED` is an engineering protocol choice awaiting review. `OPEN` is a contract dependency, not implementation discretion. Example values must not be shipped merely because they appear in this draft.

## 1a. V1 as-built capture

These are historical request/response facts extracted from the v1 product hangout/login path and from POC heartbeat demos. They are not v2 policy. Keep this table separate from any proposed v2 schema.

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Hangout probe | `GET /v1/hangout` with endpoint Bearer. HTTP 200 body `{id, name, users:[{id, name, profile:{avatar_slot, accent_hex, autoplay_new}}]}`. Firmware required a string `id`; missing `name` fell back to `id`; `profile` was optional. A 200 with an empty `users` array was treated as failure and the previous cached roster was kept. Probe cadence was 5 s with a 2.5 s timeout. | **Retain** hangout membership as the signed-out readiness document and `id`/`name` as user identity. **Replace** empty-array-as-failure (v2 empty hangout is valid, `EXP-020`). **Replace** showing a cached roster as ready. **Adapt** profile to the separate profile contract's avatar identity; do not treat v1 `avatar_slot` as a v2 ID. Add a roster revision. |
| PIN login | `POST /v1/session/login` JSON `{user_id, pin}` with endpoint Bearer. Success HTTP 200 returned the inbox envelope plus `ok`, `name`, and `pin_reset`, and issued **no** session token. Unknown user was HTTP 404 `{error:"unknown user"}`. Wrong PIN was HTTP 401 `{ok:false, error:"wrong pin"}`. No operation key. PIN cooldown was endpoint-local. Firmware could treat a non-200 while “online” as wrong PIN. | **Retain** typed unknown-user and wrong-PIN outcomes and verifying PIN against the selected user. **Replace** returning inbox on login, caller-supplied later `X-User-Id` authority, and classifying transport/5xx as wrong PIN. **Adapt** by adding an operation key, a server-issued session on success, and a mute invalidate. Cooldown stays endpoint-local (`DEC-033`). |
| Product heartbeat | The v1 product login path had no device-health beat. Signed-out truth was the hangout probe. | **Replace** with the independent 60-second health operation already decided in `EXP-035`. Roster is not a health substitute (`COM-001`). |
| Heartbeat demos | POC `POST /v1/heartbeat`: optional `{uptime_s, rssi}` or `{available}` mute-as-open, sometimes `{boot_id, logs[]}`. Ack included `ok`, `peer_online` or self/peer presence, and `server_time`. Demo stale window was 10 s. | **Replace** peer-online, user-presence, and log batching. **Retain** only the idea of an authenticated beat plus an acknowledgement and a last-successful-beat clock. Mute latch, if present, is device state, not “user available.” |

The routes in §4c are accepted engineering wire. Other paths are not.

## 2. Vocabulary and state ownership

| Term | Meaning and owner |
|---|---|
| Device health | Whether the server has recent evidence from the powered endpoint. The server derives current/stale state from health beats. This does not state whether a person is online (`DEC-067`). |
| Connectivity | The endpoint's observed Wi-Fi and server reachability; local Wi-Fi absence and server failure are distinct. |
| Readiness | HTTP 200 with a fully valid roster body as defined in §4. Empty `users` is valid (`EXP-020`). |
| Roster | Server-owned hangout membership. Endpoint-local login recency only orders received users and lasts for the current boot. |
| Login operation | One immutable selected-user/PIN submission with one unique operation key; transport retries are the same operation. |
| Login session | Endpoint state that can accept a login result. Mute ends it and late results cannot restore it. |
| Server session | Server-issued credential created on successful PIN, bound to this endpoint and user (`DEC-066`). It stays valid for 3 minutes after its last successful refresh (`DEC-069`). Mute invalidates it (`DEC-056`). A lost invalidate still dies at that bound (`DEC-070`). |

## 3. Operation inventory and decided rules

| Operation | Trigger and direction | Decided behavior | Open boundary |
|---|---|---|---|
| `OP-HEALTH` | Endpoint → server; boot, connectivity change, and every 60 seconds while powered | `POST /v2/endpoint/health` with the desk Bearer. The credential is placed on the desk before first boot. No route in this journey issues it. Runs while muted and independently of roster requests and user presence. Payload is device health only (`DEC-067`). Server marks endpoint stale after 180 seconds (§4a). Stale is server-only. Failed outage beats are skipped, not queued. Connectivity return sends a fresh beat immediately. Stale clears on a successful beat. One in-flight request covers overlapping triggers. | None for installation |
| `OP-ROSTER` | Endpoint → server; unmute/readiness, active-roster refresh, Wi-Fi return, unknown-user reconciliation, and connecting after exhausted PIN transport | `GET /v2/hangout` with the desk Bearer. HTTP 200 plus a valid roster body, including zero users, is required before selectable cards. Active refresh is 60 seconds, slowing to five minutes after ten idle minutes without changes. One failed periodic check gets one immediate retry, then connecting in the lower slot. Connecting uses that same roster/readiness request with a 6-second request timeout (`DEC-044`), then waits 1, 2, and 4 seconds, then every 30 seconds (`DEC-042`). Entering connecting from exhausted `OP-PIN` starts that same schedule even if Wi-Fi never dropped (`COM-005`). Success or Wi-Fi return resets the backoff. Normal roster polling keeps a six-second timeout. Failure classes are §4a. | None for the route or failure classes |
| `OP-PIN` | Endpoint → server; fourth PIN digit | `POST /v2/session/login` with the desk Bearer. Four total attempts for a timed-out, unanswered, or HTTP 5xx request, each with a one-second timeout and delays of 1/2/4 seconds. Same key and exact request on retry; server records the first result and replays it for exact duplicates without reprocessing for one minute from first acceptance. Success issues a server session and does not return inbox (`DEC-066`). Expired or mismatched keys follow `DEC-068`. A missing record after restart is `operation_expired`. Exhausted attempts close the PIN operation and start connecting `OP-ROSTER` (`COM-005`). | Atomic in-flight claim (`COM-OPEN-007`) |
| `OP-SESSION-INVALIDATE` | Endpoint → server; mute during or after an in-flight PIN success | `POST /v2/session/invalidate` with the desk Bearer. Best-effort, non-blocking. Always identifies the PIN operation; includes the session credential if the endpoint already received it. Login is already canceled locally; late PIN success cannot restore it (`EXP-042`). Arrival revokes immediately. If it never arrives, the credential dies at the 3-minute bound (`DEC-070`). Three attempts: immediate, then 1 second, then 2 seconds. | None |
| `OP-SESSION-REFRESH` | Signed-in endpoint → server; every 60 seconds while signed in | `POST /v2/session/refresh` with body `{}`. Extends the credential. Does not ask for a PIN or change stage. Failure leaves the endpoint signed in (`DEC-069`, `COM-017`). Not a health beat. | None |

- **COM-001:** `OP-HEALTH` MUST continue every 60 seconds while the endpoint is powered, including while muted, and MUST also be triggered on boot and connectivity changes (`EXP-035`). A roster request is not a substitute for a health beat.
- **COM-002:** The server MUST mark the endpoint stale after 180 seconds without a successful beat (§4a, `EXP-035`). Stale health MUST NOT change the endpoint login screen or local login state (`EXP-041`). A later successful beat MUST clear server stale status (`EXP-043`).
- **COM-010:** `OP-HEALTH` MUST carry only device health: that this powered endpoint is alive, mute latch on or off, this boot, and why the beat was sent (`boot`, `periodic`, or `connectivity`). It MUST NOT include user names, user presence, peer-online, logs, or instructions that change login (`DEC-067`, `EXP-051`).
- **COM-011:** A valid health acknowledgement is `accepted` plus server time. It MUST NOT report stale to the endpoint, MUST NOT include roster or session data, and MUST NOT change login stage.
- **COM-009:** During an outage the endpoint MUST skip and discard failed health beats. It MUST NOT queue or replay them. When connectivity returns it MUST send a fresh beat immediately, then resume the 60-second interval (`EXP-043`).
- **COM-003:** `OP-ROSTER` MUST return valid readiness information before the endpoint displays selectable roster cards (`EXP-004`, `EXP-024`, `EXP-034`). A malformed or unclassified server result must not be treated as ready.
- **COM-004:** An active roster MUST follow the 60-second/idle five-minute policy. A failed periodic check MUST get one immediate retry; after a second failure the client shows connecting in the lower slot. Wi-Fi recovery changes the lower-slot copy from `no Wi-Fi` and causes an immediate server attempt; if that attempt is not valid, the lower slot shows connecting (`EXP-011`, `EXP-023`, `DEC-043`). While connecting is showing, the roster/readiness request uses a 6-second timeout (`DEC-044`); failed requests then wait 1 second, then 2 seconds, then 4 seconds, then 30 seconds (`DEC-042`). Success or Wi-Fi return resets that backoff. The six-second timeout applies only to normal roster polling (`DEC-038`). The 6 seconds is that connecting request’s timeout, not a second timer around polling. Full-canvas PIN cooldown takes presentation priority until the timer ends (`EXP-040`).
- **COM-005:** A PIN timeout, missing response, connection failure, HTTP 5xx (including `storage_unavailable`), HTTP 429, or unclassified login result MUST not count as a wrong PIN. Those `OP-PIN` attempts MUST follow four total one-second attempts with 1/2/4-second retry delays (`EXP-022`). The visible stage during retries remains PIN verification. When the fourth attempt fails, the client MUST close that PIN operation, show connecting, and **start the connecting `OP-ROSTER` schedule in `COM-004`** even if Wi-Fi never dropped. It MUST NOT leave connecting with no hangout attempt scheduled. A later valid hangout MUST restore user selection and MUST NOT resume the exhausted PIN (`EXP-029`). Health beats MAY continue independently (`COM-001`) and MUST NOT be treated as PIN recovery.
- **COM-006:** Each new PIN submission MUST have a new operation key. Retries MUST repeat the exact request with the same key. During the one-minute result-retention window, which starts when the server first accepts the request, the server MUST replay the first recorded result for an exact duplicate without reprocessing and log the duplicate (`DEC-032`, `DEC-039`, `DEC-068`).
- **COM-007:** A roster or login response MUST be checked against the current session and operation identity before it changes client state. Mute cancels login locally; a late success cannot sign the endpoint back in (`EXP-010`, `EXP-017`, `EXP-033`). If the server created a session, the endpoint MUST request invalidation best-effort without blocking mute (`EXP-042`, `COM-016`).
- **COM-008:** Health and roster activity MUST NOT overwrite active PIN entry or turn a transport failure into a credential failure ([client standard](../../standards/client-application-coding-standards.md) §§3, 7; `EXP-015`).
- **COM-012:** A roster body is valid only when it satisfies §4. An empty `users` array is valid ready (`EXP-020`). A malformed body is not ready and MUST NOT be treated as wrong PIN (`EXP-024`).
- **COM-013:** On `authenticated`, the server MUST issue a session bound to this endpoint and the selected user. The login result MUST NOT include inbox content (`DEC-066`, `EXP-050`).
- **COM-014:** After the one-minute window, a reused PIN operation key MUST NOT authenticate and MUST NOT count as wrong PIN (`DEC-068`). The endpoint treats that result as connecting, not `wrong pin`.
- **COM-015:** The same PIN operation key with a different `user_id` or `pin` MUST NOT create a second login. The server returns `identity_conflict`. The endpoint shows connecting, not `wrong pin` (`DEC-068`).
- **COM-016:** `OP-SESSION-INVALIDATE` MUST include the PIN `operation_id`. It MUST include the session credential when the endpoint already received one. The server MUST revoke any session created by that operation for this endpoint. The request MUST NOT block mute. Repeating the same invalidate is idempotent. When the request arrives, revocation is immediate (`DEC-070`).
- **COM-017:** A signed-in credential MUST be rejected when more than 3 minutes have passed since its last successful refresh, or since issuance if it has never been refreshed (`DEC-069`, `EXP-053`). While the endpoint remains signed in as that user, it MUST refresh that credential in the background. Refresh MUST NOT ask for a PIN, change the visible stage, or return to the roster. Refresh failure MUST leave the endpoint signed in. Inbox and a finished-send wait use their existing connecting treatment. Volume keeps its slider. The record canvas stays up. A replacement credential is issued only after that user mutes and completes PIN sign-in. Refresh MUST NOT be carried on `OP-HEALTH` (`DEC-067`, `COM-010`).
- **COM-018:** A roster member with a missing or unrecognized `avatar_id`, or a missing or unrecognized `card_background_id`, is still ready. The client shows initials and `card-bg-01` for those gaps (`DEC-071`, `EXP-054`). Those gaps MUST NOT make the roster malformed. Missing `user_id` or `display_name`, a non-array `users`, or a duplicate `user_id` remain malformed (`COM-012`).

## 4. Proposed v2 schemas

These JSON objects are the accepted bodies for the routes in §4c. Token format is `COM-019`. HTTP status is `COM-020`. Unknown required `schema` values fail as `unsupported_schema` using the [shared error envelope](user-scoped-envelope.md). Do not log `pin` or session secrets.

Health and roster use the **endpoint** credential. PIN uses the endpoint credential. After `authenticated`, user-scoped calls use the issued session credential. A request body or `X-User-Id` cannot choose the actor.

### 4.1 `OP-HEALTH` — `POST /v2/endpoint/health` (accepted route)

Runs while muted and while signed out. Does not interrupt PIN (`COM-008`).

```json
{
  "schema": "fl.endpoint-health/2",
  "reason": "periodic",
  "boot_id": "7c2f1a0e-3b9d-4c55-9a10-2f8e6b1d4c77",
  "muted": true
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.endpoint-health/2` in this proposal. |
| `reason` | Required enum: `boot`, `periodic`, or `connectivity`. Diagnostics only; does not change stale logic (`COM-OPEN-003`). |
| `boot_id` | Required opaque string generated once per power-on. Identifies this boot; not a user id. |
| `muted` | Required boolean for the physical mute latch. Device state, not user presence. |

Endpoint identity comes from the endpoint credential, not the body. Forbidden in this request: user names, user ids, peer data, logs, RSSI, or commands.

Proposed acknowledgement:

```json
{
  "schema": "fl.endpoint-health-ack/2",
  "accepted": true,
  "server_time": "2026-10-21T20:00:00Z"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.endpoint-health-ack/2`. |
| `accepted` | Required boolean. The client counts the attempt successful only when this is `true` inside a valid ack. The server stale clock uses acceptance of the request (§4a). |
| `server_time` | Required RFC 3339 UTC. Does not replace the stale clock (`COM-OPEN-002`). |

A 200 body missing these fields, `accepted` not true, or an unknown schema is not a successful beat. The ack MUST NOT include stale, roster, session, or login instructions (`COM-011`).

### 4.2 `OP-ROSTER` — `GET /v2/hangout` (accepted route)

No request body. Hangout membership is derived from the authenticated endpoint.

```json
{
  "schema": "fl.hangout-roster/2",
  "hangout_id": "hangout-kitchen",
  "revision": 12,
  "users": [
    {
      "user_id": "user-maya",
      "display_name": "Maya",
      "avatar_id": "avatar-03"
    }
  ]
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.hangout-roster/2`. |
| `hangout_id` | Required non-empty string. |
| `revision` | Required server-owned integer ≥ 0. Increases when membership or a required user field changes. Does not order login recency. |
| `users` | Required array, possibly empty. At most 8 people (`EXP-028`). |
| `users[].user_id` | Required non-empty string; unique within the array; immutable. |
| `users[].display_name` | Required non-empty string. |
| `users[].avatar_id` | Optional string. Absent, empty, or not a known catalog id: the client shows initials and the member stays selectable (`COM-018`). A known id is stored for when avatar art is listed. V1 `avatar_slot` is not a v2 id. |
| `users[].card_background_id` | Optional string. Absent, empty, or not `card-bg-01` through `card-bg-08`: the client shows `card-bg-01` and the member stays selectable (`COM-018`). |

A body is **malformed** (not ready) when JSON is invalid, `schema` is missing or unknown, `hangout_id` is missing/empty, `revision` is missing or not an integer ≥ 0, `users` is not an array, any user lacks `user_id` or `display_name`, a `user_id` repeats, or extra **required** future fields in this schema are absent. A missing or unrecognized avatar or card background is not malformed (`COM-018`). Unknown optional properties are ignored. HTTP 200 with a valid empty `users` array is empty hangout, not connecting (`COM-012`). Unknown hangout is `not_found` and is not ready.

### 4.3 `OP-PIN` — `POST /v2/session/login` (accepted route)

```json
{
  "schema": "fl.session-login/2",
  "operation_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "user_id": "user-maya",
  "pin": "1234"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.session-login/2`. |
| `operation_id` | Required UUID generated by the endpoint for this submission. Unique within this endpoint credential. Retries reuse it. A new PIN entry gets a new id. |
| `user_id` | Required selected-user id, frozen at submit. |
| `pin` | Required four-character string of digits `1`–`9` only (`DEC-035`). Never logged. Transport protection is `COM-OPEN-006`. |

Request fingerprint is `schema` + `operation_id` + `user_id` + `pin`. Cooldown is endpoint-local (`DEC-033`) and is not a server outcome.

Proposed result (same schema for every terminal PIN outcome):

```json
{
  "schema": "fl.session-login-result/2",
  "operation_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "outcome": "authenticated",
  "user_id": "user-maya",
  "session": "<session credential>"
}
```

| `outcome` | Server state | Client visible effect | Required extra fields |
|---|---|---|---|
| `authenticated` | Create exactly one session for this operation | Enter signed-in carousel; update recency (`EXP-012`, `EXP-050`) | `user_id`, `session` |
| `wrong_pin` | No session | Restore PIN marks; `wrong pin`; increment local failure count | none |
| `unknown_user` | No session | Return to roster and refresh (`EXP-018`) | none |

`session` MUST be absent unless `outcome` is `authenticated`. Inbox, profile, and `pin_reset` MUST NOT appear (`COM-013`). Transport, 5xx, malformed JSON, and unclassified codes are connecting, never `wrong_pin` (`COM-008`).

Shared-envelope codes used by this operation:

| Code | When | Client effect |
|---|---|---|
| `identity_conflict` | Same `operation_id` with different `user_id` or `pin` | Connecting; not `wrong pin` (`COM-015`) |
| `operation_expired` | Same `operation_id` after the one-minute window | Connecting; not `wrong pin` (`COM-014`) |
| `unauthenticated` | Bad endpoint credential | Connecting |
| `invalid_request` / `unsupported_schema` | Malformed PIN request | Connecting |
| `temporarily_unavailable` | Retryable server failure | PIN transport retry, then connecting plus hangout retry (`COM-005`, `EXP-022`) |
| `storage_unavailable` | Durable PIN persist failed after PIN check | Same as transport: PIN retry, then connecting plus hangout retry (`COM-005`). Not `wrong_pin` |

**Duplicate handling (proposed, `COM-OPEN-007` implementation):** the server atomically claims `operation_id` for this endpoint before verifying the PIN. A concurrent exact duplicate waits for the recorded result and replays it. It MUST NOT verify the PIN a second time or issue a second session.

**Retention (proposed):** the one-minute window starts when the server first accepts (claims) the request. After expiry the record may be deleted. A later reuse returns `operation_expired` and MUST NOT authenticate (`COM-014`). The client MUST NOT retry the same key after leaving PIN verification. If the server restarts and the one-minute record is gone, the server returns `operation_expired` and MUST NOT authenticate (`COM-014`, accepted engineering wire 2026-10-22).

### 4.4 `OP-SESSION-INVALIDATE` — `POST /v2/session/invalidate` (accepted route)

```json
{
  "schema": "fl.session-invalidate/2",
  "operation_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "session": "<session credential>"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.session-invalidate/2`. |
| `operation_id` | Required. The PIN operation whose session, if any, must be revoked (`COM-016`). |
| `session` | Optional. Present when the endpoint already received the login result. |

Uses the endpoint credential. The server revokes any session created by that `operation_id` for this endpoint, whether or not `session` is supplied. Success is `{ "schema": "fl.session-invalidate-result/2", "operation_id": "...", "revoked": true }` even if no session existed (already revoked or never created). Lost invalidate: retry in the background without blocking mute; do not show UI. The first attempt is immediate, the second is 1 second later, and the third is 2 seconds after the second. Then stop. The credential also dies at the `COM-017` bound if invalidate never arrives (`DEC-070`).

### 4.5 `OP-SESSION-REFRESH` — `POST /v2/session/refresh` (accepted route)

While the endpoint is signed in, it refreshes the current credential every 60 seconds (`COM-017`). This call is not `OP-HEALTH`. The body is an empty JSON object `{}`. The Bearer token is the credential being refreshed. Success is HTTP 200 with `{}`, and the server moves last-refresh time to the time it accepted the request. `session_expired` or `unauthenticated` is a failed refresh: the endpoint stays signed in and does not show PIN. HTTP status numbers are `COM-020`.

## 4a. Accepted login clocks and retries

The owner accepted these engineering choices on 2026-10-22. They do not reopen `DEC-064` or `DEC-072`.

### Missed-beat clock (`COM-OPEN-002`, accepted)

- The server owns the stale clock. It stores `last_successful_beat_at` in server time for that endpoint.
- The server advances that clock when it accepts a valid authenticated health request. It does not wait to learn whether the endpoint received the acknowledgement. No acknowledgement of the acknowledgement is defined.
- The client marks its own attempt successful only when it receives HTTP 200 with `accepted` true and a valid `fl.endpoint-health-ack/2`. An HTTP 200 with a malformed ack is not client success. A lost response is a failed client attempt. The client does not replay that beat. The next beat is the next scheduled beat, or the fresh connectivity-return beat (`COM-009`).
- **Stale after three missed beats** means: the server has not accepted a valid health request for **180 seconds** after `last_successful_beat_at` (three 60-second intervals, `EXP-035`). There is no extra grace window. Elapsed time while accepted beats continue does not make the endpoint stale.
- If the endpoint has never had an accepted beat, it is stale until the first acceptance.
- A later accepted beat clears stale immediately (`COM-002`). Stale is not sent to the endpoint and does not change login (`EXP-041`).

### Overlapping health triggers (`COM-OPEN-003`, accepted)

- Boot, connectivity-return, and the 60-second schedule may coincide. **One** in-flight health request satisfies all concurrent triggers.
- If a scheduled beat is due within 5 seconds of a boot or connectivity-return trigger, send that one request immediately and restart the 60-second interval from its completion attempt, not from the skipped schedule tick.
- The request may include a `reason` of `boot`, `connectivity`, or `periodic` for diagnostics. Reason does not change server stale logic. Connectivity-return is a **fresh** beat (`COM-009`); it is not a replay of failed outage beats.

### Lost-invalidate retries (`COM-OPEN-009`, accepted)

- After mute, send the invalidate in the background. Do not block mute and do not show UI (`COM-016`).
- The first attempt is immediate. The second is 1 second later. The third is 2 seconds after the second. Then stop. The credential still dies at the `COM-017` bound if none arrive.
- A success or an already-revoked result ends the retries. Repeating the same invalidate stays idempotent.

### Which roster failures get the immediate retry (`COM-OPEN-005`, accepted)

Classify every `OP-ROSTER` result before changing stage:

| Class | Detection | Immediate retry while roster is showing? | Visible stage if still failing |
|---|---|---|---|
| Valid ready | HTTP 200 and a fully valid roster body, including zero users | No | Roster or empty hangout |
| Transient transport | Timeout, connection reset, no response, HTTP 5xx, HTTP 429 | Yes, once | Connecting after the retry also fails |
| Malformed / unclassified | HTTP 200 with invalid JSON or missing required fields; 3xx; unknown 4xx other than 401 | Yes, once | Connecting (`EXP-024`) |
| Unauthenticated endpoint | HTTP 401/403 on the endpoint credential | Yes, once | Connecting; not wrong PIN |
| Local Wi-Fi down | Endpoint observes no Wi-Fi | No roster request | `no Wi-Fi` |

Wi-Fi return cancels any in-flight 1-second poll, resets connecting backoff, and starts one 6-second connecting request (`DEC-042`, `DEC-044`). Do not overlap a leftover poll with that recovery request. A background roster result MUST NOT apply while PIN verification is the current operation (`COM-008`).

## 4b. Accepted credential and HTTP map

The owner accepted these engineering choices on 2026-10-22. The routes in §4c are accepted.

## 4c. Accepted login routes

The owner accepted these paths on 2026-10-22. Each uses HTTPS.

| Operation | Method and path | Credential |
|---|---|---|
| `OP-HEALTH` | `POST /v2/endpoint/health` | Desk Bearer |
| `OP-ROSTER` | `GET /v2/hangout` | Desk Bearer |
| `OP-PIN` | `POST /v2/session/login` | Desk Bearer |
| `OP-SESSION-INVALIDATE` | `POST /v2/session/invalidate` | Desk Bearer |
| `OP-SESSION-REFRESH` | `POST /v2/session/refresh` | Session Bearer. Body `{}`. Success HTTP 200 `{}` |

- **COM-019:** A signed-in credential is 32 cryptographically random bytes, encoded as base64url without padding. The login result field `session` carries that encoding. Later user-scoped calls send `Authorization: Bearer` plus that encoding. The server stores the desk and the person bound to the token. A request body or header MUST NOT choose the actor. Health and roster use a separate long-lived desk credential, also `Authorization: Bearer`. That desk credential is placed on the desk before first boot. No route in this journey issues it. The PIN appears only in the login request body, on the protected HTTPS connection, and MUST NOT be logged.
- **COM-020:** Clients branch on the JSON `outcome` or `code`, using this HTTP map. HTTP 200 with a valid body is success, and it is also the status for PIN outcomes `authenticated`, `wrong_pin`, and `unknown_user`. HTTP 401 `unauthenticated` and HTTP 403 `forbidden` or `session_expired` mean a bad credential: login shows connecting, and a finished send stays queued. HTTP 409 `identity_conflict` is connecting on PIN and `couldn't send` on a send. HTTP 410 `operation_expired` is connecting on PIN and does not count as a wrong PIN. HTTP 413 and HTTP 415 are `couldn't send`. HTTP 422 `recipient_unavailable` refuses the whole send. Timeout, connection reset, HTTP 429, and HTTP 5xx keep a send queued and show connecting. HTTP 200 with the wrong schema is a wait: never `sent`, and never a successful login.

## 5. Timing, failure, and recovery matrix

| Event | Decided / proposed result | Remaining contract question |
|---|---|---|
| Health beat succeeds | Server accepts a valid request and advances `last_successful_beat_at`. The client treats the attempt as success only after a valid ack (`accepted` true + `server_time`). Login stage unchanged. Stale clears on server acceptance. | None. The desk credential is placed on the desk before first boot. |
| Wi-Fi/server outage during health beat | Failed beats are skipped and discarded. Connectivity return sends a fresh beat with `reason=connectivity`. | None for the clock (`COM-OPEN-002` accepted). |
| Scheduled beat and connectivity event coincide | One coalesced request; restart the 60 s interval. | None (`COM-OPEN-003` accepted). |
| Periodic roster check fails once | One immediate retry while still on roster, per §4a classes. | None for the taxonomy (`COM-OPEN-005` accepted). |
| Periodic roster check fails twice | Show connecting in the lower slot. Further connecting requests use a 6-second timeout, then wait 1, 2, and 4 seconds, then 30 seconds. | Route is `GET /v2/hangout`. HTTP map is `COM-020`. |
| Wi-Fi returns | Change copy in place to `connecting` if needed, cancel leftover polls, reset backoff, and check immediately with the 6-second connecting request timeout. | None for the route. |
| PIN response lost after server success | Client retries same key/request; server replays recorded `authenticated` with the same session; one local login transition. | Route is `POST /v2/session/login`. Token format is `COM-019`. A missing record after restart is `operation_expired`. |
| PIN response arrives after mute | Ignore it locally. Treat the login as canceled. Invalidate using `operation_id` and `session` if held, without blocking mute. The first attempt is immediate, the second is 1 second later, and the third is 2 seconds after the second. Then stop. The credential dies immediately when invalidate arrives, or at the 3-minute bound if it never arrives. | None (`COM-OPEN-009` accepted). |
| Signed-in refresh succeeds | Stage unchanged. Server last-refresh time moves forward. No PIN prompt. Cadence is 60 seconds. Body `{}`. | None. |
| Signed-in refresh cannot succeed | Endpoint stays signed in. Inbox and send use connecting. Volume and the record canvas stay up. | None for the route. |
| Roster member lacks an avatar or has an unknown card background | Member stays selectable. Initials and `card-bg-01`. Rest of the roster stays selectable. | None for this behavior. |
| PIN key reused with different content | `identity_conflict`; no second login; client shows connecting. | Atomic claim implementation (`COM-OPEN-007`). |
| PIN key reused after one minute | `operation_expired`; not success; not wrong PIN; client shows connecting. | A missing record after restart is the same `operation_expired` (`COM-OPEN-008` accepted). |

## 6. Draft acceptance scenarios

These are planned checks. Routes and token format for this contract are accepted. No device evidence is claimed yet.

| ID | Starting condition and event | Expected client/server result | Rules |
|---|---|---|---|
| `COM-SCN-001` | Endpoint powers on muted | Display remains off; health beat `reason=boot` and `muted=true`; no user-presence claim | COM-001–002, COM-010 |
| `COM-SCN-002` | Endpoint remains powered and muted, and the server accepts no health request for 180 seconds | Three beat intervals are missed. Server marks stale. Endpoint login screen is unchanged. Accepted beats during that window would have kept it fresh | COM-001–002, COM-011 |
| `COM-SCN-017` | Server accepts a valid health request and the response is lost | Server `last_successful_beat_at` advances. The client does not mark that attempt successful and does not replay it. Login screen is unchanged | COM-002, COM-009 |
| `COM-SCN-009` | Health beats fail during an outage, then connectivity returns | Failed beats are discarded; a fresh beat with `reason=connectivity` is sent immediately; stale clears on that success | COM-009, COM-002 |
| `COM-SCN-003` | Active roster check times out, immediate retry succeeds | Roster remains active until the valid response applies; no connecting transition | COM-003–004 |
| `COM-SCN-004` | Both periodic roster attempts fail | Endpoint enters connecting; no stale roster is selectable | COM-003–004 |
| `COM-SCN-010` | HTTP 200 roster with `users: []` | Empty hangout; heading `Sign in as ...`; not connecting | COM-012 |
| `COM-SCN-011` | HTTP 200 roster missing `user_id` on a member | Not ready; connecting; not treated as empty hangout | COM-012 |
| `COM-SCN-005` | First PIN request succeeds remotely, response is lost, retry reaches server | Same key and request; first `authenticated` replayed with the same session; no second login | COM-005–006, COM-013 |
| `COM-SCN-018` | Four `OP-PIN` attempts return HTTP 5xx `storage_unavailable`; Wi-Fi stays up; `/v2/hangout` still returns HTTP 200 | Client stays on PIN through the four attempts, then connecting, then a connecting hangout GET. Valid roster restores selectable cards. That PIN is not resumed. Health may still succeed independently | COM-004–005, COM-008 |
| `COM-SCN-006` | Mute occurs during PIN request; success arrives afterward | Display stays off; endpoint remains signed out; invalidate uses `operation_id`; late success is ignored | COM-007, COM-016 |
| `COM-SCN-007` | PIN entry is active when a background roster or health result arrives | PIN entry and its selected identity remain intact | COM-007–008 |
| `COM-SCN-008` | Same PIN key arrives concurrently with the exact request | Second request waits and receives the first recorded result; one session | COM-006, COM-015 |
| `COM-SCN-012` | Same PIN key arrives with a different `pin` or `user_id` | `identity_conflict`; no second login; endpoint shows connecting | COM-015 |
| `COM-SCN-013` | Same PIN key arrives after the one-minute window | `operation_expired`; not authenticated; not wrong PIN; endpoint shows connecting | COM-014 |
| `COM-SCN-014` | Maya is signed in; refresh succeeds | She stays on the current signed-in stage. No PIN prompt. Server extends that credential | COM-017 |
| `COM-SCN-015` | Maya mutes; invalidate never arrives; 3 minutes pass after the last refresh | Desk stays dark and signed out. Server then rejects that credential | COM-016, COM-017 |
| `COM-SCN-016` | Roster is otherwise valid; one member has no `avatar_id` and `card_background_id` `card-bg-99` | That member is selectable with initials on `card-bg-01`. The roster is ready. Other members stay selectable | COM-012, COM-018 |

## 7. Traceability and follow-on work

| Source decision / requirement | Contract rules | Scenarios |
|---|---|---|
| `DEC-029`, `DEC-031`, `DEC-055`, `DEC-057`, `DEC-067`, `EXP-035`, `EXP-041`, `EXP-043`, `EXP-051` | COM-001–002, COM-009–011 | COM-SCN-001–002, COM-SCN-009, COM-SCN-017 |
| `DEC-030`, `DEC-038`, `EXP-004`, `EXP-011`, `EXP-020`, `EXP-023`–`EXP-024` | COM-003–004, COM-012 | COM-SCN-003–004, COM-SCN-010–011 |
| `DEC-032`, `DEC-039`, `DEC-066`, `DEC-068`, `EXP-014`, `EXP-022`, `EXP-029`, `EXP-050`, `EXP-052` | COM-005–006, COM-013–015 | COM-SCN-005, COM-SCN-008, COM-SCN-012–013, COM-SCN-018 |
| `EXP-010`, `EXP-015`, `EXP-017`, `EXP-033`, `EXP-042`; client standard §§3, 7 | COM-007–008, COM-016 | COM-SCN-006–007 |
| `DEC-069`, `DEC-070`, `EXP-053`, `EXP-055` | COM-017 | COM-SCN-014, COM-SCN-015 |
| `DEC-071`, `EXP-054` | COM-018, COM-012 | COM-SCN-016 |

Owner-visible login behavior in this contract is decided. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Accepted engineering wire | Health route is `POST /v2/endpoint/health`. The desk Bearer is placed on the desk before first boot. No route in this journey issues it | None |
| `COM-OPEN-002` | Accepted engineering wire | Server stale clock advances when a valid health request is accepted. The client marks the attempt successful only on a valid ack. Stale after 180 seconds with no extra grace (§4a) | None |
| `COM-OPEN-003` | Accepted engineering wire | One in-flight health request covers overlapping triggers (§4a) | None |
| `COM-OPEN-004` | Visual follow-on | Roster route is `GET /v2/hangout`. HTTP status for a bad credential is `COM-020` | Avatar artwork |
| `COM-OPEN-005` | Accepted engineering wire | Roster failure classes in §4a | None |
| `COM-OPEN-006` | Accepted engineering wire | Route is `POST /v2/session/login`. Token bytes and PIN transport are `COM-019` | None |
| `COM-OPEN-007` | Engineering implementation | Mismatched-key outcome is decided (`DEC-068`, `COM-015`) | Atomic in-flight claim for `operation_id` |
| `COM-OPEN-008` | Accepted engineering wire | A missing PIN record after restart is `operation_expired` and does not authenticate | None |
| `COM-OPEN-009` | Accepted engineering wire | Three invisible invalidate attempts: immediate, then 1 second, then 2 seconds. They do not block mute | None |
| `COM-OPEN-010` | Accepted engineering wire | `POST /v2/session/refresh` with body `{}` and HTTP 200 `{}`. Cadence is 60 seconds | None |

## 8. Readiness and change record

- **Analysis:** ready to review the proposed schemas against decided experience behavior.
- **Owner review:** product meanings through `DEC-072` are confirmed. Login clocks, the opaque session token, the HTTP map, the five login routes, desk-credential installation, and invalidate spacing are accepted engineering wire. Remaining engineering work on this contract is the atomic in-flight PIN claim (`COM-OPEN-007`), inside the already decided one-session rule.
- **Implementation:** login routes, bodies, and the desk credential rule in §4 can be implemented against this contract. No firmware authorization is implied by this document.

Revision 0.10 (2026-10-22): accepted `POST /v2/endpoint/health`, `GET /v2/hangout`, `POST /v2/session/login`, `POST /v2/session/invalidate`, and `POST /v2/session/refresh` with body `{}`. No implementation authorization.

Revision 0.11 (2026-10-22): the long-lived desk Bearer is placed on the desk before first boot. No route in this journey issues it. No implementation authorization.

Revision 0.12 (2026-10-22): accepted invalidate spacing of immediate, 1 second, and 2 seconds. The attempts do not block mute. No implementation authorization.

Revision 0.14 (2026-10-28): exhausted PIN transport (timeout, no response, or HTTP 5xx including `storage_unavailable`) MUST start connecting `OP-ROSTER` even when Wi-Fi never dropped; a later valid hangout restores cards and does not resume that PIN (`COM-005`, `COM-SCN-018`, `EXP-022`, `EXP-029`). No implementation authorization.

Revision 0.13 (2026-10-22): the server stale clock advances on acceptance of a valid health request. The client marks the attempt successful only when the ack is valid. A lost ack does not roll the server clock back. No implementation authorization.

Revision 0.9 (2026-10-22): accepted the 180-second stale clock, health coalescing, roster failure classes, 60-second refresh, three invisible invalidate attempts, `operation_expired` after a lost PIN record, the 32-byte Bearer session token, and the HTTP map (`COM-019`, `COM-020`). No implementation authorization.

Revision 0.8 (2026-10-22): owner confirmed that amendment checkpoint (login `DEC-072`). No wire protocol is approved.

Revision 0.7 (2026-10-22): recorded the 3-minute credential bound, background refresh without a PIN prompt, immediate revoke when invalidate arrives, and roster fallback for a missing avatar or unknown card background (`COM-017`, `COM-018`). Refresh route and invalidate retry count stayed proposed at that revision. No wire protocol is approved.

Revision 0.1 (2026-10-11): extracted the decided endpoint-health, roster, and PIN exchange rules from `EXP-LOGIN`; identified wire, timing, concurrency, and recovery decisions still required.

Revision 0.2 (2026-10-20): recorded the owner-decided connecting overlay retry: 3-second roster/readiness request timeout while connecting, 1-second timeout while polling, waits of 1, 2, and 4 seconds then every 30 seconds, backoff reset after success or Wi-Fi return, and the in-place `no Wi-Fi` to connecting handoff. No wire protocol is approved.

Revision 0.3 (2026-10-21): recorded server-only stale, skip-failed-beats with an immediate recovery beat, and best-effort session invalidation after mute. No wire protocol is approved.

Revision 0.4 (2026-10-21): captured v1 hangout, login, and heartbeat-demo shapes with retain/adapt/replace judgments. Proposed the missed-beat clock, health-trigger coalescing, and roster retry taxonomy. Left session issuance, roster field validity, PIN key mismatch/expiry, and invalidate identity OPEN for product meaning. No wire protocol is approved.

Revision 0.5 (2026-10-21): recorded owner-accepted session issuance without inbox, device-only health beats, and expired/mismatched PIN-key outcomes. Added proposed health, roster, PIN, and invalidate schemas. Remaining OPEN items are routes, token format, clock, retry taxonomy, restart durability, and lost-invalidate retries. No wire protocol is approved.

Revision 0.6 (2026-10-21): moved `COM-OPEN-001`–`009` to follow-on work. No product behavior changed. No wire protocol is approved.

Revision (2026-10-30): connecting `OP-ROSTER` request timeout raised from 3 to 6 seconds (`DEC-044`); normal roster polling and its immediate retry raised from 1 to 6 seconds (`DEC-038`). Health and PIN request timeouts are unchanged.
