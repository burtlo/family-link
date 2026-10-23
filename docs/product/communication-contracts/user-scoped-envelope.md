# Proposed shared envelope for user-scoped exchanges

| Field | Value |
|---|---|
| ID | `COM-USER-SCOPED-ENVELOPE` |
| Revision | 0.10 |
| Status | Engineering wire; session token, HTTP map, core-journey routes, schema names, and the no-Retry-After rule are accepted; this journey does not use a push channel |
| Scope | Send, inbox, media/playback state, profile |

## Why this is needed

The [v1 communication inventory](../../v1-assessments/communication-contract-inventory.md) found that v1 user-scoped routes accept an endpoint Bearer token and a caller-supplied `X-User-Id`; the server checks that the ID exists, but the route does not bind it to a server-issued signed-in session. A v2 message schema needs the server to know which signed-in user is acting. Session issuance on PIN success, mute invalidation, and the 3-minute credential bound are decided in [endpoint health and login](endpoint-health-and-login.md) (`DEC-066`, `DEC-056`, `DEC-069`, `DEC-070`, `COM-013`, `COM-016`, `COM-017`). Token bytes are accepted in that contract as `COM-019`. Refresh is `POST /v2/session/refresh`.

## Proposed transport and authority

| Element | Proposed contract | Status |
|---|---|---|
| User-scoped authorization | A server-issued credential binds `endpoint_id`, `user_id`, and a session identifier. It is 32 random bytes, base64url without padding, sent as `Authorization: Bearer` (`COM-019` in the login contract). The server derives the actor from it. A request body or `X-User-Id` cannot choose the actor. The credential is valid for 3 minutes after its last successful refresh, or after issuance if never refreshed. Mute revocation is immediate when invalidate arrives. | Accepted token format; lifetime and revocation meaning decided |
| Transport | Authenticated HTTPS for reads and writes. PIN travels only in the login body and is never logged. This journey does not open a push channel. Inbox membership arrives by `GET /v2/inbox` on the inbox contract schedule. | HTTPS for requests is accepted. No push route on this journey |
| Schema version | Every JSON request and success body in this journey requires the `schema` literal already written in its contract. Two HTTP 200 successes stay `{}` with no `schema` field: `POST /v2/session/refresh`, and `PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}`. Byte bodies, including media reads and upload-part requests, have no `schema` field. An unknown required schema is `unsupported_schema`. Error JSON uses `fl.error/2`. | Accepted |
| Operation identity | State-changing requests carry a stable client operation ID when the linked contract requires replay. Read requests do not need one. | As written in the linked contracts |
| Server time | Where a contract already requires a timestamp, it is RFC 3339 UTC. Durations use integer milliseconds. A timestamp does not replace a revision or an idempotency key. | As written in the linked contracts |

Examples in the linked contracts use the accepted core-journey routes. The session credential encoding is `COM-019` in the login contract. The push channel is not an accepted route.

## Common JSON error (proposal)

```json
{
  "schema": "fl.error/2",
  "code": "identity_conflict",
  "message": "Request conflicts with an existing operation",
  "retryable": false,
  "operation_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab"
}
```

| Field | Type and rule |
|---|---|
| `schema` | Required literal `fl.error/2`. |
| `code` | Required stable machine-readable enum; clients branch on this, not `message`. Each operation lists applicable codes. |
| `message` | Optional diagnostic text for logs; never displayed directly on a device. No credential, private media, hostname, or stack trace. |
| `retryable` | Required boolean describing whether repeating the *same* request may succeed. It does not authorize an automatic retry. |
| `operation_id` | Optional echo when the failed request carried one. |

Proposed common codes: `unauthenticated`, `session_expired`, `forbidden`, `not_found`, `invalid_request`, `unsupported_schema`, `identity_conflict`, `operation_expired`, `recipient_unavailable`, `revision_conflict`, `rate_limited`, `storage_unavailable`, and `temporarily_unavailable`. An operation must map each code to its own visible behavior. The HTTP status for each code used by login and send is login `COM-020`. Servers omit `Retry-After`. Clients ignore that header and use the waits already accepted for part upload, connecting, and PIN. Logs omit the PIN and both Bearer credentials.

## Invariants and follow-on work

- A user-scoped request must not gain authority merely by naming a valid user ID.
- A response must be applied only to the current endpoint user/session and operation. Sign-out or mute invalidates local acceptance of late results. This journey does not open a push channel.
- A credential older than 3 minutes since its last successful refresh, or since issuance if never refreshed, is `session_expired`. The signed-in endpoint stays on its current stage and refreshes in the background (`COM-017` in the login contract). Refresh failure does not return the endpoint to the roster.
- A server restart that drops the credential is a failed refresh. The endpoint stays signed in until that user mutes and completes PIN sign-in again.
- This journey does not use a push event. Inbox membership arrives by the inbox snapshot.

Owner-visible session meaning is decided in the login contract. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Accepted engineering wire | The credential is 32 random bytes, base64url, Bearer (`COM-019` in the login contract). A restart that drops it is a failed refresh | None for the byte format. Storage of the server-side record remains an implementation choice inside that rule |
| `COM-OPEN-002` | Accepted engineering wire for this journey | User-scoped calls use the issued session on the accepted routes. This journey does not open a push channel | None for this journey |
| `COM-OPEN-003` | Accepted engineering wire | HTTP status for login and send is login `COM-020`. Clients branch on `code`. Servers omit `Retry-After`. Clients ignore it. Logs omit the PIN and both Bearer credentials | None |

Revision 0.1 (2026-10-11): proposed a shared authority and error envelope based on gaps in the v1 implementation. No acceptance scenarios are implementation-ready until session semantics are decided.

Revision 0.2 (2026-10-21): recorded that PIN success issues a session and mute revokes it, per the login contract. Token format, expiration, and refresh remain OPEN. Added `operation_expired` to the common error codes.

Revision 0.3 (2026-10-21): moved `COM-OPEN-001`–`003` to follow-on work. No product behavior changed. No wire protocol is approved.

Revision 0.4 (2026-10-22): recorded the 3-minute credential bound and background refresh from the login contract. Token bytes and the refresh route stay follow-on. No wire protocol is approved.

Revision 0.5 (2026-10-22): owner confirmed that amendment checkpoint (login `DEC-072`). No wire protocol is approved.

Revision 0.6 (2026-10-22): accepted the 32-byte Bearer token, HTTPS for requests, and the login `COM-020` HTTP map. Routes and push authentication stayed follow-on at that revision. No implementation authorization.

Revision 0.7 (2026-10-22): accepted the core-journey routes listed in the login, inbox, and delivery contracts. Push authentication stayed follow-on at that revision. No implementation authorization.

Revision 0.8 (2026-10-22): accepted that this journey does not open a push channel. Inbox refresh is the inbox contract. No implementation authorization.

Revision 0.9 (2026-10-22): accepted the `schema` literals already written in this journey, with refresh request and success remaining `{}`. Servers omit `Retry-After`. Logs omit the PIN and both Bearer credentials. No implementation authorization.

Revision 0.10 (2026-10-22): the accepted part-upload acknowledgement is HTTP 200 `{}` with no `schema` field, same as refresh success. No implementation authorization.
