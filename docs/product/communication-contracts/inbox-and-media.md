# Inbox, media, and playback-state communication contract

| Field | Value |
|---|---|
| ID | `COM-INBOX-MEDIA` (`COM-INBOX-SYNC` and `COM-MESSAGE-MEDIA-STATE` scopes) |
| Revision | 0.1 |
| Status | V1 evidence plus v2 schema proposal; product and protocol decisions OPEN |
| Approval | No v2 wire or offline behavior approved by this document |
| Related behavior | [Carousel and playback assessment](../../v1-assessments/carousel-playback.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

## Boundary and v1 capture

The server owns inbox membership, message metadata, media access, and persisted playback state. The client owns current focus and active playback. A push notification is a reason to reconcile, not proof that an item is playable. The schemas below are proposals for two linked operations: `OP-INBOX-SYNC` and `OP-MESSAGE-MEDIA-STATE`.

## Proposed checkable rules

| ID | Proposed invariant | V1 evidence / active source |
|---|---|---|
| `COM-001` | Push is a hint; an authenticated, validated snapshot is authoritative. | V1 push triggers full reload; [carousel assessment](../../v1-assessments/carousel-playback.md). |
| `COM-002` | Apply a complete page only when its schema, user/session, and revision are valid; stale responses cannot replace newer state. | V1 parser clears current list before full validation. |
| `COM-003` | A stable message ID binds list entry, media, active playback, and state writes. | V1 recipient sequence routes and newer global message ID coexist. |
| `COM-004` | Media and playback-state operations validate authenticated recipient access and cannot report success for unavailable media. | V1 per-user media lookup; state writes lack message ownership validation. |

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Push | Authenticated endpoint WebSocket sends `{"type":"inbox","user_id":string,"seq":integer,"kind":string,"from":string}` to all endpoints after a commit. | **Adapt as a hint.** Add a durable change/revision mechanism so a missed push can be recovered; scope delivery to the acting session. |
| Inbox snapshot | Login response and `GET /v1/inbox` contain `user_id`, `last_viewed_seq`, `unread`, `profile`, `messages`; each message has `seq`, `kind`, `from`, `from_label`, `system`, `read`, `position_ms`, `created_at`, `has_sketch`, optional `duration_ms`, `message_id`, `broadcast_id`, `audio_codec`. Default is newest eight, in ascending sequence within the page; optional `limit` 1–16 and `before_seq` expose older pages. | **Retain bounded paging and metadata.** Add explicit snapshot validity and version; avoid defaulting malformed fields or replacing good state with a partial parse. Profile has its own authority and revision. |
| Media | `GET /v1/messages/{seq}/blob` returns WAV; `/{seq}/sketch` returns FLSK1 bytes. Newer message-ID audio route exists server-side with hash headers; firmware still uses recipient sequence routes. | **Adapt to one stable message identity** and media descriptors with format and integrity metadata. |
| Playback writes | `PUT /v1/session/view` with `seq`; `PUT /v1/messages/{seq}/read` and `/position` with `position_ms`. Server persists values but does not validate sequence ownership before writing. | **Replace with validated, versioned state changes** and explicit conflict handling. |

## Proposed v2 schemas

### 1. Change hint — server → authenticated session (proposed)

```json
{
  "schema": "fl.inbox-hint/2",
  "type": "inbox_changed",
  "inbox_revision": 42,
  "latest_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"
}
```

`inbox_revision` is a strictly increasing server-owned value for one user's inbox. `latest_message_id` is optional and informational; it never replaces a snapshot. The push channel is bound to the authenticated user/session by the [shared envelope](user-scoped-envelope.md), so a supplied user ID in the event does not grant or redirect access. Duplicate or older revisions may be ignored; a revision gap or reconnect triggers snapshot reconciliation. The server's revision durability and exact event ordering are `COM-OPEN-001`.

### 2. Inbox page — `GET /v2/inbox?cursor=...&limit=...` (proposed)

```json
{
  "schema": "fl.inbox-page/2",
  "inbox_revision": 42,
  "total_count": 12,
  "unread_count": 2,
  "last_viewed_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "items": [
    {
      "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
      "recipient_sequence": 18,
      "sender_user_id": "user-a",
      "sender_name": "Ari",
      "received_at": "2026-10-11T18:00:00Z",
      "kind": "audio_sketch",
      "read": false,
      "position_ms": 0,
      "duration_ms": 3000,
      "media": {
        "audio": {"format_id": "pcm_s16le-wav", "byte_count": 96044, "sha256": "<64 lowercase hex characters>"},
        "sketch": {"format_id": "<color-capable-format>", "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
      }
    }
  ],
  "next_cursor": "<opaque cursor>",
  "has_more": true
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.inbox-page/2` in this proposal. |
| `inbox_revision` | Required non-negative integer; server-owned, durable across restart if used for reconciliation. |
| `total_count`, `unread_count` | Required non-negative integers with `unread_count <= total_count`; both describe the full inbox, not just this page. |
| `last_viewed_message_id` | Nullable stable message ID. If it is outside this page, the client must not invent a focused item from its position. |
| `items` | Required array of complete message summaries, ordered by ascending `recipient_sequence` within the page. IDs are unique within one inbox. Limit and maximum response bytes OPEN. |
| `message_id` | Required stable server ID. `recipient_sequence` is optional display/order metadata; it is not an authorization key. |
| `sender_user_id`, `sender_name` | Required identity/display snapshot in this proposal. Whether name follows profile changes or is frozen at send is OPEN. |
| `kind` | Required enum proposed as `audio` or `audio_sketch`; other message kinds require a versioned extension. |
| `read`, `position_ms`, `duration_ms` | Required boolean and non-negative integer milliseconds; `position_ms <= duration_ms` unless an explicitly defined live/incomplete message type is added. |
| `media` | Required descriptor for available assets. Each asset needs format ID, non-negative byte count, SHA-256; absent sketch is represented by no `sketch` member, not a false `has_sketch` alongside an unavailable URL. |
| `next_cursor`, `has_more` | Cursor is nullable and opaque; `has_more=false` requires `next_cursor=null`. Cursor must bind to user and snapshot/paging policy. |

`profile` is intentionally absent. The v1 inbox payload included it, but profile has a separate revision and may change without an inbox change. The client applies a new page only after validating the whole response and verifying it belongs to the current user/session. A stale page cannot overwrite a newer revision. Paging across a changing inbox needs a decided snapshot or cursor policy (`COM-OPEN-002`).

### 3. Media fetch — `GET /v2/messages/{message_id}/media/{audio|sketch}` (proposed)

The response body is bytes, not JSON. Required proposed response metadata: `Content-Type` appropriate to the selected format, `Content-Length` or a defined streaming equivalent, `ETag` or SHA-256, and range headers if byte ranges are supported. A client validates the fetched bytes against the inbox descriptor before treating media as ready. A missing, forbidden, corrupt, or unsupported asset is a typed failure, not an empty playable message. Exact audio codec, color sketch format, range/resume policy, cache lifetime, and offline availability are OPEN.

### 4. Playback-state update — `PUT /v2/messages/{message_id}/playback-state` (proposed)

```json
{
  "schema": "fl.playback-state-update/2",
  "operation_id": "bfa8883e-a7f8-45bf-a2ef-cf2c7966ccf1",
  "expected_revision": 7,
  "read": true,
  "position_ms": 1500,
  "set_last_viewed": true
}
```

The server verifies that the current authenticated user owns this inbox item, validates `position_ms` against known duration, and returns the complete persisted state:

```json
{
  "schema": "fl.playback-state/2",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "revision": 8,
  "read": true,
  "position_ms": 1500,
  "last_viewed_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"
}
```

`read`, `position_ms`, and `set_last_viewed` are optional patch fields, but at least one must be present. `expected_revision` prevents an old write from silently replacing newer state; a mismatch returns `revision_conflict` with current state or a safe refetch instruction. The client reuses `operation_id` when retrying the same write after a lost response. Whether read may ever revert to false, how position resets/replays work, and when writes occur are product decisions (`COM-OPEN-004`).

## Failure and continuity rules to decide

| Condition | Proposed protocol result | Product behavior still needed |
|---|---|---|
| Push missed or WebSocket reconnects | Fetch an authoritative page at latest revision. | Whether to notify user, preserve focus, or autoplay a newly found message. |
| Page malformed or stale | Reject as a whole; keep last valid snapshot labeled appropriately. | Offline/stale presentation policy. |
| Focused item disappears from new snapshot | Keep active playback bound to its message ID until its operation resolves or is canceled. | Removal and navigation policy. |
| Media fetch fails or hash mismatches | Typed error; do not mark playback as successful. | Copy, retry, caching, and read-state effect. |
| Playback-state write conflicts | Refetch current state; never silently overwrite newer server state. | Merge rule for position and read state. |

| Scenario | Event | Proposed check | Remaining dependency |
|---|---|---|---|
| `COM-SCN-001` | Push is duplicated or missed, then channel reconnects | Client converges by revision and snapshot without duplicate cards. | COM-001; revision recovery OPEN |
| `COM-SCN-002` | Malformed page arrives during playback | Keep last validated collection and active message ID; do not render empty inbox. | COM-002–003; stale presentation OPEN |
| `COM-SCN-003` | Page for previous signed-in user arrives late | Reject without changing current user's focus or inbox. | COM-002; shared session envelope |
| `COM-SCN-004` | Fetched media hash differs from descriptor | Do not play or mark successful; expose typed failure. | COM-004; copy/retry OPEN |
| `COM-SCN-005` | Position writes complete out of order | Older write cannot silently replace newer persisted state. | COM-004; merge policy OPEN |
| `COM-SCN-006` | New mail arrives while older pages are traversed | No duplicate/missing item after reconciliation. | COM-002; cursor policy OPEN |

| ID | Decision needed | Blocks |
|---|---|---|
| `COM-OPEN-001` | Durable inbox revision and push/reconnect reconciliation, including cross-endpoint delivery. | Hint schema |
| `COM-OPEN-002` | Paging/snapshot consistency, default page size, older-message UI, and focus when viewed item is outside page. | Page schema |
| `COM-OPEN-003` | Final media formats, range/resume, hashes, cache/offline playback, and failure presentation. | Fetch schema |
| `COM-OPEN-004` | Read threshold, position-save cadence, reset/merge semantics, last-viewed behavior, and whether state writes may be combined. | Playback-state schema |
| `COM-OPEN-005` | Message identity and access lifetime after deletion/removal or a user/session change. | Media and state authorization |

Revision 0.1 (2026-10-11): captured v1 notification, snapshot, media, and state-write shapes and proposed versioned reconciliation and validated writes. No v2 behavior approval claimed.
