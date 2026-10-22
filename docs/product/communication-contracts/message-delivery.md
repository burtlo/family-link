# Message delivery communication contract

| Field | Value |
|---|---|
| ID | `COM-MESSAGE-DELIVERY` |
| Revision | 0.4 |
| Status | Interview draft; send product meanings from `EXP-MESSAGE-RECORD-SEND` accepted 2026-10-21 including recipients-only publication and refusal-vs-wait classes; v2 schemas are a labeled proposal |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | No v2 wire approved by this document |
| Participants | Signed-in endpoint as sender, and the server that archives and publishes the message |
| Related behavior | [`EXP-MESSAGE-RECORD-SEND`](../experiences/message-recording-and-sending.md) `DEC-009`, `DEC-028`–`DEC-036`, `DEC-041`, `DEC-042`, `EXP-014`, `EXP-028`–`EXP-035`, `EXP-040`, `EXP-041`; [recording and send assessment](../../v1-assessments/recording-send.md); [sketch experience](../experiences/sketch-record-and-playback.md) |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md); session issued by [endpoint health and login](endpoint-health-and-login.md) |

Conceptual `POST /v2/...` paths are **not approved routes**. The `<session credential>` placeholder is not a token format.

`MUST` restates a decided experience requirement or applicable standard. `PROPOSED` is an engineering protocol choice. `OPEN` is not implementation discretion.

## 1. Scope and authority

This contract is the interoperability boundary for one finished send from a signed-in endpoint: create, media upload, complete, and lost-response receipt lookup. It does not define recipient notification, playback, or inbox browsing.

It does **not** run while muted or signed out (`DEC-033`). It does not interrupt PIN login. A late `accepted` result MUST NOT sign anyone in, MUST NOT steal inbox selection, and MUST NOT show `sent` if the person already left the wait screen (`EXP-019`, `EXP-031`).

| Source | Authority here |
|---|---|
| [`EXP-MESSAGE-RECORD-SEND`](../experiences/message-recording-and-sending.md) | Owner-decided send behavior, including truthful `sent`, keep/resume, atomic refusal, and one pending send |
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

### 4.1 Create — `POST /v2/outgoing` (proposed)

```json
{
  "schema": "fl.message-create/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "recipient_user_ids": ["user-jordan", "user-sam"],
  "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1, "duration_ms": 8000},
  "sketch": {"format_id": "<color-capable-format>", "present": true}
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.message-create/2`. |
| `client_message_id` | Required UUID generated once for this finished send; unique for this sender. Retained through wait, mute, and restart-if-stored. |
| `recipient_user_ids` | Required non-empty unique frozen ids, deterministic order. No self. No implicit Everyone expansion. Maximum count OPEN (`COM-OPEN-002`). |
| `audio` | Required. Exact codec, rate, channels, and duration bound OPEN. Example is v1 PCM evidence, not a v2 choice. Duration must be > 0 after wipe-complete clip. |
| `sketch` | Optional. Omit when audio-only. When present, `present` is true and `format_id` is a color-capable versioned format. |

Sender is the authenticated session user, not a body field. Proposed create response: `state` `open` or, on replay of an already accepted id, the accepted receipt. `identity_conflict` if the same id arrives with different recipients or media descriptors.

### 4.2 Upload part — `PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}` (proposed)

Body is bytes. `kind` is `audio` or `sketch`. Duplicate exact part replays the ack; different bytes for the same slot are `identity_conflict`. Acknowledgement durability, chunk size, and parallelism remain `COM-OPEN-005`.

### 4.3 Complete — `POST /v2/outgoing/{message_id}/complete` (proposed)

```json
{
  "schema": "fl.message-complete/2",
  "client_message_id": "4a742960-fb74-4256-a0b5-6b556e8ae1ab",
  "audio": {"parts": 3, "byte_count": 96000, "sha256": "<64 lowercase hex characters>", "duration_ms": 8000},
  "sketch": {"parts": 1, "byte_count": 800, "sha256": "<64 lowercase hex characters>"}
}
```

`sketch` is omitted for audio-only. The server validates contiguous media and hashes, then **atomically** publishes one logical message to every frozen recipient or to none (`COM-005`). Identical complete retries return the same receipt. Notification failure after commit MUST NOT reverse `accepted` (`COM-003`).

If any frozen recipient is not a valid current hangout member (other than the sender), complete is a definite refusal for the whole set (`COM-006`). It MUST NOT publish to a subset.

### 4.4 Receipt lookup — `GET /v2/outgoing/by-client-id/{client_message_id}` (proposed)

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

`accepted` means archive commit and publication into the frozen recipients' inboxes, not that anyone was notified or played the message, and not that the sender received a copy (`COM-003`, `COM-012`). After a lost complete, look up this id **before** creating a new send (`COM-004`).

| Lookup result | Client |
|---|---|
| `accepted` | Same send; show `sent` only if still on the wait screen |
| `open` | Resume missing parts, then complete |
| `not_found` | No accepted receipt. If local finished work remains, continue **this** id (create/resume). MUST NOT allocate a new id |
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
| `COM-010` | At most one pending finished send on this endpoint. A second recording MUST NOT start while it is queued. | `DEC-031` |
| `COM-011` | If `accepted` arrives after the person left the wait screen, skip `sent` and do not interrupt the inbox. | `EXP-031` |
| `COM-012` | Publish only to frozen recipients. The sender's inbox MUST NOT receive a copy of this send. | `DEC-041`, `EXP-040` |
| `COM-013` | Classify non-success results: no Wi-Fi, timeout, connection reset, and HTTP 5xx keep the send queued (`no Wi-Fi` / `connecting`). An expired or forbidden session keeps it queued and MUST NOT upload until this same user has a valid session. Unsupported or too-large media, and same `client_message_id` with different content, are definite refusals (`couldn't send`, discard, no auto-retry). A malformed or unknown-schema success body is a wait, never `sent`. Frozen-recipient hangout refusal remains `COM-006`. | `DEC-042`, `EXP-041` |

## 6. Timing, duplicates, and recovery

| Event | Decided / proposed result | Remaining |
|---|---|---|
| Finish while offline | Queue locally; body `no Wi-Fi` / `connecting`; not `sent` | Retry cadence |
| Wi-Fi returns | Auto-submit this id | Same |
| Complete response lost | Receipt lookup; replay `accepted`; no second message | Receipt retention (`COM-OPEN-004`) |
| Mute after finish | Keep queue; no upload until that user signs in | Session token lifetime |
| Restart with stored queue | Resume after that user signs in | Storage medium |
| Restart without storage | No send; no recovery UI | — |
| Same id, different body | `identity_conflict`; no extra delivery | — |
| Frozen recipient left hangout | Whole-send refusal | HTTP status mapping of this class |
| 5xx / timeout | Stay queued; connecting | HTTP status mapping of this class |
| Unsupported / too-large media | `couldn't send`; discard | Same |
| `identity_conflict` | `couldn't send`; discard | Same |
| Unauthenticated / forbidden | Keep queued; no upload until this user has a valid session | Session lifetime |
| Malformed success body | Keep queued as `connecting`; never `sent` | Same |

There is no product stuck-send deadline. Abandoned **open** uploads after a discard are `COM-OPEN-005`.

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
| `COM-SCN-008` | Inbox red while this send is queued | Return to wait; no second create | COM-010 |
| `COM-SCN-009` | Complete accepts Maya → Jordan | Jordan's inbox gains the message; Maya's does not | COM-012 |
| `COM-SCN-010` | Complete times out or returns 5xx | Stay queued; `connecting`; not `sent` | COM-013 |
| `COM-SCN-011` | Unsupported or too-large media | `couldn't send`; discard; nobody received it | COM-009, COM-013 |
| `COM-SCN-012` | Same id, different media descriptors | `identity_conflict`; `couldn't send`; no extra delivery | COM-002, COM-013 |
| `COM-SCN-013` | Session expired while Maya is still signed in locally | Keep queued; do not upload until Maya has a valid session | COM-008, COM-013 |

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
| `COM-OPEN-002` | Engineering wire | Send kinds and all-or-nothing refusal are decided | Audio codec, color sketch format, size/duration limits, max recipients |
| `COM-OPEN-003` | Closed / engineering implementation | Keep/resume and signed-out rules are decided (`DEC-029`–`DEC-035`) | Storage medium and retry cadence |
| `COM-OPEN-004` | Engineering wire | Lost-response replay is decided | Receipt and `client_message_id` retention across server restart |
| `COM-OPEN-005` | Engineering wire | One pending send and part identity are decided | Part headers, ack durability, chunking, backoff, abandoned-open cleanup |
| `COM-OPEN-006` | Closed / engineering wire | Refusal-versus-wait classes are decided (`DEC-042`, `COM-013`) | Exact HTTP status numbers for those classes |
| `COM-OPEN-007` | Closed | Recipients-only publication is decided (`DEC-041`, `COM-012`) | None in this contract. Inbox snapshots must not invent a sender self-copy |

Owner-decided refusal-versus-wait map (`COM-013`):

| Observation | Client | Source |
|---|---|---|
| No Wi-Fi, timeout, connection reset, HTTP 5xx | Keep queued; body `no Wi-Fi` or `connecting` | `DEC-042` |
| Frozen recipient is not a current hangout member | `couldn't send`; discard | `DEC-036` / `COM-006` |
| Same `client_message_id` with different content (`identity_conflict`) | `couldn't send`; discard | `DEC-042` |
| Unsupported or too-large media | `couldn't send`; discard | `DEC-042` |
| Unauthenticated / forbidden | Keep queued; do not upload until this same user has a valid session | `DEC-042` / `COM-008` |
| Malformed or unknown-schema success body | Keep queued as `connecting`; never `sent` | `DEC-042` |

## 9. Readiness

- **Analysis:** ready against locked send behavior.
- **Owner review:** atomic send, keep/resume, recipients-only publication, and refusal-versus-wait classes are recorded. Remaining review is engineering wire (routes, codecs, chunking, retention, HTTP numbers).
- **Implementation:** blocked by follow-on routes, media formats, retention, chunking, and HTTP mapping (`COM-OPEN-002`, `COM-OPEN-004`–`COM-OPEN-006`). `COM-OPEN-007` is not an active item.
- **Verification:** scenarios planned; no v2 evidence claimed. No implementation authorization.

Revision 0.1 (2026-10-11): captured v1 multipart/receipt/chunk shapes and proposed a unified schema.

Revision 0.2 (2026-10-21): recorded owner-decided atomic frozen-set send, keep/resume outbox, no upload while signed out, skip-`sent` if already left, and one pending send. Closed product meaning of former `COM-OPEN-001` and `COM-OPEN-003`. Failure taxonomy and sender-copy remain to confirm. No wire protocol is approved.

Revision 0.3 (2026-10-21): recorded recipients-only publication (`DEC-041`) and the refusal-versus-wait map (`DEC-042`). Closed product meaning of `COM-OPEN-006` and `COM-OPEN-007`. No wire protocol is approved.

Revision 0.4 (2026-10-21): moved remaining `COM-OPEN` rows to follow-on work. `COM-OPEN-007` has no remaining work in this contract. No product behavior changed. No wire protocol is approved.
