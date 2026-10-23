# Message delivery communication contract

| Field | Value |
|---|---|
| ID | `COM-MESSAGE-DELIVERY` |
| Revision | 0.12 |
| Status | Interview draft; send behavior, routes, PCM limits, `flsk2` pixel replay, 16 KiB parts, the empty part acknowledgement, open and accepted success bodies, part retry, the 24-hour open upload, and the durable accepted-ID record are accepted engineering wire |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | Engineering wire for the send routes, success bodies, PCM limits, `flsk2` pixel replay, parts, and the durable accepted-ID record is recorded. No implementation authorization implied |
| Participants | Signed-in endpoint as sender, and the server that archives and publishes the message |
| Related behavior | [`EXP-MESSAGE-RECORD-SEND`](../experiences/message-recording-and-sending.md) `DEC-009`, `DEC-028`–`DEC-036`, `DEC-041`, `DEC-042`, `EXP-014`, `EXP-028`–`EXP-035`, `EXP-040`, `EXP-041`; [recording and send assessment](../../v1-assessments/recording-send.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md); session issued by [endpoint health and login](endpoint-health-and-login.md) |

The send routes below are accepted engineering wire. The session credential is login `COM-019`. HTTP status is login `COM-020`.

`MUST` restates a decided experience requirement or applicable standard. `PROPOSED` is an engineering protocol choice. `OPEN` is not implementation discretion.

## 1. Scope and authority

This contract is the interoperability boundary for one finished send from a signed-in endpoint: create, media upload, complete, and lost-response receipt lookup. It does not define recipient notification, playback, or inbox browsing.

It does **not** run while muted or signed out (`DEC-033`). It does not interrupt PIN login. A late `accepted` result MUST NOT sign anyone in, MUST NOT steal inbox selection, and MUST NOT show `sent` if the person already left the wait screen (`EXP-019`, `EXP-031`).

| Source | Authority here |
|---|---|
| [`EXP-MESSAGE-RECORD-SEND`](../experiences/message-recording-and-sending.md) | Owner-decided send behavior, including truthful `sent`, keep/resume, atomic refusal, and a new recording while one finished message streams |
| [Client application coding standards](../../standards/client-application-coding-standards.md) §§4, 7, 9 | One terminal outcome, no false success, coalesced remote writes |
| [Recording and send assessment](../../v1-assessments/recording-send.md) | V1 as-built evidence, not v2 policy |

## 1a. V1 as-built capture

These historical shapes are not v2 policy.

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Device upload | `POST /v1/messages` multipart: `kind=audio`, `to_user_id` **or** `broadcast`, WAV `blob`, optional FLSK1 `sketch`; endpoint Bearer + `X-User-Id`. The device sends no `client_message_id`. | **Replace** device send identity, `broadcast`, whole-body dependency, and caller-supplied user id. Preserve audio plus optional drawing as product input. |
| Server multipart result | `{"messages":[{"seq":number,"kind":string,"from":string,"to":string,"broadcast_id":string|null,"message_id":string},...]}` after archive commit. Optional `client_message_id` enables identical replay and 409 on content conflict. | **Retain** the accepted-receipt idea. Use one client ID that the device actually reads. |
| Existing receipt lookup | `GET /v1/outgoing/{client_message_id}` returns `schema=family-send-receipt/1`, `state=complete`, or 404. | **Adapt** for lost-response recovery. 404 means no accepted receipt yet, not permission to create a second send. |
| Server chunk subset | JSON `family-message/1` create; numbered PCM chunks; complete; identical replay, conflicts 409. Firmware does not call it. | **Evaluate/adapt.** Not automatic v2. Color-capable sketch is required by the sketch experience. |
| Multi-recipient | Device posted one request per recipient and could stop after a later failure. | **Replace** with one logical send and an atomic recipient set (`DEC-034`). |

## 2. Vocabulary and state ownership

| Term | Meaning and owner |
|---|---|
| Finished send | Local audio (required) plus optional sketch, with a frozen recipient set, after red, 3-minute cap, or mid-record mic failure. Client owns it until `accepted` or an owner-decided discard. |
| Logical send / `client_message_id` | One UUID for this finished send. Retries reuse it. A new recording gets a new id. |
| Frozen recipients | Other hangout members selected when recording started. Not rewritten after freeze (`DEC-036`). |
| Server acceptance | Archive commit and publication into each frozen recipient's inbox. Not notification or playback (`DEC-009`). Not a card in the sender's inbox (`DEC-041`). |
| Queued send | Finished send not yet accepted. Kept across connectivity wait, Boot-leave, mute, and restart-if-stored (`DEC-029`–`DEC-035`). |
| Definite refusal | Non-connectivity server rejection of this logical send. Atomic: nobody receives it. Client discards and does not auto-retry (`DEC-034`). |

The server owns acceptance, recipient inbox publication, and receipt lookup. The client owns capture, the queued blob, and whether `sent` is visible. Recipients' devices learn about the message through inbox sync, not this contract.

## 3. Operation inventory

| Operation | Trigger and direction | Preconditions | While muted / signed out | Open boundary |
|---|---|---|---|---|
| `OP-MESSAGE-CREATE` | Endpoint → server after finish (`preparing`) | Signed in as the sender; frozen non-empty recipients; audio present | MUST NOT run | Codec/limits; route |
| `OP-MEDIA-CHUNK` | Endpoint → server | Open upload from create; same session | MUST NOT run | Part headers, size, durability |
| `OP-MESSAGE-COMPLETE` | Endpoint → server when parts are present | Same `client_message_id` and frozen content | MUST NOT run | Atomic commit implementation |
| `OP-OUTGOING-RECEIPT` | Endpoint → server after lost complete, reboot, or re-login | Signed in as the same sender | MUST NOT run | Retention of receipts |

- Create, chunk, and complete are the same logical send. They MUST NOT start a second message (`EXP-029`).
- They MUST NOT run during PIN login or as a different signed-in user (`EXP-033`).
- Auto-submit when Wi-Fi is up and the session can accept; do not require another red press (`EXP-030`). Retry cadence is delegated.

## 4. Proposed v2 schemas

Examples are illustrative. Do not log media bytes or session secrets.

### 4.1 Create — `POST /v2/outgoing` (accepted route)

```json
{
  "schema": "fl.message-create/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "recipient_user_ids": ["user-jordan", "user-sam"],
  "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1, "duration_ms": 8000},
  "sketch": {"format_id": "flsk2", "present": true}
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.message-create/2`. |
| `client_message_id` | Required UUID generated once for this finished send; unique for this sender. Retained through wait, mute, and restart-if-stored. |
| `recipient_user_ids` | Required non-empty unique frozen ids, deterministic order. No self. No implicit Everyone expansion. At most 7. More than 7 is HTTP 422 `invalid_request` and the client shows `couldn't send`. |
| `audio` | Required. `codec` is `pcm_s16le`, `sample_rate_hz` is 16000, `channels` is 1. `duration_ms` is an integer from 1 through 180000. Bytes are little-endian samples. The server accepts a full 180000 ms, which is 5,760,000 bytes. Larger media is HTTP 413. |
| `sketch` | Optional. Omit when audio-only. When present, `present` is true and `format_id` is `flsk2`. |

Sender is the authenticated session user, not a body field. Create success is HTTP 200. A new upload, or a resume of an upload that is still open, returns this body:

```json
{
  "schema": "fl.send-open/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "state": "open",
  "audio": {"parts_acknowledged": 0},
  "sketch": {"parts_acknowledged": 0}
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.send-open/2`. |
| `client_message_id` | The request id. |
| `message_id` | Required server id for the part and complete routes. Stable for this open upload. |
| `state` | Required literal `open`. |
| `audio.parts_acknowledged` | Required integer ≥ 0. Contiguous audio parts stored from sequence 0. The client resumes at this sequence. |
| `sketch` | Present only when create included a sketch. `parts_acknowledged` counts contiguous sketch parts stored from sequence 0. |

Replaying an already accepted id with the same descriptors returns `fl.send-receipt/2` below, including when only the accepted-ID record remains (`COM-014`). The same id with different recipients or media descriptors is `identity_conflict`.

### 4.2 Upload part — `PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}` (accepted route)

Body is bytes. `kind` is `audio` or `sketch`. `sequence` starts at 0 and is contiguous. Each part except the last is exactly 16384 bytes. The last part is 1 through 16384 bytes. The client sends one part at a time and waits for HTTP 200 `{}` before the next. That `{}` is a JSON success with no `schema` field; the [shared envelope](user-scoped-envelope.md) exempts it along with session refresh. The server stores that acknowledgement before it responds, and a retry of the same bytes replays the acknowledgement. Different bytes for the same sequence are `identity_conflict`.

`flsk2` bytes are a concatenation of 8-byte little-endian points in nondecreasing `t_ms` order: `x` as uint8 in 0…119, `y` as uint8 in 0…119, RGB565 color as uint16, then `t_ms` as uint32 counted from wipe-complete. RGB565 keeps the top 5 red bits, top 6 green bits, and top 5 blue bits of the 24-bit rail color (`R >> 3`, `G >> 2`, `B >> 3`), packed as `(R5 << 11) | (G6 << 5) | B5`. A byte prefix is the drawing up to that time. Audio bytes are the PCM samples described above. Upload of these bytes starts after the recording is finished. An unfinished recording is not uploaded.

Each point is one logical pixel. Replay paints that pixel in `t_ms` order and does not draw a line to the next point. A later point covers an earlier point at the same coordinate. While a finger is down, capture emits points so each new point of that touch is at most one logical pixel from the previous point of that touch (Chebyshev distance ≤ 1). A tap that does not move emits one point. A fast move is interpolated at capture into that adjacent-pixel sequence. Lifting the finger ends the touch. The gap is the absence of points. The color on a point is the rail color in effect when that point is emitted. Two same-color strokes stay separate because replay does not connect across a gap. A stored sequence that skips pixels paints only the stored pixels.

A lost part response retries that same part once immediately. Further tries wait 1 second, then 2 seconds, then 4 seconds, then every 30 seconds. The send stays queued. `sent` waits for acceptance.

The server keeps an open upload that has not been accepted until it is accepted, or until 24 hours after the last acknowledged part. After that expiry, lookup is `not_found`. If the desk still has the finished message, it continues the same `client_message_id`. That is the first delivery. An unfinished recording never creates this upload. An accepted id stays lookup-able as `accepted` (`COM-014`).

### 4.3 Complete — `POST /v2/outgoing/{message_id}/complete` (accepted route)

```json
{
  "schema": "fl.message-complete/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "audio": {"parts": 16, "byte_count": 256000, "sha256": "<64 lowercase hex characters>", "duration_ms": 8000},
  "sketch": {"parts": 1, "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
}
```

`sketch` is omitted for audio-only. At 16 kHz mono signed 16-bit PCM, 8000 ms is 256000 bytes and sixteen 16384-byte parts, as in the example. The server validates contiguous media and hashes, then **atomically** publishes one logical message to every frozen recipient or to none (`COM-005`). Success is HTTP 200 `fl.send-receipt/2` with `state` `accepted`. Identical complete retries return that same receipt. Notification failure after commit MUST NOT reverse `accepted` (`COM-003`).

If any frozen recipient is not a valid current hangout member (other than the sender), complete is a definite refusal for the whole set (`COM-006`). It MUST NOT publish to a subset.

### 4.4 Receipt lookup — `GET /v2/outgoing/by-client-id/{client_message_id}` (accepted route)

```json
{
  "schema": "fl.send-receipt/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b",
  "state": "accepted",
  "accepted_at": "2026-10-21T20:00:00Z",
  "recipients": [
    {"user_id": "user-jordan", "inbox_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"},
    {"user_id": "user-sam", "inbox_message_id": "f43b4697-2608-4d69-b189-2d6c68a6bd7b"}
  ]
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.send-receipt/2`. |
| `client_message_id` | The sender's id for this logical send. |
| `message_id` | Required server id. Stable for this accepted send. |
| `state` | Required literal `accepted`. |
| `accepted_at` | Required RFC 3339 UTC. |
| `recipients[].user_id` | Required frozen recipient id. |
| `recipients[].inbox_message_id` | Present while that recipient's inbox copy exists. Omitted after the copy is gone. |

`accepted` means archive commit and publication into the frozen recipients' inboxes, not that anyone was notified or played the message, and not that the sender received a copy (`COM-003`, `COM-012`). After a lost complete, look up this id **before** creating a new send (`COM-004`).

Media bytes and per-recipient inbox copies may be removed once the message is gone from every recipient inbox. The server still keeps a durable accepted-ID record for that sender and `client_message_id`: `message_id`, `accepted_at`, and the frozen recipient user ids. The record survives inbox deletion and server restart. The 24-hour open-upload timer does not delete it. Create, complete, and lookup of that id return `fl.send-receipt/2` with `state` `accepted`. They do not return `not_found`, and they do not publish a second delivery (`COM-014`). The client uses the `accepted` row below.

| Lookup result | Client |
|---|---|
| `accepted` | Same send; show `sent` only if still on the wait screen. This includes an accepted-ID record whose inbox copies are gone |
| `open` | HTTP 200 `fl.send-open/2`. Resume at `parts_acknowledged` for each present kind, then complete. Replaying an earlier part of the same bytes is also a valid acknowledgement |
| `not_found` | No open upload and no accepted-ID record. If local finished work remains, continue **this** id (create). MUST NOT allocate a new id. An accepted id never takes this branch |
| `unauthenticated` / `forbidden` | Do not upload; keep queued for the matching user |

## 5. Checkable rules

| ID | Rule | Source |
|---|---|---|
| `COM-001` | One immutable `client_message_id` binds sender, frozen recipients, and media. | `EXP-029`; v1 unused server id |
| `COM-002` | Same id and content replay one result. Same id with different content is `identity_conflict` and MUST NOT create another delivery. | Client standard §4 |
| `COM-003` | `accepted` is archive commit plus recipient inbox publication. Push failure cannot reverse it. `sent` is not notification or playback. | `DEC-009`, `EXP-014` |
| `COM-004` | After a lost response, query this id before any new create. | `EXP-029`; v1 lookup unused by device |
| `COM-005` | A frozen multi-recipient send is all-or-nothing. Partial publication is forbidden. | `DEC-034`, `EXP-034` |
| `COM-006` | After freeze, a later hangout change does not rewrite recipients. If the server cannot honor the full frozen set, refuse the whole send (`couldn't send`). | `DEC-036`, `EXP-034` |
| `COM-007` | Keep finished work until `accepted` or a definite refusal, mute-unfinished discard, or storage loss. Connectivity wait auto-resumes the same id. | `DEC-029`–`DEC-031` |
| `COM-008` | MUST NOT upload while muted or signed out. Resume only after that same user signs in. MUST NOT run as a different user. | `DEC-033`, `EXP-033` |
| `COM-009` | A definite refusal discards this attempt, does not auto-retry, and MUST NOT show `sent`. | `DEC-034` |
| `COM-010` | A recording is stored locally and is not uploaded until it finishes. One finished message streams at a time. Further finished messages wait in storage. A new recording may be stored while an earlier finished message streams. Each send keeps its own `client_message_id`. | `DEC-050`, `DEC-051` |
| `COM-011` | If `accepted` arrives after the person left the wait screen, skip `sent` and do not interrupt the inbox. | `EXP-031` |
| `COM-012` | Publish only to frozen recipients. The sender's inbox MUST NOT receive a copy of this send. | `DEC-041`, `EXP-040` |
| `COM-013` | Classify non-success results: no Wi-Fi, timeout, connection reset, and HTTP 5xx keep the send queued (`no Wi-Fi` / `connecting`). An expired or forbidden session keeps it queued. The endpoint refreshes per login `DEC-069` and stays on this wait if refresh cannot succeed. It MUST NOT upload until this same user has a valid session, MUST NOT ask for a PIN, and MUST NOT return to the roster. Unsupported or too-large media, and same `client_message_id` with different content, are definite refusals (`couldn't send`, discard, no auto-retry). A malformed or unknown-schema success body is a wait, never `sent`. Frozen-recipient hangout refusal remains `COM-006`. | `DEC-042`, `DEC-048`, `EXP-041`, `EXP-044` |
| `COM-014` | After acceptance, the server keeps a durable accepted-ID record for that sender and `client_message_id`. Inbox deletion and server restart do not remove it. Create, complete, and lookup return `accepted` and MUST NOT publish another delivery. `not_found` means there is no open upload and no accepted-ID record. The 24-hour limit applies only to an upload that was never accepted. | `COM-002`; no stuck-send deadline |

## 6. Timing, duplicates, and recovery

| Event | Decided / proposed result | Remaining |
|---|---|---|
| Finish while offline | Queue locally; body `no Wi-Fi` / `connecting`; not `sent`. Part retries: one immediate try, then waits of 1, 2, and 4 seconds, then every 30 seconds | None for the cadence |
| Wi-Fi returns | Auto-submit this id on that same cadence | None |
| Complete response lost | Receipt lookup; replay `accepted`; no second message. The same `accepted` result remains after every recipient inbox copy is gone | Durable accepted-ID record (`COM-014`) |
| Mute after finish | Keep queue; no upload until that user signs in | Session token lifetime |
| Restart with stored queue | Resume after that user signs in | Storage medium |
| Restart without storage | No send; no recovery UI | — |
| Same id, different body | `identity_conflict`; no extra delivery | — |
| Frozen recipient left hangout | Whole-send refusal. HTTP 422 `recipient_unavailable` | None for the status |
| 5xx / timeout | Stay queued; connecting. HTTP 5xx, and timeout with no status | None for the status |
| Unsupported / too-large media | `couldn't send`; discard | Same |
| `identity_conflict` | `couldn't send`; discard | Same |
| Unauthenticated / forbidden | Keep queued; do not upload until this user has a valid session. Refresh is attempted per login `DEC-069`. Failure stays on this wait. HTTP 401 or 403 | Session token bytes are login `COM-019` |
| Malformed success body | Keep queued as `connecting`; never `sent` | Same |

There is no product stuck-send deadline. An open upload that has not been accepted is kept until acceptance, or until 24 hours after the last acknowledged part. Lookup after that expiry is `not_found`, and the same `client_message_id` continues if the desk still has the finished message. That continuation is the first delivery. An accepted id is not expired by this timer (`COM-014`).

## 7. Draft acceptance scenarios

| ID | Event | Client / server | Rules |
|---|---|---|---|
| `COM-SCN-001` | Complete commits; response lost | Lookup returns `accepted`; one delivery | COM-002–004 |
| `COM-SCN-002` | Same id, different recipients | `identity_conflict`; no second side effect | COM-001–002 |
| `COM-SCN-003` | Commit ok; recipient push fails | Receipt stays `accepted` | COM-003 |
| `COM-SCN-004` | Restart during open upload; storage survived; Maya signs in | Resume this id; inbox first; skip `sent` if accepted while away | COM-007–008, COM-011 |
| `COM-SCN-005` | Jordan left after freeze; Sam remains | Refuse all; `couldn't send`; nobody received it | COM-005–006, COM-009 |
| `COM-SCN-006` | Mute after finish; Sam then uses the endpoint | Maya's queue does not upload as Sam | COM-008 |
| `COM-SCN-007` | Boot from `no Wi-Fi` wait; later accept | Inbox not interrupted; skip `sent` | COM-011 |
| `COM-SCN-008` | Inbox red while a finished message is streaming | The next recording starts into storage. The earlier stream continues. The same id is not created twice | COM-010 |
| `COM-SCN-009` | Complete accepts Maya → Jordan | Jordan's inbox gains the message; Maya's does not | COM-012 |
| `COM-SCN-010` | Complete times out or returns 5xx | Stay queued; `connecting`; not `sent` | COM-013 |
| `COM-SCN-011` | Unsupported or too-large media | `couldn't send`; discard; nobody received it | COM-009, COM-013 |
| `COM-SCN-012` | Same id, different media descriptors | `identity_conflict`; `couldn't send`; no extra delivery | COM-002, COM-013 |
| `COM-SCN-013` | Session expired while Maya is still signed in locally | Keep queued; do not upload until Maya has a valid session | COM-008, COM-013 |
| `COM-SCN-014` | A part response is lost | One immediate retry of that same part, then waits of 1, 2, and 4 seconds, then every 30 seconds. The send stays queued | COM-007, COM-013 |
| `COM-SCN-015` | An open upload that was never accepted receives no further part for 24 hours | Lookup is `not_found`. The same `client_message_id` continues if the desk still has the finished message. That continuation is the first delivery | COM-004, COM-014 |
| `COM-SCN-016` | Create of a new finished send | HTTP 200 `fl.send-open/2` with `message_id` and `parts_acknowledged` 0. The next part uses that `message_id` | COM-001 |
| `COM-SCN-017` | A part of the same bytes is acknowledged, then retried | Both responses are HTTP 200 `{}` with no `schema` field. One stored part | COM-002 |
| `COM-SCN-018` | Complete commits; the response is lost; recipients later remove the message; the server restarts; the sender resumes this id | Lookup returns `fl.send-receipt/2` `accepted`. No second delivery. `inbox_message_id` may be omitted | COM-002, COM-004, COM-014 |
| `COM-SCN-019` | Sketch bytes contain two separated same-color strokes, one tap, and a fast diagonal interpolated to adjacent pixels | Replay paints only stored pixels. The two strokes do not gain a connecting line. The tap is one pixel. The diagonal is a continuous run of adjacent pixels | COM-OPEN-002 |

## 8. Traceability and follow-on work

| Source | Rules | Scenarios |
|---|---|---|
| `DEC-009`, `EXP-014` | COM-003 | COM-SCN-001, COM-SCN-003 |
| `DEC-029`–`DEC-031`, `EXP-029`–`EXP-031` | COM-004, COM-007, COM-010–011 | COM-SCN-001, COM-SCN-007–008 |
| `DEC-033`–`DEC-035`, `EXP-033`–`EXP-035` | COM-008–009 | COM-SCN-004, COM-SCN-006 |
| `DEC-034`, `DEC-036`, `EXP-034` | COM-005–006 | COM-SCN-005 |
| `DEC-041`, `EXP-040` | COM-012 | COM-SCN-009 |
| `DEC-042`, `EXP-041` | COM-013 | COM-SCN-010–013 |

Owner-visible send behavior in this contract is decided. There are **no** product OPEN items here. Do not interview the owner for the rows below.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. `COM-OPEN-007` has **no remaining work** in this contract.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Closed / engineering implementation | Atomic frozen-set send is decided (`DEC-034`, `COM-005`–`COM-006`) | Commit implementation |
| `COM-OPEN-002` | Accepted engineering wire | Audio is `pcm_s16le` at 16000 Hz, one channel, 1…180000 ms, at most 5,760,000 bytes. Sketch is `flsk2`. At most 7 recipients | None for those limits |
| `COM-OPEN-003` | Accepted engineering wire | The recording and finished messages waiting to stream are kept on attached storage. One finished message streams at a time. A new recording may be stored during that stream | None for this journey |
| `COM-OPEN-004` | Accepted engineering wire | An accepted id keeps a durable accepted-ID record after inbox copies and media are removed. The 24-hour limit applies only to an upload that was never accepted (`COM-014`) | None for that retention |
| `COM-OPEN-005` | Accepted engineering wire | Parts are 16384 bytes, one at a time. A lost part retries once immediately, then after 1, 2, and 4 seconds, then every 30 seconds. A never-accepted upload lasts until acceptance or 24 hours after the last acknowledged part | None |
| `COM-OPEN-006` | Accepted engineering wire | Refusal-versus-wait classes are decided (`DEC-042`, `COM-013`). HTTP numbers are login `COM-020` | None for status numbers |
| `COM-OPEN-007` | Closed | Recipients-only publication is decided (`DEC-041`, `COM-012`) | None in this contract. Inbox snapshots must not invent a sender self-copy |

Owner-decided refusal-versus-wait map (`COM-013`):

| Observation | Client | Source |
|---|---|---|
| No Wi-Fi, timeout, connection reset, HTTP 429, HTTP 5xx | Keep queued; body `no Wi-Fi` or `connecting` | `DEC-042`, `COM-020` |
| Frozen recipient is not a current hangout member | `couldn't send`; discard. HTTP 422 `recipient_unavailable` | `DEC-036` / `COM-006`, `COM-020` |
| Same `client_message_id` with different content (`identity_conflict`) | `couldn't send`; discard. HTTP 409 | `DEC-042`, `COM-020` |
| Unsupported or too-large media | `couldn't send`; discard. HTTP 413 or 415 | `DEC-042`, `COM-020` |
| Unauthenticated / forbidden | Keep queued; do not upload until this same user has a valid session. HTTP 401 or 403 | `DEC-042` / `COM-008`, `COM-020` |
| Malformed or unknown-schema success body | Keep queued as `connecting`; never `sent` | `DEC-042` |

## 9. Readiness

- **Analysis:** ready against locked send behavior.
- **Owner review:** atomic send, keep/resume, recipients-only publication, and refusal-versus-wait classes are recorded. Routes, success bodies, PCM limits, `flsk2` pixel replay, part size, the empty part acknowledgement, part retry, the 24-hour limit on a never-accepted upload, and the durable accepted-ID record are accepted engineering wire.
- **Implementation:** those send rules can be implemented against this contract. Storage medium remains an engineering choice inside keep-if-storage-survives. One finished audio capture is at most 5,760,000 bytes, plus that send's sketch bytes. `COM-OPEN-007` is not an active item. No firmware authorization is implied.
- **Verification:** scenarios planned; no v2 evidence claimed. No implementation authorization.

Revision 0.1 (2026-10-11): captured v1 multipart/receipt/chunk shapes and proposed a unified schema.

Revision 0.2 (2026-10-21): recorded owner-decided atomic frozen-set send, keep/resume outbox, no upload while signed out, skip-`sent` if already left, and one pending send. Closed product meaning of former `COM-OPEN-001` and `COM-OPEN-003`. Failure taxonomy and sender-copy remain to confirm. No wire protocol is approved.

Revision 0.3 (2026-10-21): recorded recipients-only publication (`DEC-041`) and the refusal-versus-wait map (`DEC-042`). Closed product meaning of `COM-OPEN-006` and `COM-OPEN-007`. No wire protocol is approved.

Revision 0.4 (2026-10-21): moved remaining `COM-OPEN` rows to follow-on work. `COM-OPEN-007` has no remaining work in this contract. No product behavior changed. No wire protocol is approved.

Revision 0.5 (2026-10-22): a failed session refresh stays on the send wait and keeps the same queued send, per login `DEC-069`. No wire protocol is approved.

Revision 0.6 (2026-10-22): owner confirmed that wait with login `DEC-072`. No wire protocol is approved.

Revision 0.7 (2026-10-22): accepted the HTTP status numbers in login `COM-020` for refusal and wait. Codec, chunking, and routes stayed follow-on at that revision. No implementation authorization.

Revision 0.8 (2026-10-22): accepted the send routes, `pcm_s16le` at 16000 Hz for up to 180000 ms, at most 7 recipients, `flsk2` points, 16384-byte parts, and receipt lifetime. No implementation authorization.

Revision 0.9 (2026-10-22): `flsk2` points are stored in nondecreasing `t_ms` order. Upload starts after the recording is finished. No implementation authorization.

Revision 0.10 (2026-10-22): accepted one immediate part retry, then waits of 1, 2, and 4 seconds, then every 30 seconds, and a 24-hour limit on an open upload. No implementation authorization.

Revision 0.11 (2026-10-22): specified `fl.send-open/2` and `fl.send-receipt/2` success bodies, exempted the empty part acknowledgement, kept a durable accepted-ID record after inbox deletion, and defined `flsk2` as independently painted pixels with capture interpolation. The 8000 ms complete example is 256000 bytes and sixteen parts. No implementation authorization.

Revision 0.12 (2026-10-23): a finished message streams from attached storage while another recording is stored. Inbox refresh is not held for that stream. No implementation authorization.
