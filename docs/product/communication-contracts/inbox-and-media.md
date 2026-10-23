# Inbox, media, and playback-state communication contract

| Field | Value |
|---|---|
| ID | `COM-INBOX-MEDIA` (`COM-INBOX-SYNC` and `COM-MESSAGE-MEDIA-STATE` scopes) |
| Revision | 0.9 |
| Status | Carousel behavior accepted. Inbox routes, media bytes, monotonic revision, the 262144-byte snapshot, sender presentation on each item, ordinary and recovery fetches, the 1-second last-viewed coalesce, the 60-second inbox fetch, prefix playback, and media results after deletion or a dead session are accepted engineering wire. Inbox refresh runs while a finished message streams |
| Approval | Inbox, media, and last-viewed routes are accepted engineering wire. No implementation authorization implied |
| Related behavior | [`EXP-MESSAGE-CAROUSEL`](../experiences/message-carousel-and-playback.md) `DEC-004`, `DEC-005`, `DEC-018`, `DEC-028`, `DEC-029`, `DEC-033`–`DEC-035`, `DEC-037`, `DEC-041`–`DEC-043`; [carousel and playback assessment](../../v1-assessments/carousel-playback.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

`GET /v2/inbox`, `GET /v2/messages/{message_id}/media/audio`, `GET /v2/messages/{message_id}/media/sketch`, and `PUT /v2/inbox/last-viewed` are accepted routes. The session credential is login `COM-019`. Audio and sketch bytes match [message delivery](message-delivery.md).

## Boundary and v1 capture

The server owns inbox membership, message metadata, media access, and last-viewed identity. The client owns current selection, active playback, and same-session play heads. A push notification is a reason to reconcile, not proof that an item is playable. This contract covers `OP-INBOX-SYNC`, `OP-MEDIA-FETCH`, and `OP-LAST-VIEWED`. It does not persist pause position, sketch progress, or unread flags for the carousel experience (`DEC-028`, `DEC-043`). A finished send is published only into frozen recipients' inboxes; the sender does not receive a self-copy ([message delivery](message-delivery.md) `COM-012`, `DEC-041`).

## Proposed checkable rules

| ID | Invariant | V1 evidence / active source |
|---|---|---|
| `COM-001` | A validated snapshot is authoritative. This journey does not open a push channel. The ordinary fetch is `GET /v2/inbox` every 60 seconds while the carousel is showing and the desk is not in connecting or `no Wi-Fi`. Connectivity return and a successful session refresh each add one fetch; those two triggers also run during connecting (`COM-011`). A finished message streaming from storage does not delay these fetches. Health beats and session refresh continue. | Snapshot authority and poll accepted 2026-10-22; stream no longer suspends the fetch (`EXP-MESSAGE-RECORD-SEND` `DEC-051`) |
| `COM-002` | Apply a complete snapshot only when its schema, user/session, and revision are valid; stale responses cannot replace newer state. | V1 parser clears current list before full validation. |
| `COM-003` | A stable message ID binds list entry, media, active playback, and last-viewed writes. | V1 recipient sequence routes and newer global message ID coexist. |
| `COM-004` | Media fetch validates authenticated recipient access and cannot report success for unavailable media. Hash mismatch is a typed failure, shown as `can't play` when the inbox is reachable (`DEC-033`). | V1 per-user media lookup. |
| `COM-005` | Last-viewed from a snapshot or from another endpoint MUST NOT change this endpoint's selection while it stays signed in (`DEC-041`, `EXP-038`). Last-viewed is applied only on first entry after login (`DEC-018`). | Owner-decided carousel meaning |
| `COM-006` | While connecting or `no Wi-Fi` is showing, the client MUST NOT start or continue audio from a buffer, cache, or earlier fetch (`DEC-042`, `EXP-039`). | Owner-decided; v1 offline-blob policy is not adopted |
| `COM-007` | `OP-LAST-VIEWED` writes only the settled selected message. The client writes after that selection has stayed still for 1 second. A lost-response retry of the same `operation_id` replays that result for 60 seconds and MUST NOT move selection backward (`DEC-043`, `EXP-040`). | Owner-decided coalesce; 1-second quiet and 60-second retention accepted 2026-10-22 |
| `COM-008` | This experience MUST NOT write `position_ms`, sketch progress, or unread/read flags (`DEC-028`, `DEC-043`). | Owner-decided; v1 read/position writes are not adopted |
| `COM-009` | A new-mail snapshot MUST add cards on the right without moving the current selection (`DEC-005`). A deleted selected identity uses the carousel deletion fallback (`DEC-025`). | Owner-decided carousel membership rules |
| `COM-010` | Signed-in carousel readiness requires one complete inbox body of at most 262144 bytes, with `has_more` false and `next_cursor` null (`EXP-035`). A larger body or `has_more` true is not ready. | Owner-decided full-inbox reach; byte ceiling accepted 2026-10-22 |
| `COM-011` | A malformed snapshot, unknown schema, body over 262144 bytes, `has_more` true, timeout, connection reset, HTTP 5xx, or HTTP 401/403 enters connecting even when a valid inbox remains cached. The client keeps that membership and selection, does not show an empty inbox, and does not autoplay. While connecting and Wi-Fi is up, the client retries `GET /v2/inbox` on the login connecting schedule: a 3-second timeout, then waits of 1, 2, and 4 seconds, then every 30 seconds. `no Wi-Fi` sends no inbox request. Connectivity return, and a successful session refresh, each send one inbox fetch even while connecting is showing. A finished-message stream does not delay these fetches. A valid body whose `inbox_revision` is equal to or higher than the cached revision clears connecting. An equal revision does not add, remove, or reorder membership and does not move selection. A lower revision is ignored and does not clear connecting. The same schedule is the only inbox fetch when no valid snapshot exists yet. | Oversized-body connecting already accepted; recovery fetch runs during a finished-message stream (`DEC-051`) |
| `COM-012` | Each inbox item may include the sender's current `sender_avatar_id` and `sender_card_background_id`. Missing or unrecognized values use initials and `card-bg-01`. `profile_accent` is not a field on the item. These fields paint the card while the recipient stays signed in, including when the sender is absent from the current hangout roster. A change to avatar or card background does not by itself advance `inbox_revision`. The next valid snapshot, including one with the same revision, applies the new sender name and presentation on matching message ids. | Profile `COM-005`, `COM-010`; carousel `EXP-045` |

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

The JSON object above is not an accepted route. This journey does not open a push channel. New mail arrives through `GET /v2/inbox` on the schedule in `COM-001` and `COM-011`.

`inbox_revision` only increases for one user, including across a server restart. After revision 40, the next membership change is 41 or higher. A lower revision does not replace membership. An equal revision is a successful readiness result and refreshes sender presentation without replacing membership (`COM-011`, `COM-012`). A gap fetches `GET /v2/inbox`.

### 2. Inbox snapshot — `GET /v2/inbox` (accepted route)

A ready inbox is one JSON body (`COM-010`). `has_more` is false and `next_cursor` is null. The body is at most 262144 bytes. A larger body is not applied: the client keeps the last valid inbox, shows connecting, and continues recovery fetches (`COM-011`). The server does not omit older cards to stay under that ceiling.

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
      "sender_avatar_id": "avatar-03",
      "sender_card_background_id": "card-bg-03",
      "received_at": "2026-10-11T18:00:00Z",
      "kind": "audio_sketch",
      "duration_ms": 3000,
      "media": {
        "audio": {"format_id": "pcm_s16le", "byte_count": 96000, "sha256": "<64 lowercase hex characters>"},
        "sketch": {"format_id": "flsk2", "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
      }
    }
  ],
  "next_cursor": null,
  "has_more": false
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.inbox-page/2`. |
| `inbox_revision` | Required non-negative integer; server-owned. It only increases for that user, including across restart. A duplicate or lower value does not replace newer membership. |
| `total_count` | Required non-negative integer for the full inbox. Must equal `items.length` when `has_more` is false. |
| `last_viewed_message_id` | Nullable stable message ID. Used only on first signed-in entry (`COM-005`). If null or not in `items` after a complete snapshot, select newest (`DEC-018`). |
| `items` | Required array of complete summaries, oldest first (carousel left). IDs unique. Empty array is valid empty inbox. |
| `message_id` | Required stable server ID. Not an authorization key by itself; access comes from the session. |
| `sender_user_id`, `sender_name` | Required. Name is taken from the current snapshot for `From [sender name]`. A later valid snapshot, including an equal `inbox_revision`, may update the displayed name without changing selection. Frozen-at-send names are not required. |
| `sender_avatar_id` | Optional string. Absent, empty, or unrecognized: the card shows initials and stays selectable (`COM-012`). |
| `sender_card_background_id` | Optional string. Known values are `card-bg-01` through `card-bg-08`. Absent, empty, or unrecognized: the card shows `card-bg-01` (`COM-012`). |
| `kind` | Required enum `audio` or `audio_sketch`. |
| `duration_ms` | Required non-negative integer milliseconds for audio. |
| `media` | Required descriptor. Each present asset needs format ID, non-negative byte count, SHA-256. Absent sketch is omitted, not a false flag. |
| `next_cursor`, `has_more` | A ready snapshot has `has_more` false and `next_cursor` null. The whole JSON body is at most 262144 bytes. |

`profile`, `profile_accent`, `unread_count`, `read`, and `position_ms` are **absent**. V1 carried them; this experience does not (`COM-008`). Apply a snapshot only after validating the whole body for the current user/session. A lower `inbox_revision` cannot replace newer membership and does not update presentation. An equal revision updates sender name and sender presentation on matching ids and does not add, remove, or reorder cards (`COM-011`, `COM-012`). After first entry, a higher revision updates membership using `COM-009` and MUST NOT apply `last_viewed_message_id` to selection (`COM-005`).

### 3. Media fetch — `GET /v2/messages/{message_id}/media/{audio|sketch}` (accepted route)

The response body is bytes, not JSON. Codec and sketch layout are the delivery contract (`pcm_s16le`, `flsk2`). Sketch points are stored in nondecreasing `t_ms` order. The desk keeps the audio and sketch bytes received during this signed-in session. Pause and resume use that store and the local play head. When the store already covers the saved position, Play does not wait on a download.

Media GET accepts `Range: bytes=<first-missing>-` and answers 206 with `Content-Range`. A request with no Range returns 200 and the full body. Audio may start once a contiguous prefix from byte 0 covers 1 second (32000 bytes), or the whole message if it is shorter. The rest continues in the background. A sketch prefix is the drawing up to the buffered audio time. If the prefix runs out, playback holds at the last stored sample and the carousel stays up. A failed fetch while the inbox is reachable pauses at the last good position and shows `can't play`. The snapshot SHA-256 is checked when the full asset is stored. A mismatch then uses that same `can't play` pause.

A missing message for this user is HTTP 404 `not_found`. If the latest valid snapshot no longer lists that message, the client uses deletion fallback. If the snapshot still lists it, the client shows `can't play`. HTTP 401 or HTTP 403 discards in-flight bytes and uses connecting. Mute discards in-flight bytes and the stored media for this session, and does not play them. A sketch-only failure does not abort audio (`DEC-035`). Do not fetch or play media while connecting or `no Wi-Fi` is showing (`COM-006`).

### 4. Last-viewed update — `PUT /v2/inbox/last-viewed` (accepted route)

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

The client writes after the selected message has stayed still for 1 second. That write uses one `operation_id`. A later settled message uses a new `operation_id`. The server derives the user from the session, verifies inbox membership, and stores one last-viewed identity per user. It remembers an `operation_id` result for 60 seconds. An exact duplicate during that window replays the result and does not change last-viewed again. The same `operation_id` with a different `message_id` is `identity_conflict` and must not change last-viewed. If the message is gone, `not_found`; the client already applied deletion fallback and will write the new identity as a new operation. Server last-write-wins for the stored value used at the **next** login. A successful write MUST NOT be applied as a live selection change on any already-signed-in endpoint (`COM-005`).

## Failure and continuity

| Condition | Protocol and client result |
|---|---|
| Ordinary inbox refresh | While the carousel is showing and the desk is not in connecting or `no Wi-Fi`: `GET /v2/inbox` every 60 seconds. Connectivity return and a successful session refresh each add one fetch, including while connecting is showing. A finished message streaming from storage does not delay the fetch. Do not autoplay. Do not steal live selection (`COM-001`, `COM-005`, `COM-009`, `COM-011`). |
| Snapshot malformed, unknown schema, over 262144 bytes, `has_more` true, timeout, connection reset, HTTP 5xx, or HTTP 401/403 | Reject as a whole; keep last valid membership and selection. Do not show empty inbox from a bad body. Enter connecting. Retry on the login 3-second / 1/2/4/30 schedule while Wi-Fi is up and inbox fetches are not suspended (`COM-011`). A later valid equal or higher revision clears connecting. |
| Lower `inbox_revision` | Ignore the body. Do not replace membership, do not change selection, and do not enter connecting. |
| Selected item missing from a valid newer snapshot | Pause if playing; apply deletion fallback; write the new last-viewed as a new operation (`COM-009`). |
| Media fetch fails or the full-asset hash mismatches while inbox reachable | Pause at the last good position; `can't play` for 2 seconds; do not mark complete. Audio already played from a valid prefix stays played (`COM-004`, `DEC-034`). |
| Message gone from this user's inbox | HTTP 404 `not_found`. Deletion fallback when the latest valid snapshot omits it. `can't play` when the snapshot still lists it. |
| HTTP 401 or HTTP 403 on media | Discard in-flight bytes. Connecting. Do not play those bytes. |
| Mute | Reject late snapshots and media. Discard in-flight media bytes and do not play them. Last-viewed already stored remains for the next login. Play heads are discarded locally. |
| Wi-Fi down or server unreachable | Do not play cached audio (`COM-006`). Last-viewed write is best-effort and must not block pause or status. |
| Last-viewed `identity_conflict` or expired operation | Do not change live selection; log; next settled selection gets a new `operation_id`. |

| Scenario | Event | Expected check | Rules |
|---|---|---|---|
| `COM-SCN-001` | Carousel is showing; 60 seconds pass with no push channel | One `GET /v2/inbox`. A new card joins on the right. Selection stays. No autoplay | COM-001, COM-009 |
| `COM-SCN-011` | Media GET for a message the latest snapshot no longer lists | HTTP 404 `not_found`. Deletion fallback. In-flight bytes are not played | COM-009 |
| `COM-SCN-012` | Media GET returns HTTP 401 or HTTP 403, or mute happens during the fetch | In-flight bytes are discarded and not played. HTTP 401 or 403 shows connecting. Mute stays signed out | COM-006 |
| `COM-SCN-013` | Play on a 3-minute message | Audio starts from the first 32000 bytes. The remainder continues in the background. Pause and resume use the stored prefix and the local play head | COM-004 |
| `COM-SCN-014` | Maya is on the inbox while a finished message streams | The 60-second `GET /v2/inbox` still runs. The stream continues | COM-001 |
| `COM-SCN-002` | Malformed snapshot arrives during playback | Keep last validated collection and active message ID; do not render empty inbox | COM-002–003 |
| `COM-SCN-003` | Snapshot for a previous signed-in user arrives late | Reject without changing current focus or inbox | COM-002 |
| `COM-SCN-004` | Fetched audio hash differs from the descriptor after a prefix has already played | Pause the remainder; `can't play`; inbox stays reachable. Already played audio is not undone | COM-004 |
| `COM-SCN-005` | Bedroom last-viewed write arrives while kitchen is signed in on Sam | Server last-viewed updates; kitchen selection stays on Sam | COM-005 |
| `COM-SCN-006` | New mail arrives on a complete snapshot | New card joins on the right; selection unchanged | COM-009–010 |
| `COM-SCN-007` | Wi-Fi drops with earlier-fetched audio in memory | Do not continue playback; Play disabled | COM-006 |
| `COM-SCN-008` | Rapid swipes Ari→Sam→Jordan; the selection stays on Jordan for 1 second; that write retries within 60 seconds | One write, for Jordan. The retry replays Jordan. The carousel stays on Jordan | COM-007 |
| `COM-SCN-009` | Kitchen holds Maya at revision 40. The server restarts. Jordan’s message is the next inbox change | The snapshot revision is greater than 40 and includes Jordan. A revision of 1 is ignored | COM-002, COM-010 |
| `COM-SCN-010` | Inbox response is larger than 262144 bytes, or `has_more` is true | Keep the last valid inbox and show connecting. Do not drop older cards. Recovery fetches continue while Wi-Fi stays up | COM-010, COM-011 |
| `COM-SCN-015` | Valid revision 40 is showing. The next body is oversized. Wi-Fi never drops. A later body is valid revision 40 | Connecting shows, then clears. Selection stays. No autoplay. Membership is not replaced by the equal revision | COM-011 |
| `COM-SCN-016` | Jordan stays signed in. Maya's message is in his inbox. Maya changes `card_background_id` from `card-bg-03` to `card-bg-05`, and Maya is no longer on the hangout roster | The next valid snapshot carries Maya's new `sender_card_background_id` on that message. Jordan's card uses it. Selection stays. `profile_accent` is not on the item | COM-012 |

Owner-visible inbox and playback behavior in this contract is decided. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. These are engineering protocol choices.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Accepted engineering wire | `inbox_revision` only increases, including across restart. This journey does not open a push channel. `GET /v2/inbox` runs every 60 seconds while the carousel is showing, including while a finished message streams | None for this journey |
| `COM-OPEN-002` | Accepted engineering wire | One body, `has_more` false, `next_cursor` null, at most 262144 bytes. A larger body keeps the last valid inbox, shows connecting, and recovery fetches continue (`COM-011`) | None |
| `COM-OPEN-003` | Accepted engineering wire | Playback may start from a stored prefix of 32000 bytes. `Range: bytes=<first-missing>-` returns 206. Sketch points are in nondecreasing `t_ms` order | None |
| `COM-OPEN-004` | Accepted engineering wire | Write after the selection has stayed still for 1 second. Remember `operation_id` for 60 seconds | None |
| `COM-OPEN-005` | Accepted engineering wire | HTTP 404 `not_found` when this user no longer has the message. HTTP 401 or 403 discards in-flight bytes and uses connecting. Mute discards in-flight bytes | None for deletion and session loss |

Revision 0.1 (2026-10-11): captured v1 notification, snapshot, media, and state-write shapes and proposed versioned reconciliation and validated writes. No v2 behavior approval claimed.

Revision 0.2 (2026-10-21): recorded owner-accepted last-viewed live-steal, no cached play during status, and coalesced last-viewed-only writes. Replaced v1 read/position playback-state with `OP-LAST-VIEWED`. Remaining OPEN items are revision durability, snapshot chunking, media formats, coalesce interval, and post-deletion media access. No wire protocol is approved.

Revision 0.3 (2026-10-21): moved `COM-OPEN-001`–`005` to follow-on work. No product behavior changed. No wire protocol is approved.

Revision 0.4 (2026-10-22): accepted `GET /v2/inbox`, the two media routes, and `PUT /v2/inbox/last-viewed`. Audio and sketch bytes match message delivery. Snapshot bounds and push ordering stayed follow-on at that revision. No implementation authorization.

Revision 0.5 (2026-10-22): accepted a monotonic `inbox_revision` across restart, a single inbox body of at most 262144 bytes, and a 1-second last-viewed coalesce with 60-second `operation_id` retention. The push path stayed follow-on at that revision. No implementation authorization.

Revision 0.6 (2026-10-22): accepted no push channel on this journey, a 60-second inbox fetch, and media HTTP 404 / 401 / 403 / mute handling. Waiting for a whole media body before playback was not accepted. No implementation authorization.

Revision 0.7 (2026-10-22): accepted prefix playback with `Range` and a 32000-byte start. The 60-second inbox fetch is suspended during recording and while a finished send is pending. Recording stays local until finish. No implementation authorization.

Revision 0.8 (2026-10-22): separated ordinary polling from recovery polling while connecting, applied equal-revision snapshots to sender presentation without replacing membership, and added optional `sender_avatar_id` and `sender_card_background_id` on each inbox item. A late hash mismatch pauses the remainder. No implementation authorization.

Revision 0.9 (2026-10-23): inbox refresh runs while a finished message streams from storage. No implementation authorization.
