# V1 assessment: remaining communication contracts

Status: historical implementation assessment and v2 contract inventory; no v2 protocol or product behavior is approved here.

## Scope and evidence

This inventory examines the v1 product firmware (`x02`), product server, shared mailbox/archive, and browser twin. It covers sending a message, learning about and retrieving an inbox, retrieving message media, recording playback state, and changing a user's icon or color. Historical source locators below are relative to the read-only v1 archive; they are evidence locators, not dependencies for new implementation.

| Evidence ID | Historical source and relevant symbols |
|---|---|
| `V1-COM-SEND` | `firmware/v1/v1_record.c` `post_wav` and send loop; `demos/server/v1_product/server.py` `post_message`, `_notify_created`, `outgoing_receipt`; `demos/server/v1_product/archive.py` `import_wav`, `create_upload`; `demos/server/v1_product/chunk_api.py` create/chunk/status/complete routes |
| `V1-COM-INBOX` | `firmware/v1/v1_api.c` `v1_api_reload_inbox`, `v1_api_parse_inbox`, WebSocket handler; `firmware/v1/v1_carousel.c` inbox, media, and playback-state calls; `demos/server/v1_product/server.py` inbox/media/state routes; `demos/server/v1_product/ws.py` notification |
| `V1-COM-STORE` | `demos/server/_shared/user_mailbox.py` `inbox_payload`, `public_message`, `media_path`, `set_view`, `mark_read`, `set_position`; product server archive README's storage and paging description |
| `V1-COM-PROFILE` | `firmware/v1/v1_ui_common.c` `save_profile`, icon/color pick handlers; `demos/server/v1_product/server.py` `put_profile`, `get_hangout`; `demos/server/_shared/user_mailbox.py` `set_profile`, `profile_dict`; browser twin `web/box.js` `saveProfile`, `reloadInbox` |

Related active analysis: [recording and send](recording-send.md), [carousel and playback](carousel-playback.md), [authentication and connectivity](authentication-connectivity.md), and the [sketch experience](../product/experiences/sketch-record-and-playback.md). Follow the [communication-contract authoring guide](../product/communication-contract-authoring.md) when turning an inventory row into a v2 contract.

## Contract inventory

| Proposed v2 contract | V1 exchanges that reveal it | Why it is separate |
|---|---|---|
| [`COM-MESSAGE-DELIVERY`](../product/communication-contracts/message-delivery.md) | Create/upload, recipient selection, server commit, result/receipt query, notification | Defines when a send is accepted, how retries and partial delivery resolve, and how lost responses recover. |
| [`COM-INBOX-SYNC`](../product/communication-contracts/inbox-and-media.md) | WebSocket inbox hint, login/inbox snapshot, pagination, focus continuity | Defines how a client learns about changes and converges after missed, duplicated, or reordered notifications. |
| [`COM-MESSAGE-MEDIA-STATE`](../product/communication-contracts/inbox-and-media.md) | Audio/sketch fetch and range/format handling; viewed/read/position writes | Defines message access, playable completeness, and durable playback state. Linked with inbox sync in one draft. |
| [`COM-USER-PROFILE`](../product/communication-contracts/user-profile.md) | Profile update, returned profile, roster/inbox/login propagation | Defines the user's icon and accent/message color authority, persistence, conflicts, and acknowledgement. |

All four need a [common session/authentication envelope](../product/communication-contracts/user-scoped-envelope.md). The v1 server accepts an endpoint Bearer token plus a separately supplied `X-User-Id` for user-scoped routes. That header is only checked against the roster, not against a server-issued signed-in session. A v2 contract must define how the server proves the acting user and scopes inbox, media, profile, and send operations; inheriting the v1 header check would leave those operations under-specified.

## 1. Message delivery: current exchange and missing contract

**Device as built.** The firmware builds a whole WAV multipart request (`kind=audio`, one `to_user_id` or `broadcast`, `blob`, optional FLSK1 `sketch`) and posts it to `POST /v1/messages` with the endpoint token and `X-User-Id`. It uses a 20-second HTTP timeout. For several selected recipients it sends separate requests sequentially and stops on the first non-200 result. It treats only status 200 as success, does not read the returned receipt, and sends no `client_message_id`. It may therefore show a generic failure after earlier recipients already received a message. The record/send assessment also identifies loss of failed captured work and the 25-second sending-screen guard.

**Server as built.** Multipart accepts an optional `client_message_id`. With one, the archive detects an identical committed message and returns the previous manifest; conflicting recipient/media content produces 409. The server commits media and recipient sequence identities before attempting WebSocket notifications. A notification failure is logged but does not undo a committed send. `GET /v1/outgoing/{client_message_id}` can recover a committed receipt. Broadcast commits one recipient set against shared media. A separate `family-message/1` PCM chunk create/upload/status/complete protocol provides resumable, durable server uploads and duplicate completion handling, but the v1 firmware does not use it and it does not currently support the color-capable sketch format required by the active sketch experience.

**Contract decisions needed.**

- Is one user action one message to an immutable recipient set, or one message per recipient? Decide atomic delivery, partial success, per-recipient receipts, and how a retry avoids duplicates.
- Define the client-generated identity, frozen sender/recipients/media, conflict behavior, acceptance point, and receipt lookup after a lost response or restart.
- Choose the v2 media upload path and its resumability, size/duration/codec/sketch limits, hash verification, completion rule, and abandoned-upload cleanup. Treat the server's chunk path as available historical implementation, not adopted device behavior.
- Specify what the device retains on failure and after reboot, when `Sent` may appear, whether an accepted send remains pending notification, and which failures allow retry.
- Define notification failure separately from send failure. Committed delivery must not be reported as unsent merely because a WebSocket push failed.

## 2. Inbox synchronization: current exchange and missing contract

**As built.** Successful login returns an inbox payload. Later `GET /v1/inbox` returns the newest eight entries by default, with older-page parameters (`limit` 1–16 and `before_seq`) and `has_more`, `next_before_seq`, and `total_messages`. Each entry includes recipient sequence, sender label, read flag, position, duration, and sketch presence; the payload includes last-viewed sequence, unread count, and profile. The firmware reads the default page and has no older-page navigation. Its JSON parser clears its current list before parsing; it takes up to its local maximum, defaults missing read/position fields, and resets focus based on `last_viewed_seq`. The server pushes a WebSocket `inbox` hint with user ID, sequence, kind, and sender to every registered endpoint; the firmware uses the user ID to trigger a full inbox reload. The browser twin also reloads on a hint.

**Contract decisions needed.**

- State whether the push is an advisory hint or an authoritative event, and define sequence/version semantics, deduplication, reconnect catch-up, and periodic reconciliation when a push is missed.
- Define snapshot validity and atomic application. A malformed, truncated, or stale response must not silently replace the currently usable inbox with an empty list or move focus to a different message.
- Define ordering, page boundaries, insertion/removal, total count, and what happens when `last_viewed_seq` is outside the loaded page. Decide whether v2 exposes older messages and how it avoids presenting the latest eight as the complete inbox.
- Define user/session binding for late reloads and notifications, including sign-out and a different user signing in on the same endpoint.
- State the visible behavior of offline inbox metadata and whether stale/cached content can be browsed. The current playback assessment leaves important offline behavior unresolved.

## 3. Message media and playback state: current exchange and missing contract

**As built.** A recipient fetches audio by inbox sequence at `GET /v1/messages/{seq}/blob` and an optional sketch at `/{seq}/sketch`; the product server resolves the sequence inside that user's inbox. The newer message-ID audio route exists for chunk uploads and provides media hash headers, but the v1 carousel still uses sequence routes. Media is stored on disk and served by file response. The carousel writes `PUT /v1/session/view`, `PUT /v1/messages/{seq}/read`, and `PUT /v1/messages/{seq}/position`. The shared mailbox persists these values. The server methods do not verify that the supplied viewed/read/position sequence belongs to that user's inbox before recording the state.

**Contract decisions needed.**

- Choose canonical message identity across list entries, media URLs, notifications, and state writes; specify the scope and stability of recipient sequence versus global message ID.
- Define audio/sketch format versioning, content type, byte-range and resume semantics, integrity checks, missing/corrupt media, and how a partial download affects playback or read status. The active sketch experience requires a color-capable format beyond v1 FLSK1.
- Define when a message becomes read, when and how often position is saved, whether writes are monotonic or may reset to zero, and how duplicate, delayed, or out-of-order writes are resolved.
- Validate that a viewed/read/position write applies to an accessible message and current user. Define a typed not-found or unauthorized result and what the client does with it.
- Specify offline playback truthfully: which media is local, what is streamed, what can resume, and what the UI says when media cannot be fetched.

## 4. User profile: current exchange and missing contract

**As built.** `PUT /v1/profile` accepts optional `avatar_slot` (0–12), `accent_hex`, and `autoplay_new`, then returns the full profile. Profiles persist in the server's mutable user state and are returned in hangout, inbox, and login data. On the device, selecting an icon or color changes local state first and calls `save_profile` with the entire current profile. The HTTP status and response are ignored. The browser twin also changes local state first, but shows `profile save failed` for a non-OK response; it does not automatically roll back the local choice. Its card gradient is saved only in browser local storage. The device's card gradient is a separate local setting. The v1 `accent_hex` represents a user profile accent; it is not the per-stroke sketch color and is not itself an immutable per-message color field.

**Contract decisions needed.**

- Clarify the product meaning of “message color”: sender profile accent on message cards, a recipient-controlled card theme, a color frozen into each sent message, or some combination. Keep it distinct from drawing stroke colors.
- Decide which settings are server-owned and shared across endpoints, which are endpoint-local, and whether icon/color changes apply to existing messages, future messages, or both.
- Define the allowed icon catalog and color values, validation, canonical representation, and fallback when an asset is unavailable on another device.
- Define whether updates are full replacement or partial patch, and how concurrent changes from two endpoints resolve. A whole-profile save of stale local fields can overwrite another setting.
- Define save acknowledgement, optimistic display, failure/retry/rollback, offline edits, and the point at which the user may be told the change is saved.
- Define propagation to the roster and other signed-in endpoints. The v1 inbox hint only covers new messages; it is not a profile-change notification.

## Suggested order

1. Decide the shared signed-in user/session authority for all user-scoped routes.
2. Author `COM-MESSAGE-DELIVERY` with receipt, retry, recipient, and media-completion semantics. It has the highest risk of duplicate or partial side effects.
3. Author `COM-INBOX-SYNC` and `COM-MESSAGE-MEDIA-STATE` together or as linked contracts, with one identity model and recovery after missed pushes.
4. Author `COM-USER-PROFILE` after deciding what “message color” means and which profile fields are shared.

The corresponding experience specifications still own send, playback, and settings behavior. This assessment identifies work; it does not approve v1 behavior for v2.
