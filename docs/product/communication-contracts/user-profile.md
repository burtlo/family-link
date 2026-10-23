# User profile communication contract

| Field | Value |
|---|---|
| ID | `COM-USER-PROFILE` |
| Revision | 0.9 |
| Status | Product meanings accepted 2026-10-21 and 2026-10-22; v2 schemas remain a labeled proposal |
| Owner | Product owner for behavior; engineering proposal needed for protocol |
| Approval | No approval claimed; no implementation authorization implied |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

## 1. Scope and authority

`OP-PROFILE-READ`, `OP-PROFILE-UPDATE`, and a proposed `OP-PROFILE-HINT` synchronize **server-owned** user presentation: avatar, `profile_accent`, and card background. In core V2, `profile_accent` is not rendered on roster or inbox cards; their face uses `card_background_id`. [Volume](../experiences/settings.md) is endpoint-local and is **not** a profile field. Sketch stroke color stays per recorded stroke in the sketch experience.

| Source | Authority here |
|---|---|
| Product owner, 2026-10-21 | Initially recorded profile color as following the sender’s current profile; superseded for core card rendering by the 2026-10-22 clarification. Everything except volume is server-side; failed save reverts the preview; last write wins |
| Product owner, 2026-10-22 | Keep `profile_accent` out of core V2 card rendering |
| [`EXP-SETTINGS`](../experiences/settings.md) `DEC-009` | Volume is this endpoint’s desk setting, not a profile field |
| [`EXP-MESSAGE-CAROUSEL`](../experiences/message-carousel-and-playback.md) `DEC-018` | First entry after login does not autoplay; v1 `autoplay_new` is not a v2 profile field |
| [User-scoped envelope](user-scoped-envelope.md) | Session credential after PIN success; mute invalidates it |
| [V1 communication inventory](../../v1-assessments/communication-contract-inventory.md) | Historical evidence, not v2 policy |

`MUST` restates a decided product requirement. `PROPOSED` is an engineering protocol choice. Example values must not be shipped merely because they appear in this draft.

## 1a. V1 as-built capture

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Update | `PUT /v1/profile` with optional `avatar_slot` integer 0–12, `accent_hex` string, and `autoplay_new` boolean; endpoint Bearer and caller-supplied `X-User-Id`. | **Retain** avatar and accent as presentation fields. **Replace** authority (`X-User-Id`) and whole-profile overwrite. **Replace** `autoplay_new` (carousel already forbids autoplay on entry). |
| Response | `{"ok":true,"profile":{"avatar_slot":number,"accent_hex":string,"autoplay_new":boolean}}`. The server persists profile state. It prepends `#` to a nonempty color without one and uppercases the value, but does not strictly validate six hex digits. | **Retain** canonical read-after-write. **Adapt** revision and catalog validation. |
| Propagation | Profile appears in hangout, login, and inbox payloads. No dedicated profile-change push. | **Adapt** to revision plus a change hint or documented refresh. V1's current-profile color behavior is not adopted for V2 cards; core V2 cards use `card_background_id`. |
| Device behavior | Icon/color change updates local state before sending the full profile; firmware ignores status/body. Browser twin shows failure toast but keeps optimistic local choice. | **Replace** silent acknowledgement and keeping a failed preview. Preview is allowed; a failed save reverts. |
| Other colors | Browser/device card gradient was local. Sketch stroke color is per recorded stroke. | **Replace** card gradient with a server-owned catalog field. **Keep** sketch stroke color separate. |

## 2. Decided product meaning

- **COM-001:** The server derives the editable user from the authenticated session. A request body or `X-User-Id` MUST NOT choose a different user.
- **COM-002:** An accepted update changes only the named fields, validates all of them, persists atomically, increments `revision` once, and returns canonical state.
- **COM-003:** A lost-response retry of the **same** `operation_id` and exact patch MUST replay the first recorded result without a second write.
- **COM-005:** `profile_accent` remains server-owned profile data, but core V2 roster and inbox cards MUST NOT render it. Their face uses the sender’s current `card_background_id` and its paired `fl-color-text-on-card`. Sketch ink and sent content are unchanged.
- **COM-006:** Volume is endpoint-local (`DEC-009`). Avatar, `profile_accent`, and card background are server-owned profile. v1 `autoplay_new` is not a v2 profile field.
- **COM-007:** A local icon or color change MAY preview immediately. The client MUST NOT claim saved without a successful acknowledgement. If the save does not succeed, revert to the last canonical profile. Do not queue the change for later. The person MUST set it again later if they still want it.
- **COM-008:** Concurrent edits do not prompt and do not merge as a user-facing conflict. The later **accepted** write is canonical (last write wins). Two patches that name different fields may both stick; two patches that name the same field keep the later accepted value.
- **COM-009:** Other signed-in endpoints of that user, and hangout members who can see that user, MUST converge on the canonical profile so avatar and card background stay current. The canonical `profile_accent` value also converges as profile data but has no core V2 card rendering. A missed hint MUST still converge on sign-in or reconnect. A profile hint is not an inbox revision.
- **COM-010:** On roster, recipient, and inbox cards, a missing or unrecognized `avatar_id` shows initials, and a missing or unrecognized `card_background_id` shows `card-bg-01`. That person or message stays visible and selectable. These gaps are not a malformed roster (`DEC-071` in [endpoint login](../experiences/endpoint-login.md); login `COM-018`).

## 3. Proposed v2 schemas

These JSON objects are an **engineering proposal**. They are not approved routes or token formats. Unknown required `schema` values fail as `unsupported_schema` using the [shared error envelope](user-scoped-envelope.md).

Profile uses the **session** credential after PIN success. Mute invalidates it.

### 3.1 Canonical profile — `GET /v2/me/profile` (proposed)

```json
{
  "schema": "fl.user-profile/2",
  "user_id": "user-a",
  "revision": 7,
  "avatar_id": "avatar-03",
  "profile_accent": "#FF6B6B",
  "card_background_id": "card-bg-03",
  "updated_at": "2026-10-21T20:00:00Z"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.user-profile/2` in this proposal. |
| `user_id` | Required immutable user ID from the session, not a writable target. |
| `revision` | Required server-owned integer ≥ 0. Increases on each accepted change. Orders last-write-wins. |
| `avatar_id` | Optional string in this proposal. Absent, empty, or not a known catalog id: identity surfaces show the LAB-14 centered initials fallback (`COM-010`). V2 artwork and IDs are still deferred (`COM-OPEN-003`). Do not carry V1 avatar art forward. |
| `profile_accent` | Required canonical uppercase `#RRGGBB` in this proposal. Allowed palette versus arbitrary hex and future visual use are `COM-OPEN-003`. Core V2 inbox and roster cards do not render this field (`COM-005`). |
| `card_background_id` | Optional string. Known values are `card-bg-01` through `card-bg-08` in [`FOUNDATION-CARD-BACKGROUNDS`](../device-ui/foundations/FOUNDATION-CARD-BACKGROUNDS.md). Absent, empty, or unknown: cards show `card-bg-01` (`COM-010`). V1 color migration remains `COM-OPEN-003`. |
| `updated_at` | Required RFC 3339 UTC. Diagnostics only; `revision` orders changes. |

Do not include volume or `autoplay_new`.

Hangout and inbox presentation MUST use the canonical avatar and `card_background_id`; core V2 cards do not render `profile_accent`. For this journey the sign-in roster carries `avatar_id` and `card_background_id` on each member (login contract). Each inbox item carries optional `sender_avatar_id` and `sender_card_background_id` from the sender's current profile ([inbox and media](inbox-and-media.md) `COM-012`). That snapshot is the signed-in read path, including for a sender who is no longer on the hangout roster. `GET /v2/me/profile` and `PATCH /v2/me/profile` stay proposals for profile editing, which is outside the core journey. A profile change does not advance `inbox_revision` by itself. The next valid inbox snapshot applies the current sender face.

### 3.2 Partial update — `PATCH /v2/me/profile` (proposed)

```json
{
  "schema": "fl.user-profile-patch/2",
  "operation_id": "bfa8883e-a7f8-45bf-a2ef-cf2c7966ccf1",
  "changes": {"avatar_id": "avatar-04", "profile_accent": "#5AA0E8"}
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.user-profile-patch/2`. |
| `operation_id` | Required UUID for this submission. Retries reuse it. A new edit gets a new id. |
| `changes` | Required object with at least one allowed field: `avatar_id`, `profile_accent`, `card_background_id`. Absent fields stay unchanged. Explicit `null` is invalid unless a later field defines a reset. |

The server validates the full patch, persists it, increments `revision` once, and returns `fl.user-profile/2`. Repeating the same `operation_id` and exact patch replays the first result (`COM-003`). The same id with different content is `identity_conflict` and MUST NOT apply a second write.

**Last write (`COM-008`, proposed):** a later **different** `operation_id` is accepted even if another endpoint already wrote. Do not prompt. Do not require the person to resolve `revision_conflict`. `expected_revision` is omitted from this proposal so a stale client cannot block last-write-wins. `revision` on the result still orders what hangout members show.

**Retention (proposed):** how long the server keeps an `operation_id` result is `COM-OPEN-006`.

### 3.3 Change hint — server → interested session (proposed)

```json
{
  "schema": "fl.profile-hint/2",
  "type": "profile_changed",
  "user_id": "user-a",
  "revision": 8
}
```

This is a hint to refetch canonical profile or roster. It MUST NOT by itself paint cards. Recipients are other signed-in endpoints of that user and hangout members who can see that user (`COM-009`). Transport and reconnect backoff are `COM-OPEN-007`. A profile hint MUST NOT be treated as an inbox revision (`COM-005`, `COM-009`).

## 4. Failure and recovery

| Event | Decided client/server result | Remaining contract question |
|---|---|---|
| Invalid avatar or color | `invalid_request`; no partial change; revert preview (`COM-007`) | Allowed catalog values (`COM-OPEN-003`) |
| Save fails or never acknowledges | Revert to last canonical profile. Do not queue. Do not claim saved. Set again later if still wanted (`COM-007`) | Retry cadence of the in-flight request |
| Response lost after the server committed | Retry same `operation_id`; replay first success; keep the preview that matches canonical (`COM-003`) | Result retention (`COM-OPEN-006`) |
| Two endpoints patch at once | Later accepted write wins; no prompt (`COM-008`) | Hint delivery (`COM-OPEN-007`) |
| Profile hint missed | Next sign-in or reconnect refetches; cards converge on current avatar and `card_background_id`; `profile_accent` is not painted on core cards (`COM-005`, `COM-009`). A missing or unknown id uses initials and `card-bg-01` (`COM-010`) | Hint channel |
| Unknown or missing card background or avatar on a card | Show the person or message. Initials and `card-bg-01` (`COM-010`) | Avatar artwork (`COM-OPEN-003`) |
| Mute during an in-flight save | Session invalid; late success cannot keep a preview after sign-out. Next login loads canonical | Same as envelope mute |

## 5. Draft acceptance scenarios

| ID | Starting condition and event | Expected result | Rules |
|---|---|---|---|
| `COM-SCN-001` | Kitchen saves Maya’s new avatar; bedroom is signed in as Maya | Bedroom converges on the same `avatar_id` and `revision` | COM-001–002, COM-009 |
| `COM-SCN-002` | Maya previews a new color; save fails | Preview reverts to last canonical color; not queued; not shown as saved | COM-007 |
| `COM-SCN-003` | Kitchen patches Maya’s avatar; bedroom then patches Maya’s accent | Canonical profile has kitchen’s avatar and bedroom’s accent (different fields). If both patch accent, bedroom’s value remains | COM-008 |
| `COM-SCN-004` | First PATCH succeeds remotely; response is lost; retry reaches server | Same `operation_id` replays the first result; `revision` increments once | COM-003 |
| `COM-SCN-005` | Jordan already has Maya’s message; Maya then changes `profile_accent` | The canonical profile updates and converges, but Jordan’s core V2 message card does not change from the accent update; its face follows Maya’s current `card_background_id`. The message body is not rewritten | COM-005, COM-009 |
| `COM-SCN-006` | Maya changes color while muted or after mute | No durable preview. Next sign-in shows canonical server profile | COM-007, COM-009 |
| `COM-SCN-007` | Jordan's profile has no `avatar_id` and `card_background_id` `card-bg-99` | Roster, recipient, and inbox cards still show Jordan. Face is initials. Fill is `card-bg-01` | COM-010 |

## 6. Traceability and follow-on

| Source | Rules | Scenarios |
|---|---|---|
| Owner 2026-10-21 profile color meaning; owner 2026-10-22 core-card rendering | COM-005 | COM-SCN-005 |
| Owner 2026-10-21 server vs local; `DEC-009`; carousel `DEC-018` | COM-006 | — |
| Owner 2026-10-21 preview/revert | COM-007 | COM-SCN-002, COM-SCN-006 |
| Owner 2026-10-21 last write wins | COM-008, COM-009 | COM-SCN-001, COM-SCN-003 |
| Session authority | COM-001–003 | COM-SCN-001, COM-SCN-004 |
| Owner 2026-10-22 catalog fallback | COM-010 | COM-SCN-007 |

### OPEN items

Owner-visible profile behavior in this contract is decided. There are **no** product OPEN items here.

### Follow-on work (not product OPEN)

Former `COM-OPEN-###` IDs are retained. Do not interview the owner for these rows.

| ID | Kind | Why it is not product OPEN | Remaining work |
|---|---|---|---|
| `COM-OPEN-001` | Closed | `profile_accent` is server-owned profile data but is excluded from core V2 card rendering; cards use `card_background_id` (`COM-005`) | Final allowed accent values and any future visual use remain in `COM-OPEN-003`; wire field name is the proposed `profile_accent` |
| `COM-OPEN-002` | Closed | Volume is local; avatar, accent, and card background are server profile (`COM-006`) | None for ownership |
| `COM-OPEN-003` | Visual catalog and deferred avatar work | Unknown or missing card background uses `card-bg-01`, and a missing or unrecognized avatar id uses initials (`COM-010`). Core V2 cards do not render `profile_accent`. | New avatar art and IDs, final profile accent values and future use, default `avatar_id` when a profile is created, and V1 migration |
| `COM-OPEN-004` | Closed | Preview, then revert if unsaved; set again later (`COM-007`) | In-flight request timeout only |
| `COM-OPEN-005` | Closed | Last write wins; hangout members and the user’s other endpoints converge (`COM-008`, `COM-009`) | Hint transport |
| `COM-OPEN-006` | Engineering wire | Same `operation_id` must replay (`COM-003`) | How long the server retains that result |
| `COM-OPEN-007` | Engineering wire | Who must converge is decided (`COM-009`) | Hint channel, reconnect refetch timing |

## 7. Readiness

- **Analysis:** ready against locked appearance, save-failure, and last-write behavior.
- **Owner review:** server-vs-local ownership, exclusion of `profile_accent` from core V2 card rendering, failed-save revert, and last-write-wins are recorded. The card-background palette is selected. Avatar art and remaining profile appearance details are deferred.
- **Implementation:** profile editing routes remain unaccepted and outside the core journey. Core cards read presentation from the hangout roster and the inbox snapshot. Deferred avatar artwork stays in `COM-OPEN-003`. Unknown card-background fallback is decided (`COM-010`). Icon and message-color **screens** remain the future settings hub, not this contract.
- **Verification:** scenarios planned; no v2 evidence claimed. No implementation authorization.

Revision 0.1 (2026-10-11): captured v1 profile request/response and proposed revisioned partial updates while leaving product meaning and propagation decisions OPEN.

Revision 0.2 (2026-10-21): split product OPEN from follow-on. Volume remains outside this contract. No implementation authorization.

Revision 0.3 (2026-10-21): recorded owner-accepted current-profile message color, server-owned appearance except volume, revert-on-failed-save, and last-write-wins. Dropped v1 `autoplay_new`. No wire protocol is approved.

Revision 0.4 (2026-10-22): Linked `avatar_id` validation to [`FOUNDATION-PROFILE-AVATARS`](../device-ui/foundations/FOUNDATION-PROFILE-AVATARS.md). No wire protocol is approved.

Revision 0.5 (2026-10-22): Adopted the eight card-background IDs and recorded that new avatar art is deferred; LAB-14 initials are the interim display fallback. V1 avatar art is not carried forward. No wire protocol is approved.

Revision 0.6 (2026-10-22): Recorded that `profile_accent` stays server-owned profile data but is not rendered on core V2 roster or inbox cards; those cards use `card_background_id`. No wire protocol is approved.

Revision 0.7 (2026-10-22): Recorded that a missing or unrecognized avatar shows initials and a missing or unrecognized card background shows `card-bg-01`, without hiding the person or message (`COM-010`). Avatar artwork remains deferred. No wire protocol is approved.

Revision 0.8 (2026-10-22): owner confirmed that catalog fallback with login `DEC-072`. No wire protocol is approved.

Revision 0.9 (2026-10-22): core-journey cards read the current avatar and card background from the hangout roster and the inbox snapshot. Profile edit routes stay proposed and outside that journey. No implementation authorization.
