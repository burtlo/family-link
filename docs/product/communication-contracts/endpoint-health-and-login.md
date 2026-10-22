# Endpoint health and login communication contract

| Field | Value |
|---|---|
| ID | `COM-ENDPOINT-HEALTH-LOGIN` |
| Revision | 0.6 |
| Status | Interview draft; product meanings accepted 2026-10-21; v2 schemas are a labeled proposal |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | No approval claimed; no implementation authorization implied |
| Participants | Powered endpoint, server, and the endpoint's login session |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) for session credential shape after PIN success |

## 1. Scope and authority

This contract gathers the exchanges exposed by [endpoint login](../experiences/endpoint-login.md): endpoint-health heartbeat, signed-out readiness and roster refresh, PIN verification, and best-effort session invalidation after mute. It does not define signed-in message activity or user-presence policy. Its purpose is to make the two sides interoperable without treating a historical demo as a v2 protocol.

| Source | Authority here |
|---|---|
| [`EXP-LOGIN`](../experiences/endpoint-login.md) `DEC-029`–`DEC-032`, `DEC-038`–`DEC-039`, `DEC-054`–`DEC-057`, `DEC-066`–`DEC-068`, `EXP-004`, `EXP-010`–`EXP-011`, `EXP-014`, `EXP-022`–`EXP-024`, `EXP-035`, `EXP-040`–`EXP-043`, `EXP-050`–`EXP-052` | Owner-decided behavior and timing, including session issuance, health-beat meaning, and PIN-key expiry/mismatch |
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

Conceptual `POST /v2/...` and `GET /v2/...` paths below follow the [user-scoped envelope](user-scoped-envelope.md). They are **not approved routes**. The `<session credential>` and endpoint Bearer placeholders are not token formats.

## 2. Vocabulary and state ownership

| Term | Meaning and owner |
|---|---|
| Device health | Whether the server has recent evidence from the powered endpoint. The server derives current/stale state from health beats. This does not state whether a person is online (`DEC-067`). |
| Connectivity | The endpoint's observed Wi-Fi and server reachability; local Wi-Fi absence and server failure are distinct. |
| Readiness | HTTP 200 with a fully valid roster body as defined in §4. Empty `users` is valid (`EXP-020`). |
| Roster | Server-owned hangout membership. Endpoint-local login recency only orders received users and lasts for the current boot. |
| Login operation | One immutable selected-user/PIN submission with one unique operation key; transport retries are the same operation. |
| Login session | Endpoint state that can accept a login result. Mute ends it and late results cannot restore it. |
| Server session | Server-issued credential created on successful PIN, bound to this endpoint and user (`DEC-066`). Mute invalidates it (`DEC-056`). |

## 3. Operation inventory and decided rules

| Operation | Trigger and direction | Decided behavior | Open boundary |
|---|---|---|---|
| `OP-HEALTH` | Endpoint → server; boot, connectivity change, and every 60 seconds while powered | Runs while muted and independently of roster requests and user presence. Payload is device health only (`DEC-067`). Server marks endpoint stale after three missed beats. Stale is server-only. Failed outage beats are skipped, not queued. Connectivity return sends a fresh beat immediately. Stale clears on a successful beat. | Routes, endpoint credential format, missed-beat clock proposal |
| `OP-ROSTER` | Endpoint → server; unmute/readiness, active-roster refresh, Wi-Fi return, and unknown-user reconciliation | HTTP 200 plus a valid roster body, including zero users, is required before selectable cards. Active refresh is 60 seconds, slowing to five minutes after ten idle minutes without changes. One failed periodic check gets one immediate retry, then connecting in the lower slot. Connecting uses that same roster/readiness request with a 3-second request timeout (`DEC-044`), then waits 1, 2, and 4 seconds, then every 30 seconds (`DEC-042`). Success or Wi-Fi return resets the backoff. Normal roster polling keeps a one-second timeout. | Final field names, HTTP status map, and COM-OPEN-005 retry taxonomy |
| `OP-PIN` | Endpoint → server; fourth PIN digit | Four total attempts for a timed-out request, each with a one-second timeout and delays of 1/2/4 seconds. Same key and exact request on retry; server records the first result and replays it for exact duplicates without reprocessing for one minute from first acceptance. Success issues a server session and does not return inbox (`DEC-066`). Expired or mismatched keys follow `DEC-068`. | Token format, PIN protection, HTTP status map, restart durability |
| `OP-SESSION-INVALIDATE` | Endpoint → server; mute during or after an in-flight PIN success | Best-effort, non-blocking. Always identifies the PIN operation; includes the session credential if the endpoint already received it. Login is already canceled locally; late PIN success cannot restore it (`EXP-042`). | Lost-invalidate retry count and session lifetime if invalidate is lost |

- **COM-001:** `OP-HEALTH` MUST continue every 60 seconds while the endpoint is powered, including while muted, and MUST also be triggered on boot and connectivity changes (`EXP-035`). A roster request is not a substitute for a health beat.
- **COM-002:** The server MUST mark the endpoint stale after three missed beats (`EXP-035`). Stale health MUST NOT change the endpoint login screen or local login state (`EXP-041`). A later successful beat MUST clear server stale status (`EXP-043`). The missed-beat clock proposal is §4a / `COM-OPEN-002`.
- **COM-010:** `OP-HEALTH` MUST carry only device health: that this powered endpoint is alive, mute latch on or off, this boot, and why the beat was sent (`boot`, `periodic`, or `connectivity`). It MUST NOT include user names, user presence, peer-online, logs, or instructions that change login (`DEC-067`, `EXP-051`).
- **COM-011:** A valid health acknowledgement is `accepted` plus server time. It MUST NOT report stale to the endpoint, MUST NOT include roster or session data, and MUST NOT change login stage.
- **COM-009:** During an outage the endpoint MUST skip and discard failed health beats. It MUST NOT queue or replay them. When connectivity returns it MUST send a fresh beat immediately, then resume the 60-second interval (`EXP-043`).
- **COM-003:** `OP-ROSTER` MUST return valid readiness information before the endpoint displays selectable roster cards (`EXP-004`, `EXP-024`, `EXP-034`). A malformed or unclassified server result must not be treated as ready.
- **COM-004:** An active roster MUST follow the 60-second/idle five-minute policy. A failed periodic check MUST get one immediate retry; after a second failure the client shows connecting in the lower slot. Wi-Fi recovery changes the lower-slot copy from `no Wi-Fi` and causes an immediate server attempt; if that attempt is not valid, the lower slot shows connecting (`EXP-011`, `EXP-023`, `DEC-043`). While connecting is showing, the roster/readiness request uses a 3-second timeout (`DEC-044`); failed requests then wait 1 second, then 2 seconds, then 4 seconds, then 30 seconds (`DEC-042`). Success or Wi-Fi return resets that backoff. The one-second timeout applies only to normal roster polling (`DEC-038`). The 3 seconds is that connecting request’s timeout, not a second timer around polling. Full-canvas PIN cooldown takes presentation priority until the timer ends (`EXP-040`).
- **COM-005:** A PIN timeout MUST not count as a wrong PIN. Timed-out `OP-PIN` attempts MUST follow four total one-second attempts with 1/2/4-second retry delays (`EXP-022`). The visible stage during retries remains PIN verification; exhausted attempts lead to connecting.
- **COM-006:** Each new PIN submission MUST have a new operation key. Retries MUST repeat the exact request with the same key. During the one-minute result-retention window, which starts when the server first accepts the request, the server MUST replay the first recorded result for an exact duplicate without reprocessing and log the duplicate (`DEC-032`, `DEC-039`, `DEC-068`).
- **COM-007:** A roster or login response MUST be checked against the current session and operation identity before it changes client state. Mute cancels login locally; a late success cannot sign the endpoint back in (`EXP-010`, `EXP-017`, `EXP-033`). If the server created a session, the endpoint MUST request invalidation best-effort without blocking mute (`EXP-042`, `COM-016`).
- **COM-008:** Health and roster activity MUST NOT overwrite active PIN entry or turn a transport failure into a credential failure ([client standard](../../standards/client-application-coding-standards.md) §§3, 7; `EXP-015`).
- **COM-012:** A roster body is valid only when it satisfies §4. An empty `users` array is valid ready (`EXP-020`). A malformed body is not ready and MUST NOT be treated as wrong PIN (`EXP-024`).
- **COM-013:** On `authenticated`, the server MUST issue a session bound to this endpoint and the selected user. The login result MUST NOT include inbox content (`DEC-066`, `EXP-050`).
- **COM-014:** After the one-minute window, a reused PIN operation key MUST NOT authenticate and MUST NOT count as wrong PIN (`DEC-068`). The endpoint treats that result as connecting, not `wrong pin`.
- **COM-015:** The same PIN operation key with a different `user_id` or `pin` MUST NOT create a second login. The server returns `identity_conflict`. The endpoint shows connecting, not `wrong pin` (`DEC-068`).
- **COM-016:** `OP-SESSION-INVALIDATE` MUST include the PIN `operation_id`. It MUST include the session credential when the endpoint already received one. The server MUST revoke any session created by that operation for this endpoint. The request MUST NOT block mute. Repeating the same invalidate is idempotent.

## 4. Proposed v2 schemas

These JSON objects are an **engineering proposal**. They are not approved routes, token formats, or HTTP status maps. Examples are illustrative. Unknown required `schema` values fail as `unsupported_schema` using the [shared error envelope](user-scoped-envelope.md). Do not log `pin` or session secrets.

Health and roster use the **endpoint** credential. PIN uses the endpoint credential. After `authenticated`, user-scoped calls use the issued session credential. A request body or `X-User-Id` cannot choose the actor.

### 4.1 `OP-HEALTH` — `POST /v2/endpoint/health` (proposed)

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
| `accepted` | Required boolean. Only `true` counts as a successful beat. |
| `server_time` | Required RFC 3339 UTC. Does not replace the stale clock (`COM-OPEN-002`). |

A 200 body missing these fields, `accepted` not true, or an unknown schema is not a successful beat. The ack MUST NOT include stale, roster, session, or login instructions (`COM-011`).

### 4.2 `OP-ROSTER` — `GET /v2/hangout` (proposed)

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
| `users` | Required array, possibly empty. Maximum OPEN; v2 hangouts support at least eight (`EXP-028`). |
| `users[].user_id` | Required non-empty string; unique within the array; immutable. |
| `users[].display_name` | Required non-empty string. |
| `users[].avatar_id` | Required non-empty catalog id. V1 `avatar_slot` is not a v2 id. Device assets and ids: [`FOUNDATION-PROFILE-AVATARS`](../device-ui/foundations/FOUNDATION-PROFILE-AVATARS.md) (`COM-OPEN-003`). |

A body is **malformed** (not ready) when JSON is invalid, `schema` is missing or unknown, `hangout_id` is missing/empty, `revision` is missing or not an integer ≥ 0, `users` is not an array, any user lacks `user_id` / `display_name` / `avatar_id`, a `user_id` repeats, or extra **required** future fields in this schema are absent. Unknown optional properties are ignored. HTTP 200 with a valid empty `users` array is empty hangout, not connecting (`COM-012`). Unknown hangout is `not_found` and is not ready.

### 4.3 `OP-PIN` — `POST /v2/session/login` (proposed)

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
| `temporarily_unavailable` | Retryable server failure | PIN transport retry, then connecting (`EXP-022`) |

**Duplicate handling (proposed, `COM-OPEN-007` implementation):** the server atomically claims `operation_id` for this endpoint before verifying the PIN. A concurrent exact duplicate waits for the recorded result and replays it. It MUST NOT verify the PIN a second time or issue a second session.

**Retention (proposed):** the one-minute window starts when the server first accepts (claims) the request. After expiry the record may be deleted. A later reuse returns `operation_expired` and MUST NOT authenticate (`COM-014`). The client MUST NOT retry the same key after leaving PIN verification. Server-restart durability of that record remains `COM-OPEN-008`; this proposal prefers rejecting a missing record as `operation_expired` rather than logging in twice.

### 4.4 `OP-SESSION-INVALIDATE` — `POST /v2/session/invalidate` (proposed)

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

Uses the endpoint credential. The server revokes any session created by that `operation_id` for this endpoint, whether or not `session` is supplied. Success is `{ "schema": "fl.session-invalidate-result/2", "operation_id": "...", "revoked": true }` even if no session existed (already revoked or never created). Lost invalidate: retry in the background without blocking mute; do not show UI. How many retries and session lifetime if the invalidate is lost remain `COM-OPEN-009`.

## 4a. Engineering proposals that do not change locked login UX

These are protocol choices. They are **not** owner-approved. They do not reopen `DEC-064`.

### Missed-beat clock (`COM-OPEN-002`, proposed)

- The server owns the stale clock. It stores `last_successful_beat_at` in server time for that endpoint.
- A beat is successful only when the server accepts a valid health request and the endpoint receives a valid acknowledgement. An HTTP 200 with a malformed ack is not success.
- **Stale after three missed beats** means: no successful beat for **180 seconds** after `last_successful_beat_at` (three 60-second intervals, `EXP-035`). There is no extra grace window in this proposal.
- If the endpoint has never sent a successful beat, it is stale until the first success.
- A later successful beat clears stale immediately (`COM-002`). Stale is not sent to the endpoint and does not change login (`EXP-041`).

### Overlapping health triggers (`COM-OPEN-003`, proposed)

- Boot, connectivity-return, and the 60-second schedule may coincide. **One** in-flight health request satisfies all concurrent triggers.
- If a scheduled beat is due within 5 seconds of a boot or connectivity-return trigger, send that one request immediately and restart the 60-second interval from its completion attempt, not from the skipped schedule tick.
- The request may include a `reason` of `boot`, `connectivity`, or `periodic` for diagnostics. Reason does not change server stale logic. Connectivity-return is a **fresh** beat (`COM-009`); it is not a replay of failed outage beats.

### Which roster failures get the immediate retry (`COM-OPEN-005`, proposed)

Classify every `OP-ROSTER` result before changing stage:

| Class | Detection | Immediate retry while roster is showing? | Visible stage if still failing |
|---|---|---|---|
| Valid ready | HTTP 200 and a fully valid roster body, including zero users | No | Roster or empty hangout |
| Transient transport | Timeout, connection reset, no response, HTTP 5xx, HTTP 429 | Yes, once | Connecting after the retry also fails |
| Malformed / unclassified | HTTP 200 with invalid JSON or missing required fields; 3xx; unknown 4xx other than 401 | Yes, once | Connecting (`EXP-024`) |
| Unauthenticated endpoint | HTTP 401/403 on the endpoint credential | Yes, once | Connecting; not wrong PIN |
| Local Wi-Fi down | Endpoint observes no Wi-Fi | No roster request | `no Wi-Fi` |

Wi-Fi return cancels any in-flight 1-second poll, resets connecting backoff, and starts one 3-second connecting request (`DEC-042`, `DEC-044`). Do not overlap a leftover poll with that recovery request. A background roster result MUST NOT apply while PIN verification is the current operation (`COM-008`).

## 5. Timing, failure, and recovery matrix

| Event | Decided / proposed result | Remaining contract question |
|---|---|---|
| Health beat succeeds | Valid ack (`accepted` true + `server_time`); login stage unchanged; stale clears. | Endpoint credential format and route (`COM-OPEN-001`). |
| Wi-Fi/server outage during health beat | Failed beats are skipped and discarded. Connectivity return sends a fresh beat with `reason=connectivity`. | Clock proposal in §4a. |
| Scheduled beat and connectivity event coincide | One coalesced request; restart the 60 s interval. | Accept §4a coalescing. |
| Periodic roster check fails once | One immediate retry while still on roster, per §4a classes. | Accept §4a taxonomy. |
| Periodic roster check fails twice | Show connecting in the lower slot. Further connecting requests use a 3-second timeout, then wait 1, 2, and 4 seconds, then 30 seconds. | HTTP status map (`COM-OPEN-004`). |
| Wi-Fi returns | Change copy in place to `connecting` if needed, cancel leftover polls, reset backoff, and check immediately with the 3-second connecting request timeout. | Same as roster validation. |
| PIN response lost after server success | Client retries same key/request; server replays recorded `authenticated` with the same session; one local login transition. | Token format; restart durability (`COM-OPEN-006`, `COM-OPEN-008`). |
| PIN response arrives after mute | Ignore it locally. Treat the login as canceled. Invalidate using `operation_id` and `session` if held, without blocking mute. | Lost-invalidate retry count (`COM-OPEN-009`). |
| PIN key reused with different content | `identity_conflict`; no second login; client shows connecting. | Atomic claim implementation (`COM-OPEN-007`). |
| PIN key reused after one minute | `operation_expired`; not success; not wrong PIN; client shows connecting. | Record storage after restart (`COM-OPEN-008`). |

## 6. Draft acceptance scenarios

These are planned checks, not completed verification. They cannot be implementation tests until routes and token format are accepted.

| ID | Starting condition and event | Expected client/server result | Rules |
|---|---|---|---|
| `COM-SCN-001` | Endpoint powers on muted | Display remains off; health beat `reason=boot` and `muted=true`; no user-presence claim | COM-001–002, COM-010 |
| `COM-SCN-002` | Endpoint remains powered and muted across three nominal 60-second periods | Beats continue; server marks stale; endpoint login screen is unchanged | COM-001–002, COM-011 |
| `COM-SCN-009` | Health beats fail during an outage, then connectivity returns | Failed beats are discarded; a fresh beat with `reason=connectivity` is sent immediately; stale clears on that success | COM-009, COM-002 |
| `COM-SCN-003` | Active roster check times out, immediate retry succeeds | Roster remains active until the valid response applies; no connecting transition | COM-003–004 |
| `COM-SCN-004` | Both periodic roster attempts fail | Endpoint enters connecting; no stale roster is selectable | COM-003–004 |
| `COM-SCN-010` | HTTP 200 roster with `users: []` | Empty hangout; heading `Sign in as ...`; not connecting | COM-012 |
| `COM-SCN-011` | HTTP 200 roster missing `user_id` on a member | Not ready; connecting; not treated as empty hangout | COM-012 |
| `COM-SCN-005` | First PIN request succeeds remotely, response is lost, retry reaches server | Same key and request; first `authenticated` replayed with the same session; no second login | COM-005–006, COM-013 |
| `COM-SCN-006` | Mute occurs during PIN request; success arrives afterward | Display stays off; endpoint remains signed out; invalidate uses `operation_id`; late success is ignored | COM-007, COM-016 |
| `COM-SCN-007` | PIN entry is active when a background roster or health result arrives | PIN entry and its selected identity remain intact | COM-007–008 |
| `COM-SCN-008` | Same PIN key arrives concurrently with the exact request | Second request waits and receives the first recorded result; one session | COM-006, COM-015 |
| `COM-SCN-012` | Same PIN key arrives with a different `pin` or `user_id` | `identity_conflict`; no second login; endpoint shows connecting | COM-015 |
| `COM-SCN-013` | Same PIN key arrives after the one-minute window | `operation_expired`; not authenticated; not wrong PIN; endpoint shows connecting | COM-014 |

## 7. Traceability and follow-on work

| Source decision / requirement | Contract rules | Scenarios |
|---|---|---|
| `DEC-029`, `DEC-031`, `DEC-055`, `DEC-057`, `DEC-067`, `EXP-035`, `EXP-041`, `EXP-043`, `EXP-051` | COM-001–002, COM-009–011 | COM-SCN-001–002, COM-SCN-009 |
| `DEC-030`, `DEC-038`, `EXP-004`, `EXP-011`, `EXP-020`, `EXP-023`–`EXP-024` | COM-003–004, COM-012 | COM-SCN-003–004, COM-SCN-010–011 |
| `DEC-032`, `DEC-039`, `DEC-066`, `DEC-068`, `EXP-014`, `EXP-022`, `EXP-050`, `EXP-052` | COM-005–006, COM-013–015 | COM-SCN-005, COM-SCN-008, COM-SCN-012–013 |
| `EXP-010`, `EXP-015`, `EXP-017`, `EXP-033`, `EXP-042`; client standard §§3, 7 | COM-007–008, COM-016 | COM-SCN-006–007 |

Owner-visible login behavior in this contract is decided. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Engineering wire | Health-beat meaning, privacy, and ack contents are decided (`DEC-067`, §4.1) | Health route and endpoint credential format |
| `COM-OPEN-002` | Engineering wire | Stale after three missed beats is decided (`EXP-035`); stale is server-only (`DEC-055`) | Accept or adjust the §4a 180-second clock; no extra grace in this proposal |
| `COM-OPEN-003` | Engineering wire | Boot, connectivity, and periodic beats already run; failed outage beats are skipped (`DEC-057`) | Accept or adjust §4a coalescing of overlapping triggers |
| `COM-OPEN-004` | Engineering wire | Empty versus malformed roster meaning is decided | Roster route, HTTP status map. Avatar catalog IDs are visual/profile follow-on, not a login-behavior question |
| `COM-OPEN-005` | Engineering wire | One immediate retry then connecting is decided | Accept or adjust the §4a failure-class taxonomy |
| `COM-OPEN-006` | Engineering wire | Session issuance and typed PIN outcomes are decided (`DEC-066`) | PIN route, session-token bytes, expiration/refresh, PIN transport protection |
| `COM-OPEN-007` | Engineering implementation | Mismatched-key outcome is decided (`DEC-068`, `COM-015`) | Atomic in-flight claim for `operation_id` |
| `COM-OPEN-008` | Engineering implementation | Expired-key outcome is decided (`DEC-068`, `COM-014`) | Durable one-minute PIN result across server restart. This draft prefers `operation_expired` if the record is missing |
| `COM-OPEN-009` | Engineering implementation | Mute already cancels login and invalidates best-effort (`DEC-056`, `COM-016`) | Lost-invalidate retry count and session lifetime if invalidate never arrives |

## 8. Readiness and change record

- **Analysis:** ready to review the proposed schemas against decided experience behavior.
- **Owner review:** session issuance, health-beat meaning, and PIN-key expiry/mismatch are recorded (`DEC-066`–`DEC-068`). Remaining review is engineering: routes, token format, clock, retry taxonomy, restart durability, and lost-invalidate retries.
- **Implementation:** blocked by follow-on `COM-OPEN-001`–`COM-OPEN-009` until those engineering choices are accepted. Product meaning for those items is not OPEN.
- **Verification:** scenarios are planned; no v2 client/server or device evidence is claimed. No implementation authorization is implied.

Revision 0.1 (2026-10-11): extracted the decided endpoint-health, roster, and PIN exchange rules from `EXP-LOGIN`; identified wire, timing, concurrency, and recovery decisions still required.

Revision 0.2 (2026-10-20): recorded the owner-decided connecting overlay retry: 3-second roster/readiness request timeout while connecting, 1-second timeout while polling, waits of 1, 2, and 4 seconds then every 30 seconds, backoff reset after success or Wi-Fi return, and the in-place `no Wi-Fi` to connecting handoff. No wire protocol is approved.

Revision 0.3 (2026-10-21): recorded server-only stale, skip-failed-beats with an immediate recovery beat, and best-effort session invalidation after mute. No wire protocol is approved.

Revision 0.4 (2026-10-21): captured v1 hangout, login, and heartbeat-demo shapes with retain/adapt/replace judgments. Proposed the missed-beat clock, health-trigger coalescing, and roster retry taxonomy. Left session issuance, roster field validity, PIN key mismatch/expiry, and invalidate identity OPEN for product meaning. No wire protocol is approved.

Revision 0.5 (2026-10-21): recorded owner-accepted session issuance without inbox, device-only health beats, and expired/mismatched PIN-key outcomes. Added proposed health, roster, PIN, and invalidate schemas. Remaining OPEN items are routes, token format, clock, retry taxonomy, restart durability, and lost-invalidate retries. No wire protocol is approved.

Revision 0.6 (2026-10-21): moved `COM-OPEN-001`–`009` to follow-on work. No product behavior changed. No wire protocol is approved.
