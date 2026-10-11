# Proposed shared envelope for user-scoped exchanges

| Field | Value |
|---|---|
| ID | `COM-USER-SCOPED-ENVELOPE` |
| Revision | 0.1 |
| Status | Engineering proposal for owner/security review; no v2 wire approval |
| Scope | Send, inbox, media/playback state, profile |

## Why this is needed

The [v1 communication inventory](../../v1-assessments/communication-contract-inventory.md) found that v1 user-scoped routes accept an endpoint Bearer token and a caller-supplied `X-User-Id`; the server checks that the ID exists, but the route does not bind it to a server-issued signed-in session. A v2 message schema is incomplete until the server knows which signed-in user is acting. This draft proposes a shared session context without deciding the token format or storage implementation. It also depends on the still-open session issuance and mute behavior in [endpoint health and login](endpoint-health-and-login.md) (`COM-OPEN-006`, `COM-OPEN-009`).

## Proposed transport and authority

| Element | Proposed contract | Status |
|---|---|---|
| User-scoped authorization | A server-issued credential binds `endpoint_id`, `user_id`, and a session identifier. The server derives the actor from it; a request body or `X-User-Id` cannot choose the actor. | PROPOSED; session issuance/revocation OPEN |
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

Proposed common codes: `unauthenticated`, `session_expired`, `forbidden`, `not_found`, `invalid_request`, `unsupported_schema`, `identity_conflict`, `revision_conflict`, `rate_limited`, `storage_unavailable`, and `temporarily_unavailable`. An operation must map each code to its own visible behavior. HTTP status mapping and retry-after representation remain OPEN.

## Invariants and OPEN items

- A user-scoped request must not gain authority merely by naming a valid user ID.
- A response or push event must be applied only to the current endpoint user/session and operation. Sign-out or mute invalidates local acceptance of late results.
- A push event is a hint to fetch authoritative state unless a linked contract explicitly defines it as a complete snapshot.

| ID | Decision needed | Blocks |
|---|---|---|
| `COM-OPEN-001` | Session credential issuance, expiration, refresh, server revocation on mute/sign-out, and behavior after restart. | All user-scoped exchanges |
| `COM-OPEN-002` | Final transport/routes, TLS deployment model, and push authentication. | Interoperability |
| `COM-OPEN-003` | Final error-code and HTTP mapping, retry-after fields, and privacy/logging policy. | Error handling |

Revision 0.1 (2026-10-11): proposed a shared authority and error envelope based on gaps in the v1 implementation. No acceptance scenarios are implementation-ready until session semantics are decided.
