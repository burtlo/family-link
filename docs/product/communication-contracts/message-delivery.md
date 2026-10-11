# Message delivery communication contract

| Field | Value |
|---|---|
| ID | `COM-MESSAGE-DELIVERY` |
| Revision | 0.1 |
| Status | V1 evidence plus v2 schema proposal; product and protocol decisions OPEN |
| Approval | No v2 wire or send behavior approved by this document |
| Related behavior | [Recording and send assessment](../../v1-assessments/recording-send.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

## Boundary and v1 capture

`OP-MESSAGE-CREATE`, `OP-MEDIA-CHUNK`, `OP-MESSAGE-COMPLETE`, and `OP-OUTGOING-RECEIPT` form one proposed logical send. The server owns acceptance and recipient delivery truth. The client owns captured work until it has a trustworthy acceptance receipt. Notification to recipients is a later, independent activity.

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Device upload | `POST /v1/messages` multipart: `kind=audio`, `to_user_id` **or** `broadcast`, WAV `blob`, optional FLSK1 `sketch`; endpoint Bearer + `X-User-Id`. The device sends no `client_message_id`. | **Replace device send identity and whole-body dependency.** Preserve support for audio plus optional drawing as product input, not this wire representation. |
| Server multipart result | `{"messages":[{"seq":number,"kind":string,"from":string,"to":string,"broadcast_id":string|null,"message_id":string},...]}` after archive commit. Optional `client_message_id` enables identical replay and 409 on content conflict server-side. | **Retain the accepted-receipt idea.** Use one logical client ID and a stable receipt that the device actually reads. |
| Existing receipt lookup | `GET /v1/outgoing/{client_message_id}` returns `schema=family-send-receipt/1`, `state=complete`, message and recipient/media metadata, or 404. | **Adapt** for lost-response recovery; define what 404 means before retry. |
| Server chunk subset | JSON `family-message/1` create; numbered PCM chunks with hashes/timing; status; complete; identical create/chunk/complete replay, conflicts 409. Firmware does not call it. | **Evaluate/adapt**, not automatic v2 choice. Server support is not proof of device feasibility or a color-capable sketch format. |

The v1 device sends separate requests for several selected recipients; an earlier recipient can receive the message before a later request fails. A v2 choice about atomicity is required before the proposed recipient-array schema below is accepted.

## Proposed checkable rules

| ID | Proposed invariant | V1 evidence / active source |
|---|---|---|
| `COM-001` | A logical send has one immutable client ID and frozen sender, recipient set, and media identity. | V1 optional deduplication exists server-side but is unused by device; [send assessment](../../v1-assessments/recording-send.md). |
| `COM-002` | The same ID and content recover one result; the same ID with different content fails without another side effect. | V1 archive duplicate/conflict handling; [client standard](../../standards/client-application-coding-standards.md) §4. |
| `COM-003` | `accepted` follows complete media validation and archive/inbox commit; push notification failure cannot reverse acceptance. | V1 server commit before notification. |
| `COM-004` | After a lost response, the client queries its operation ID before creating a new send. | V1 receipt lookup exists but device does not use it. |

## Proposed v2 JSON and media schemas

The following is a **single-message, immutable-recipient-set proposal**. It avoids v1's mixture of per-recipient device requests and a `broadcast` flag. It is not owner-approved.

### 1. Create upload — `POST /v2/outgoing` (proposed)

```json
{
  "schema": "fl.message-create/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "recipient_user_ids": ["user-a", "user-b"],
  "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1},
  "sketch": {"format_id": "<color-capable-format>", "present": true}
}
```

| Field | Type, validation, and ownership |
|---|---|
| `schema` | Required literal `fl.message-create/2` in this proposal. |
| `client_message_id` | Required UUID generated once by client and retained through recovery; unique within authenticated sender scope. Server must reject reuse with different frozen content. |
| `recipient_user_ids` | Required non-empty array of unique immutable user IDs, with deterministic ordering; server validates membership and whether self-send is permitted. Maximum count OPEN. No server-side implicit broadcast expansion after creation. |
| `audio` | Required metadata object. Exact codec, sample rate, channels, and duration bounds remain OPEN. Example reflects v1 server's supported PCM subset, not a v2 selection. |
| `sketch` | Optional metadata object. `present` is boolean; when true, `format_id` must identify a color-capable, versioned format required by the sketch experience. Exact format and limit OPEN. |

Proposed response:

```json
{
  "schema": "fl.message-upload/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "state": "open",
  "limits": {"max_audio_bytes": null, "max_sketch_bytes": null, "max_duration_ms": null}
}
```

`message_id` is the server's stable message identity. `state` is `open` or `accepted`; accepted replay returns the existing receipt rather than allocating a new message. Limit values are nullable only in this draft to show they have not been chosen; an accepted wire schema must return concrete limits or omit the object by explicit decision.

### 2. Upload media — `PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}` (proposed)

Body is bytes. `kind` is `audio` or `sketch`; `sequence` is a non-negative integer unique within kind. Metadata is conveyed by required headers or a signed part descriptor; choose one before implementation. The conceptual descriptor is:

```json
{
  "schema": "fl.media-part/2",
  "kind": "audio",
  "sequence": 0,
  "byte_count": 32000,
  "sha256": "<64 lowercase hex characters>",
  "start_ms": 0,
  "duration_ms": 1000
}
```

`start_ms` and `duration_ms` apply to timed audio/sketch data only when the selected format needs them. A duplicate `(message_id, kind, sequence)` with identical bytes and metadata returns the same acknowledgement; different content returns an identity conflict. The acknowledgement must say whether the server durably stored the part. Exact chunk size, ordering, parallelism, retry, and storage guarantee remain OPEN.

### 3. Complete — `POST /v2/outgoing/{message_id}/complete` (proposed)

```json
{
  "schema": "fl.message-complete/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "audio": {"parts": 3, "byte_count": 96000, "sha256": "<64 lowercase hex characters>", "duration_ms": 3000},
  "sketch": {"parts": 1, "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
}
```

`sketch` is absent when no drawing was captured. The server validates complete contiguous media and hashes before acceptance, freezes the recipient set, commits one logical message, and returns the same receipt for an identical retry. This atomic-recipient proposal is blocked by `COM-OPEN-001`.

### 4. Accepted receipt and recovery lookup — response or `GET /v2/outgoing/by-client-id/{client_message_id}` (proposed)

```json
{
  "schema": "fl.send-receipt/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "state": "accepted",
  "accepted_at": "2026-10-11T18:00:00Z",
  "recipients": [
    {"user_id": "user-a", "inbox_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"},
    {"user_id": "user-b", "inbox_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"}
  ],
  "media": {"audio_sha256": "<64 lowercase hex characters>", "sketch_sha256": "<64 lowercase hex characters>"}
}
```

`accepted` means server archive commit and recipient inbox publication, **not** notification delivery or media playback. A lost complete response is recovered by querying the same client ID before creating a new operation. A 404 means “no accepted receipt found,” not necessarily “no open upload”; status lookup must distinguish those states. Exact receipt retention and restart guarantees remain OPEN.

## Proposed typed outcomes and checks

| Exchange | Success | Explicit failures to represent |
|---|---|---|
| Create | Existing or new upload identity | `invalid_recipient`, `unsupported_media`, `identity_conflict`, `too_many_open_uploads`, storage unavailable |
| Part | Durable acknowledgement of exact part | hash mismatch, size/sequence limit, identity conflict, storage unavailable |
| Complete | Stable accepted receipt | missing part, aggregate hash mismatch, unsupported sketch, identity conflict, storage unavailable |
| Receipt/status | Accepted receipt or open-upload status | unknown ID, expired receipt, unauthorized actor |

| Scenario | Event | Proposed check | Remaining dependency |
|---|---|---|---|
| `COM-SCN-001` | Completion commits; response is lost | Receipt lookup by client ID returns the accepted result; no second delivery. | COM-002, COM-004; retention OPEN |
| `COM-SCN-002` | Same client ID arrives with different recipient/media | Identity conflict; no second side effect. | COM-001–002 |
| `COM-SCN-003` | Commit succeeds; recipient push fails | Accepted receipt remains valid; notification is retried or reconciled separately. | COM-003; notification recovery OPEN |
| `COM-SCN-004` | Restart during upload | Status distinguishes open, accepted, and unknown; client resumes only confirmed missing parts. | COM-004; durability/cleanup OPEN |
| `COM-SCN-005` | Selected recipient becomes invalid before commit | Outcome remains OPEN pending atomic-recipient decision. | COM-OPEN-001 |

| ID | Decision needed | Blocks |
|---|---|---|
| `COM-OPEN-001` | One logical message with atomic recipient set versus per-recipient or partial delivery, including invalidated recipients. | Create and receipt semantics |
| `COM-OPEN-002` | Chosen audio codec, color-capable sketch format, limits, and device storage/streaming feasibility. | Media schema |
| `COM-OPEN-003` | Client retention/outbox after send failure, reboot, and server-unavailable receipt lookup; exact `Sent` and failure behavior. | Recovery UX |
| `COM-OPEN-004` | Client ID and receipt retention, expiration, and server restart guarantees. | Lost-response retry |
| `COM-OPEN-005` | Upload part transport/header schema, acknowledgement durability, concurrency, timeout/backoff, and abandoned upload cleanup. | Protocol implementation |

Revision 0.1 (2026-10-11): captured the working v1 multipart/receipt/chunk shapes and proposed a unified send schema while leaving product and feasibility decisions OPEN.
