# Endpoint health and login communication contract

| Field | Value |
|---|---|
| ID | `COM-ENDPOINT-HEALTH-LOGIN` |
| Revision | 0.3 |
| Status | Interview draft; transport and wire protocol OPEN |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | No approval claimed; no implementation authorization implied |
| Participants | Powered endpoint, server, and the endpoint's login session |

## 1. Scope and authority

This contract gathers the exchanges exposed by [endpoint login](../experiences/endpoint-login.md): endpoint-health heartbeat, signed-out readiness and roster refresh, and PIN verification. It does not define signed-in message activity or user-presence policy. Its purpose is to make the two sides interoperable without treating a historical demo as a v2 protocol.

| Source | Authority here |
|---|---|
| [`EXP-LOGIN`](../experiences/endpoint-login.md) `DEC-029`–`DEC-032`, `DEC-038`–`DEC-039`, `DEC-054`–`DEC-057`, `EXP-004`, `EXP-010`–`EXP-011`, `EXP-014`, `EXP-022`–`EXP-024`, `EXP-035`, `EXP-040`–`EXP-043` | Owner-decided behavior and timing, with stated OPEN boundaries |
| [Client application coding standards](../../standards/client-application-coding-standards.md) §§3–7 | Normative client concurrency and operation constraints |
| [Operational-contract concept guide](../../v1-assessments/operational-contract-shape.md) | Supporting structure, not v2 wire authority |

`MUST` below restates a decided experience requirement or applicable standard. `OPEN` is a contract dependency, not implementation discretion. Example values must not be shipped merely because they appear in this draft.

## 2. Vocabulary and state ownership

| Term | Meaning and owner |
|---|---|
| Device health | Whether the server has recent evidence from the powered endpoint. The server derives current/stale state from health beats. This does not state whether a person is online. |
| Connectivity | The endpoint's observed Wi-Fi and server reachability; local Wi-Fi absence and server failure are distinct. |
| Readiness | Server response accepted as HTTP 200 with valid response information. Field-level validity is `COM-OPEN-004`. |
| Roster | Server-owned hangout membership. Endpoint-local login recency only orders received users and lasts for the current boot. |
| Login operation | One immutable selected-user/PIN submission with one unique operation key; transport retries are the same operation. |
| Login session | Endpoint state that can accept a login result. Mute ends it and late results cannot restore it. |

## 3. Operation inventory and decided rules

| Operation | Trigger and direction | Decided behavior | Open boundary |
|---|---|---|---|
| `OP-HEALTH` | Endpoint → server; boot, connectivity change, and every 60 seconds while powered | Runs while muted and independently of roster requests and user presence. Server marks endpoint stale after three missed beats. Stale is server-only. Failed outage beats are skipped, not queued. Connectivity return sends a fresh beat immediately. Stale clears on a successful beat. | Payload, acknowledgements, event coalescing, precise missed-beat clock |
| `OP-ROSTER` | Endpoint → server; unmute/readiness, active-roster refresh, Wi-Fi return, and unknown-user reconciliation | HTTP 200 plus valid information is required before selectable cards. Active refresh is 60 seconds, slowing to five minutes after ten idle minutes without changes. One failed periodic check gets one immediate retry, then the connecting overlay. Connecting uses that same roster/readiness request with a 3-second request timeout (`DEC-044`), then waits 1, 2, and 4 seconds, then every 30 seconds (`DEC-042`). Success or Wi-Fi return resets the backoff. Normal roster polling keeps a one-second timeout. | Message schema, validation, error taxonomy, and freshness |
| `OP-PIN` | Endpoint → server; fourth PIN digit | Four total attempts for a timed-out request, each with a one-second timeout and delays of 1/2/4 seconds. Same key and exact request on retry; server records the first result and replays it for duplicates without reprocessing for one minute. | Wire representation, atomic concurrent handling, mismatched reuse, expiry and restart semantics |

- **COM-001:** `OP-HEALTH` MUST continue every 60 seconds while the endpoint is powered, including while muted, and MUST also be triggered on boot and connectivity changes (`EXP-035`). A roster request is not a substitute for a health beat.
- **COM-002:** The server MUST mark the endpoint stale after three missed beats (`EXP-035`). Stale health MUST NOT change the endpoint login screen or local login state (`EXP-041`). A later successful beat MUST clear server stale status (`EXP-043`). The precise missed-beat clock and grace boundary remain `COM-OPEN-002`.
- **COM-009:** During an outage the endpoint MUST skip and discard failed health beats. It MUST NOT queue or replay them. When connectivity returns it MUST send a fresh beat immediately, then resume the 60-second interval (`EXP-043`).
- **COM-003:** `OP-ROSTER` MUST return valid readiness information before the endpoint displays selectable roster cards (`EXP-004`, `EXP-024`, `EXP-034`). A malformed or unclassified server result must not be treated as ready.
- **COM-004:** An active roster MUST follow the 60-second/idle five-minute policy. A failed periodic check MUST get one immediate retry; after a second failure the client shows connecting in the lower slot. Wi-Fi recovery changes the lower-slot copy from `no Wi-Fi` and causes an immediate server attempt; if that attempt is not valid, the lower slot shows connecting (`EXP-011`, `EXP-023`, `DEC-043`). While connecting is showing, the roster/readiness request uses a 3-second timeout (`DEC-044`); failed requests then wait 1 second, then 2 seconds, then 4 seconds, then 30 seconds (`DEC-042`). Success or Wi-Fi return resets that backoff. The one-second timeout applies only to normal roster polling (`DEC-038`). The 3 seconds is that connecting request’s timeout, not a second timer around polling. Full-canvas PIN cooldown takes presentation priority until the timer ends (`EXP-040`).
- **COM-005:** A PIN timeout MUST not count as a wrong PIN. Timed-out `OP-PIN` attempts MUST follow four total one-second attempts with 1/2/4-second retry delays (`EXP-022`). The visible stage during retries remains PIN verification; exhausted attempts lead to connecting.
- **COM-006:** Each new PIN submission MUST have a new operation key. Retries MUST repeat the exact request with the same key. During the one-minute result-retention window, the server MUST replay the first recorded result for a duplicate without reprocessing and log the duplicate (`DEC-032`, `DEC-039`).
- **COM-007:** A roster or login response MUST be checked against the current session and operation identity before it changes client state. Mute cancels login locally; a late success cannot sign the endpoint back in (`EXP-010`, `EXP-017`, `EXP-033`). If the server created a session, the endpoint MUST request invalidation best-effort without blocking mute (`EXP-042`). The invalidate request shape is `COM-OPEN-009`.
- **COM-008:** Health and roster activity MUST NOT overwrite active PIN entry or turn a transport failure into a credential failure ([client standard](../../standards/client-application-coding-standards.md) §§3, 7; `EXP-015`).

## 4. Message and state details awaiting decisions

No v2 path, HTTP method, field name, payload example, or status-code map has been approved. These tables are the fields the protocol must define, not a proposed schema.

| Exchange | Required questions for the wire contract |
|---|---|
| Health request/response | Endpoint identity and authentication; boot/session identity; event reason versus periodic beat; health fields and units; server acknowledgement; duplicate/event ordering; whether any server-to-client data belongs in the response; privacy and log policy. |
| Roster request/response | Hangout and endpoint identity; roster revision or freshness marker; required user fields; empty versus malformed roster; unknown hangout; typed errors; response validation and versioning. |
| PIN request/response | Key namespace, entropy and scope; immutable request fingerprint; selected-user identity; credential representation and protection; typed success, wrong PIN, unknown user, cooldown, malformed, and unavailable outcomes; authentication/session issuance; replay of the full result. |

The server must be able to identify a duplicate PIN operation before executing its side effect. How it atomically claims an in-flight key and stores a result is `COM-OPEN-007`; no particular database or framework is prescribed. The one-minute retention start point and behavior after expiry are `COM-OPEN-008`.

## 5. Timing, failure, and recovery matrix

| Event | Decided result | Remaining contract question |
|---|---|---|
| Health beat succeeds | Server has health evidence; login stage is unchanged; stale clears. | What acknowledgement counts as success on the wire? |
| Wi-Fi/server outage during health beat | Failed beats are skipped and discarded. Connectivity return sends a fresh beat immediately. | How is the recovery beat distinguished from a periodic beat? |
| Scheduled beat and connectivity event coincide | Both triggers are requirements. | Whether one message may satisfy both and how its reason is represented. |
| Periodic roster check fails once | One immediate retry while still on roster. | Which typed failures qualify; can malformed response retry? |
| Periodic roster check fails twice | Show connecting in the lower slot. Further connecting requests use a 3-second timeout, then wait 1, 2, and 4 seconds, then 30 seconds. | Which typed failures qualify. |
| Wi-Fi returns | Change copy in place to `connecting` if needed, reset backoff, and check immediately with the 3-second connecting request timeout. | Result schema, validity, concurrent periodic request handling. |
| PIN response lost after server success | Client retries same key/request; server replays recorded success; one local login transition. | Atomic in-flight duplicate behavior and response/session representation. |
| PIN response arrives after mute | Ignore it locally. Treat the login as canceled. Invalidate any created server session best-effort without blocking mute. | Invalidate request shape and lost-invalidate recovery. |
| PIN key reused with different content | No policy established. | Typed rejection, logging, and side-effect prevention. |

## 6. Draft acceptance scenarios

These are planned checks, not completed verification. An OPEN result cannot be used as an implementation test yet.

| ID | Starting condition and event | Expected client/server result | Rules |
|---|---|---|---|
| `COM-SCN-001` | Endpoint powers on muted | Display remains off; health beat is triggered; no user-presence claim follows from it | COM-001–002 |
| `COM-SCN-002` | Endpoint remains powered and muted across three nominal 60-second periods | Beats continue; server marks stale; endpoint login screen is unchanged | COM-001–002 |
| `COM-SCN-009` | Health beats fail during an outage, then connectivity returns | Failed beats are discarded; a fresh beat is sent immediately; stale clears on that success | COM-009, COM-002 |
| `COM-SCN-003` | Active roster check times out, immediate retry succeeds | Roster remains active until the valid response applies; no connecting transition | COM-003–004 |
| `COM-SCN-004` | Both periodic roster attempts fail | Endpoint enters connecting; no stale roster is selectable | COM-003–004 |
| `COM-SCN-005` | First PIN request succeeds remotely, response is lost, retry reaches server | Same key and request; first result replayed; no second login side effect | COM-005–006 |
| `COM-SCN-006` | Mute occurs during PIN request; success arrives afterward | Display stays off; endpoint remains signed out; any created server session is invalidated best-effort | COM-007 |
| `COM-SCN-007` | PIN entry is active when a background roster or health result arrives | PIN entry and its selected identity remain intact | COM-007–008 |
| `COM-SCN-008` | Same PIN key arrives concurrently or with different request content | Expected server response remains OPEN; no implementation-ready test yet | COM-006, COM-OPEN-007 |

## 7. Traceability and OPEN decisions

| Source decision / requirement | Contract rules | Scenarios |
|---|---|---|
| `DEC-029`, `DEC-031`, `DEC-055`, `DEC-057`, `EXP-035`, `EXP-041`, `EXP-043` | COM-001–002, COM-009 | COM-SCN-001–002, COM-SCN-009 |
| `DEC-030`, `DEC-038`, `EXP-004`, `EXP-011`, `EXP-023`–`EXP-024` | COM-003–004 | COM-SCN-003–004 |
| `DEC-032`, `DEC-039`, `EXP-014`, `EXP-022` | COM-005–006 | COM-SCN-005, COM-SCN-008 |
| `EXP-010`, `EXP-015`, `EXP-017`, `EXP-033`, `EXP-042`; client standard §§3, 7 | COM-007–008 | COM-SCN-006–007 |

| ID | Decision needed | Owner or role | Blocks |
|---|---|---|---|
| `COM-OPEN-001` | Define health request/acknowledgement fields, identity/authentication, versioning, privacy, and whether the response carries any server-to-client instructions. | Engineering proposal; product review for meaning/privacy | Health interoperability |
| `COM-OPEN-002` | Define the server's missed-beat clock and grace boundary. Stale meaning, server-only effect, and clear-on-successful-beat are decided. | Engineering proposal for clock | Health state correctness |
| `COM-OPEN-003` | Define overlapping health triggers and whether one message may satisfy both a scheduled beat and a connectivity-return beat. Skip/discard and immediate recovery beat are decided. | Engineering proposal | Health recovery |
| `COM-OPEN-004` | Define roster request/response fields, valid HTTP 200 information, empty versus malformed response, freshness, typed failures, and versioning. | Engineering proposal; product review for readiness meaning | Login readiness |
| `COM-OPEN-005` | Define which roster failures receive the immediate retry. After the overlay appears, connecting requests use a 3-second timeout, then wait 1, 2, and 4 seconds, then every 30 seconds (`DEC-042`, `DEC-044`). Prevent overlap with Wi-Fi-return checks. | Engineering proposal | Roster recovery |
| `COM-OPEN-006` | Define PIN request/response fields, key scope, credential protection, typed outcomes, and session issuance. | Engineering proposal; product review for outcome semantics | Login interoperability |
| `COM-OPEN-007` | Define atomic handling of simultaneous duplicate keys and mismatched request content; ensure no second side effect. | Engineering proposal; product owner if user-visible outcome changes | PIN deduplication |
| `COM-OPEN-008` | Define when one-minute retention begins, behavior after expiry or server restart, and any client retry beyond that window. | Engineering proposal; product owner for changed login outcome | Lost-response recovery |
| `COM-OPEN-009` | Define the session-invalidate request after mute-canceled login: path, identity, lost-request recovery, and whether invalidate uses the PIN operation key. Product policy is decided (`DEC-056`): invalidate best-effort and do not block mute. | Engineering proposal | Sign-out correctness |

## 8. Readiness and change record

- **Analysis:** ready to propose schema and outage policies against the decided experience behavior.
- **Owner review:** stale meaning, skip-failed-beats, and mute session invalidation are recorded; remaining review is wire meaning and privacy.
- **Implementation:** blocked by `COM-OPEN-001`–`COM-OPEN-009` where each exchange is in scope.
- **Verification:** scenarios are planned; no v2 client/server or device evidence is claimed.

Revision 0.1 (2026-10-11): extracted the decided endpoint-health, roster, and PIN exchange rules from `EXP-LOGIN`; identified wire, timing, concurrency, and recovery decisions still required.

Revision 0.2 (2026-10-20): recorded the owner-decided connecting overlay retry: 3-second roster/readiness request timeout while connecting, 1-second timeout while polling, waits of 1, 2, and 4 seconds then every 30 seconds, backoff reset after success or Wi-Fi return, and the in-place `no Wi-Fi` to connecting handoff. No wire protocol is approved.

Revision 0.3 (2026-10-21): recorded server-only stale, skip-failed-beats with an immediate recovery beat, and best-effort session invalidation after mute. No wire protocol is approved.
