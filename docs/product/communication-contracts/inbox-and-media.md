# Inbox, media, and playback-state communication contract

| Field | Value |
|---|---|
| ID | `COM-INBOX-MEDIA` (`COM-INBOX-SYNC` and `COM-MESSAGE-MEDIA-STATE` scopes) |
| Revision | 0.3 |
| Status | V1 evidence plus v2 schema proposal; carousel last-viewed and outage meanings accepted; routes remain a labeled proposal |
| Approval | No v2 wire approved by this document |
| Related behavior | [`EXP-MESSAGE-CAROUSEL`](../experiences/message-carousel-and-playback.md) `DEC-004`, `DEC-005`, `DEC-018`, `DEC-028`, `DEC-029`, `DEC-033`–`DEC-035`, `DEC-037`, `DEC-041`–`DEC-043`; [carousel and playback assessment](../../v1-assessments/carousel-playback.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

Conceptual `GET /v2/...` and `PUT /v2/...` paths follow the shared envelope. They are **not approved routes**. The session credential is issued by [endpoint health and login](endpoint-health-and-login.md).

## Boundary and v1 capture

The server owns inbox membership, message metadata, media access, and last-viewed identity. The client owns current selection, active playback, and same-session play heads. A push notification is a reason to reconcile, not proof that an item is playable. This contract covers `OP-INBOX-SYNC`, `OP-MEDIA-FETCH`, and `OP-LAST-VIEWED`. It does not persist pause position, sketch progress, or unread flags for the carousel experience (`DEC-028`, `DEC-043`). A finished send is published only into frozen recipients' inboxes; the sender does not receive a self-copy ([message delivery](message-delivery.md) `COM-012`, `DEC-041`).

## Proposed checkable rules

| ID | Invariant | V1 evidence / active source |
|---|---|---|
| `COM-001` | Push is a hint; an authenticated, validated snapshot is authoritative. | V1 push triggers full reload; [carousel assessment](../../v1-assessments/carousel-playback.md). |
| `COM-002` | Apply a complete snapshot only when its schema, user/session, and revision are valid; stale responses cannot replace newer state. | V1 parser clears current list before full validation. |
| `COM-003` | A stable message ID binds list entry, media, active playback, and last-viewed writes. | V1 recipient sequence routes and newer global message ID coexist. |
| `COM-004` | Media fetch validates authenticated recipient access and cannot report success for unavailable media. Hash mismatch is a typed failure, shown as `can't play` when the inbox is reachable (`DEC-033`). | V1 per-user media lookup. |
| `COM-005` | Last-viewed from a snapshot or from another endpoint MUST NOT change this endpoint's selection while it stays signed in (`DEC-041`, `EXP-038`). Last-viewed is applied only on first entry after login (`DEC-018`). | Owner-decided carousel meaning |
| `COM-006` | While connecting or `no Wi-Fi` is showing, the client MUST NOT start or continue audio from a buffer, cache, or earlier fetch (`DEC-042`, `EXP-039`). | Owner-decided; v1 offline-blob policy is not adopted |
| `COM-007` | `OP-LAST-VIEWED` writes only the settled selected message. Rapid selection changes coalesce to one write. A lost-response retry of the same `operation_id` replays that result and MUST NOT move selection backward (`DEC-043`, `EXP-040`). | Owner-decided; client standard §9 coalesced remote writes |
| `COM-008` | This experience MUST NOT write `position_ms`, sketch progress, or unread/read flags (`DEC-028`, `DEC-043`). | Owner-decided; v1 read/position writes are not adopted |
| `COM-009` | A new-mail snapshot MUST add cards on the right without moving the current selection (`DEC-005`). A deleted selected identity uses the carousel deletion fallback (`DEC-025`). | Owner-decided carousel membership rules |
| `COM-010` | Signed-in carousel readiness requires the current inbox to be fully represented before cards are treated as ready (`EXP-035`). Internal chunked fetch is allowed; `has_more` remaining is not a ready view of a partial timeline. | Owner-decided full-inbox reach |

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Push | Authenticated endpoint WebSocket sends `{"type":"inbox","user_id":string,"seq":integer,"kind":string,"from":string}` to all endpoints after a commit. | **Adapt as a hint.** Add a durable change/revision mechanism so a missed push can be recovered; scope delivery to the acting session. |
| Inbox snapshot | Login response and `GET /v1/inbox` contain `user_id`, `last_viewed_seq`, `unread`, `profile`, `messages`; each message has `seq`, `kind`, `from`, `from_label`, `system`, `read`, `position_ms`, `created_at`, `has_sketch`, optional `duration_ms`, `message_id`, `broadcast_id`, `audio_codec`. Default is newest eight, in ascending sequence within the page; optional `limit` 1–16 and `before_seq` expose older pages. | **Replace login-bundled inbox** (login issues a session only). **Replace** default eight-item page as the user-visible inbox (`EXP-035`). **Retain** message metadata needed for cards. Add snapshot validity and revision. **Do not** treat `read` / `position_ms` / `unread` as carousel facts. Profile has its own contract. |
| Media | `GET /v1/messages/{seq}/blob` returns WAV; `/{seq}/sketch` returns FLSK1 bytes. Newer message-ID audio route exists server-side with hash headers; firmware still uses recipient sequence routes. | **Adapt to one stable message identity** and media descriptors with format and integrity metadata. |
| Playback writes | `PUT /v1/session/view` with `seq`; `PUT /v1/messages/{seq}/read` and `/position` with `position_ms`. Server persists values but does not validate sequence ownership before writing. | **Replace** with last-viewed-only writes. Do not persist position or read for this experience. Validate that the message belongs to the authenticated session. |

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

### 2. Inbox snapshot — `GET /v2/inbox` (proposed)

This experience treats a **complete current inbox** as readiness (`COM-010`). Query cursors may exist for transport chunking; the carousel is not ready while `has_more` is true.

```json
{
  "schema": "fl.inbox-page/2",
  "inbox_revision": 42,
  "total_count": 12,
  "last_viewed_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "items": [
    {
      "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
      "sender_user_id": "user-a",
      "sender_name": "Ari",
      "received_at": "2026-10-11T18:00:00Z",
      "kind": "audio_sketch",
      "duration_ms": 3000,
      "media": {
        "audio": {"format_id": "pcm_s16le-wav", "byte_count": 96044, "sha256": "<64 lowercase hex characters>"},
        "sketch": {"format_id": "<color-capable-format>", "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
      }
    }
  ],
  "next_cursor": null,
  "has_more": false
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.inbox-page/2` in this proposal. |
| `inbox_revision` | Required non-negative integer; server-owned. Durability across restart is `COM-OPEN-001`. |
| `total_count` | Required non-negative integer for the full inbox. Must equal `items.length` when `has_more` is false. |
| `last_viewed_message_id` | Nullable stable message ID. Used only on first signed-in entry (`COM-005`). If null or not in `items` after a complete snapshot, select newest (`DEC-018`). |
| `items` | Required array of complete summaries, oldest first (carousel left). IDs unique. Empty array is valid empty inbox. |
| `message_id` | Required stable server ID. Not an authorization key by itself; access comes from the session. |
| `sender_user_id`, `sender_name` | Required. Name is taken from the current snapshot for `From [sender name]`. A later valid snapshot may update the displayed name without changing selection. Frozen-at-send names are not required. |
| `kind` | Required enum `audio` or `audio_sketch`. |
| `duration_ms` | Required non-negative integer milliseconds for audio. |
| `media` | Required descriptor. Each present asset needs format ID, non-negative byte count, SHA-256. Absent sketch is omitted, not a false flag. |
| `next_cursor`, `has_more` | For a ready carousel snapshot, `has_more` is false and `next_cursor` is null. |

`profile`, `unread_count`, `read`, and `position_ms` are **absent**. V1 carried them; this experience does not (`COM-008`). Apply a snapshot only after validating the whole body for the current user/session. A stale or lower `inbox_revision` cannot replace newer membership. After first entry, a newer snapshot updates membership using `COM-009` and MUST NOT apply `last_viewed_message_id` to selection (`COM-005`).

### 3. Media fetch — `GET /v2/messages/{message_id}/media/{audio|sketch}` (proposed)

The response body is bytes, not JSON. Required proposed response metadata: `Content-Type` appropriate to the selected format, `Content-Length` or a defined streaming equivalent, and `ETag` or SHA-256. A client validates the fetched bytes against the inbox descriptor before treating media as ready. A missing, forbidden, corrupt, hash-mismatched, or unsupported audio asset is a typed failure: `can't play` when the inbox is still reachable (`DEC-033`, `DEC-034`). A sketch-only failure does not abort audio (`DEC-035`). Do not fetch or play media while connecting or `no Wi-Fi` is showing (`COM-006`). Exact codec, color sketch format, and range headers remain `COM-OPEN-003`.

### 4. Last-viewed update — `PUT /v2/inbox/last-viewed` (proposed)

This is the only playback-related write in this experience (`COM-007`, `COM-008`).

```json
{
  "schema": "fl.last-viewed-update/2",
  "operation_id": "bfa8883e-a7f8-45bf-a2ef-cf2c7966ccf1",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.last-viewed-update/2`. |
| `operation_id` | Required UUID for this settled selection. Coalesced swipe bursts share one id. A later settled selection gets a new id. Retries reuse the same id and exact body. |
| `message_id` | Required. Must belong to the authenticated user's current inbox. |

Proposed result:

```json
{
  "schema": "fl.last-viewed/2",
  "operation_id": "bfa8883e-a7f8-45bf-a2ef-cf2c7966ccf1",
  "last_viewed_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"
}
```

The server derives the user from the session, verifies inbox membership, and stores one last-viewed identity per user. An exact duplicate `operation_id` replays this result without changing last-viewed again. The same `operation_id` with a different `message_id` is `identity_conflict` and must not change last-viewed. If the message is gone, `not_found`; the client already applied deletion fallback and will write the new identity as a new operation. Server last-write-wins for the stored value used at the **next** login. A successful write MUST NOT be applied as a live selection change on any already-signed-in endpoint (`COM-005`). Coalesce delay is `COM-OPEN-004`.

## Failure and continuity

| Condition | Protocol and client result |
|---|---|
| Push missed or reconnects | Fetch latest snapshot. Do not autoplay. Do not steal live selection (`COM-005`, `COM-009`). |
| Snapshot malformed or stale | Reject as a whole; keep last valid membership. Do not show empty inbox from a bad body. Signed-in connecting uses the login 3-second / 1/2/4/30 schedule if no valid snapshot remains. |
| Selected item missing from a valid newer snapshot | Pause if playing; apply deletion fallback; write the new last-viewed as a new operation (`COM-009`). |
| Media fetch fails or hash mismatches while inbox reachable | Typed error; `can't play` for 2 seconds; do not mark complete (`COM-004`). |
| Wi-Fi down or server unreachable | Do not play cached audio (`COM-006`). Last-viewed write is best-effort and must not block pause or status. |
| Last-viewed `identity_conflict` or expired operation | Do not change live selection; log; next settled selection gets a new `operation_id`. |
| Mute | Reject late snapshots and media. Last-viewed already stored remains for the next login. Play heads are discarded locally. |

| Scenario | Event | Expected check | Rules |
|---|---|---|---|
| `COM-SCN-001` | Push is duplicated or missed, then the channel reconnects | Client converges by revision without duplicate cards or autoplay | COM-001, COM-009 |
| `COM-SCN-002` | Malformed snapshot arrives during playback | Keep last validated collection and active message ID; do not render empty inbox | COM-002–003 |
| `COM-SCN-003` | Snapshot for a previous signed-in user arrives late | Reject without changing current focus or inbox | COM-002 |
| `COM-SCN-004` | Fetched audio hash differs from the descriptor | Do not play; `can't play`; inbox stays reachable | COM-004 |
| `COM-SCN-005` | Bedroom last-viewed write arrives while kitchen is signed in on Sam | Server last-viewed updates; kitchen selection stays on Sam | COM-005 |
| `COM-SCN-006` | New mail arrives on a complete snapshot | New card joins on the right; selection unchanged | COM-009–010 |
| `COM-SCN-007` | Wi-Fi drops with earlier-fetched audio in memory | Do not continue playback; Play disabled | COM-006 |
| `COM-SCN-008` | Rapid swipes Ari→Sam→Jordan; Ari write retries after Jordan settled | Server last-viewed is Jordan; carousel stays on Jordan | COM-007 |

Owner-visible inbox and playback behavior in this contract is decided. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Engineering wire | Live last-viewed must not steal selection (`COM-005`) | Durable inbox revision and push/reconnect ordering |
| `COM-OPEN-002` | Engineering wire | User-visible paging/archive is not adopted; a ready snapshot is complete (`COM-010`) | Chunk size and maximum snapshot bytes |
| `COM-OPEN-003` | Engineering wire | Offline/cached play during status is no (`COM-006`). Failure copy is decided | Audio codec, color sketch format, hashes, and range headers |
| `COM-OPEN-004` | Engineering wire | Last-viewed write rules are decided (`COM-007`, `COM-008`) | Coalesce interval and `operation_id` retention |
| `COM-OPEN-005` | Engineering implementation | Mute and deletion fallback are decided in the carousel experience | Media access after deletion or session invalidation, including an in-flight fetch |

Revision 0.1 (2026-10-11): captured v1 notification, snapshot, media, and state-write shapes and proposed versioned reconciliation and validated writes. No v2 behavior approval claimed.

Revision 0.2 (2026-10-21): recorded owner-accepted last-viewed live-steal, no cached play during status, and coalesced last-viewed-only writes. Replaced v1 read/position playback-state with `OP-LAST-VIEWED`. Remaining OPEN items are revision durability, snapshot chunking, media formats, coalesce interval, and post-deletion media access. No wire protocol is approved.

Revision 0.3 (2026-10-21): moved `COM-OPEN-001`–`005` to follow-on work. No product behavior changed. No wire protocol is approved.
