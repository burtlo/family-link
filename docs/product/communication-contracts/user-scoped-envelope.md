# Proposed shared envelope for user-scoped exchanges

| Field | Value |
|---|---|
| ID | `COM-USER-SCOPED-ENVELOPE` |
| Revision | 0.3 |
| Status | Engineering proposal; session issuance/revocation meaning decided in the login contract; token format is follow-on, not product OPEN |
| Scope | Send, inbox, media/playback state, profile |

## Why this is needed

The [v1 communication inventory](../../v1-assessments/communication-contract-inventory.md) found that v1 user-scoped routes accept an endpoint Bearer token and a caller-supplied `X-User-Id`; the server checks that the ID exists, but the route does not bind it to a server-issued signed-in session. A v2 message schema is incomplete until the server knows which signed-in user is acting. This draft proposes a shared session context without deciding the token format or storage implementation. Session issuance on PIN success and mute invalidation are decided in [endpoint health and login](endpoint-health-and-login.md) (`DEC-066`, `DEC-056`, `COM-013`, `COM-016`). Token format, expiration, and refresh are engineering follow-on (`COM-OPEN-001`).

## Proposed transport and authority

| Element | Proposed contract | Status |
|---|---|---|
| User-scoped authorization | A server-issued credential binds `endpoint_id`, `user_id`, and a session identifier. The server derives the actor from it; a request body or `X-User-Id` cannot choose the actor. Issuance on PIN success and mute revocation are decided in the login contract. | PROPOSED token format; issuance/revocation meaning decided |
| Transport | Authenticated HTTPS requests for reads/writes and an authenticated push channel for change hints. | PROPOSED; exact protocol/path OPEN |
| Schema version | Every JSON request/response/event has a `schema` discriminator with a versioned value. Unknown required schema versions fail explicitly. | PROPOSED |
| Operation identity | State-changing requests carry a stable client operation ID when lost-response replay matters. Read requests do not need one. | PROPOSED; per-operation scope in linked contracts |
| Server time | Server timestamps use RFC 3339 UTC; durations and positions use integer milliseconds. Timestamps never substitute for revision or idempotency keys. | PROPOSED |

Examples in the linked contracts use conceptual `POST /v2/...` paths to make direction and payloads reviewable. They are **not approved routes**. The `<session credential>` placeholder is not a token format.

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
| `schema` | Required literal `fl.error/2` in this proposal. |
| `code` | Required stable machine-readable enum; clients branch on this, not `message`. Each operation lists applicable codes. |
| `message` | Optional diagnostic text for logs; never displayed directly on a device. No credential, private media, hostname, or stack trace. |
| `retryable` | Required boolean describing whether repeating the *same* request may succeed. It does not authorize an automatic retry. |
| `operation_id` | Optional echo when the failed request carried one. |

Proposed common codes: `unauthenticated`, `session_expired`, `forbidden`, `not_found`, `invalid_request`, `unsupported_schema`, `identity_conflict`, `operation_expired`, `revision_conflict`, `rate_limited`, `storage_unavailable`, and `temporarily_unavailable`. An operation must map each code to its own visible behavior. HTTP status mapping and retry-after representation remain engineering follow-on.

## Invariants and follow-on work

- A user-scoped request must not gain authority merely by naming a valid user ID.
- A response or push event must be applied only to the current endpoint user/session and operation. Sign-out or mute invalidates local acceptance of late results.
- A push event is a hint to fetch authoritative state unless a linked contract explicitly defines it as a complete snapshot.

Owner-visible session meaning is decided in the login contract. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Engineering wire | Issuance on PIN success and mute revocation are decided in the login contract | Credential bytes, expiration, refresh, and behavior after server restart |
| `COM-OPEN-002` | Engineering wire | User-scoped calls must use the issued session; a body or `X-User-Id` cannot choose the actor | Final transport/routes, TLS deployment model, and push authentication |
| `COM-OPEN-003` | Engineering wire | Clients branch on stable `code` values already listed; device UI must not show `message` | HTTP mapping, retry-after fields, and privacy/logging of diagnostics |

Revision 0.1 (2026-10-11): proposed a shared authority and error envelope based on gaps in the v1 implementation. No acceptance scenarios are implementation-ready until session semantics are decided.

Revision 0.2 (2026-10-21): recorded that PIN success issues a session and mute revokes it, per the login contract. Token format, expiration, and refresh remain OPEN. Added `operation_expired` to the common error codes.

Revision 0.3 (2026-10-21): moved `COM-OPEN-001`–`003` to follow-on work. No product behavior changed. No wire protocol is approved.
